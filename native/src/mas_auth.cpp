#include "progressive/mas_auth.hpp"
#include "progressive/olm.hpp"
#include "progressive/string_utils.hpp"

#include <sstream>
#include <cstdlib>

namespace progressive {

// ==== Small JSON helpers (manual, allocation-light) ====

namespace {

// Bias-free random string from the CSPRNG (same source as the rest of core).
std::string masRandomString(const char* charset, std::size_t n, int length) {
    std::string result;
    result.reserve(length);
    const std::size_t setSize = n;
    const unsigned reject = 256 - (256 % setSize);
    while (result.size() < static_cast<std::size_t>(length)) {
        std::string bytes = generateRandomBytes(length * 2);
        for (unsigned char b : bytes) {
            if (result.size() >= static_cast<std::size_t>(length)) break;
            if (b >= reject) continue;
            result += charset[static_cast<std::size_t>(b) % setSize];
        }
    }
    return result;
}

// Extract a JSON string value for `"key": "value"`. Handles escaped quotes.
std::string extractString(const std::string& json, const std::string& key) {
    auto pos = json.find('"' + key + '"');
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size() || json[pos] != '"') return "";
    pos++;
    std::string out;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            out += json[pos + 1];
            pos += 2;
        } else {
            out += json[pos];
            pos++;
        }
    }
    return out;
}

// Extract an integer value for `"key": 123`.
int extractInt(const std::string& json, const std::string& key, int def = 0) {
    auto pos = json.find('"' + key + '"');
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return def;
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    try {
        std::size_t consumed = 0;
        int v = std::stoi(json.substr(pos), &consumed);
        if (consumed == 0) return def;
        return v;
    } catch (...) {
        return def;
    }
}

bool contains(const std::string& json, const std::string& needle) {
    return json.find(needle) != std::string::npos;
}

// Base path: strip trailing slash, ensure we can append /_matrix/...
std::string normalizeBase(const std::string& url) {
    std::string out = url;
    if (!out.empty() && out.back() == '/') out.pop_back();
    return out;
}

} // namespace

// ==== Detection / capabilities ====

MasCapabilities masParseLoginFlows(const std::string& json,
                                    const std::string& homeserverUrl) {
    MasCapabilities caps;
    caps.reachable = true;
    caps.homeserverUrl = homeserverUrl;
    caps.supportsPassword = contains(json, "m.login.password");
    caps.supportsSso = contains(json, "m.login.sso");
    caps.supportsToken = contains(json, "m.login.token");
    // A MAS-backed homeserver advertises OIDC-aware SSO on the legacy endpoint.
    caps.isMas = caps.supportsSso && contains(json, "oauth_aware_preferred");
    return caps;
}

std::string masIssuerFromWellKnown(const std::string& homeserverUrl) {
    std::string url = normalizeBase(homeserverUrl) + "/.well-known/matrix/client";
    auto resp = httpGet(url);
    if (!resp.isOk()) return "";
    // {"org.matrix.msc2965.authentication":{"issuer":"https://auth..."}}
    auto pos = resp.body.find("\"org.matrix.msc2965.authentication\"");
    if (pos == std::string::npos) return "";
    auto issPos = resp.body.find("\"issuer\"", pos);
    if (issPos == std::string::npos) return "";
    return extractString(resp.body.substr(issPos), "issuer");
}

MasCapabilities masDetect(const std::string& homeserverUrl) {
    std::string url = normalizeBase(homeserverUrl) + "/_matrix/client/v3/login";
    auto resp = httpGet(url);
    if (!resp.isOk()) {
        MasCapabilities caps;
        caps.homeserverUrl = homeserverUrl;
        caps.errorMessage = "No response from /login (HTTP " +
                            std::to_string(resp.statusCode) + ")";
        return caps;
    }
    MasCapabilities caps = masParseLoginFlows(resp.body, homeserverUrl);
    caps.issuer = masIssuerFromWellKnown(homeserverUrl);
    return caps;
}

// ==== Native password login ====

MasLoginResult masPasswordLogin(const std::string& homeserverUrl,
                                const std::string& username,
                                const std::string& password,
                                const std::string& initialDeviceDisplayName,
                                bool refreshToken) {
    MasLoginResult result;
    std::ostringstream body;
    body << R"({"type":"m.login.password")";
    body << R"(,"identifier":{"type":"m.id.user","user":")" << username << R"("})";
    body << R"(,"password":")" << password << R"(")";
    if (!initialDeviceDisplayName.empty())
        body << R"(,"initial_device_display_name":")" << initialDeviceDisplayName << R"(")";
    body << R"(,"refresh_token":)" << (refreshToken ? "true" : "false");
    body << "}";

    std::string url = normalizeBase(homeserverUrl) + "/_matrix/client/v3/login";
    auto resp = httpPost(url, body.str());

    if (resp.isOk()) {
        result.success = true;
        result.userId = extractString(resp.body, "user_id");
        result.accessToken = extractString(resp.body, "access_token");
        result.refreshToken = extractString(resp.body, "refresh_token");
        result.deviceId = extractString(resp.body, "device_id");
        return result;
    }

    // 401 → UIA required (captcha / email) or bad credentials.
    if (resp.statusCode == 401) {
        std::string errcode = extractString(resp.body, "errcode");
        result.errcode = errcode;
        result.errorMessage = extractString(resp.body, "error");
        std::string session = extractString(resp.body, "session");
        if (contains(resp.body, "m.login.recaptcha")) {
            result.needsCaptcha = true;
            result.captchaSession = session;
        } else if (contains(resp.body, "m.login.email.identity")) {
            result.needsEmail = true;
            result.emailSession = session;
        }
        return result;
    }

    result.errcode = extractString(resp.body, "errcode");
    result.errorMessage = extractString(resp.body, "error");
    if (result.errorMessage.empty())
        result.errorMessage = "Login failed (HTTP " + std::to_string(resp.statusCode) + ")";
    return result;
}

// ==== CAPTCHA fallback URL ====

std::string masCaptchaFallbackUrl(const std::string& homeserverUrl,
                                  const std::string& stageType,
                                  const std::string& session) {
    std::ostringstream url;
    url << normalizeBase(homeserverUrl)
        << "/_matrix/client/v3/auth/" << stageType << "/fallback/web";
    if (!session.empty()) url << "?session=" << urlEncode(session);
    return url.str();
}

// ==== Upstream SSO providers ====

std::vector<MasUpstream> masListUpstreams(const std::string& homeserverUrl) {
    std::vector<MasUpstream> out;
    // MAS exposes the configured upstream providers here. The exact path is
    // server-dependent; try the documented unstable path, then the v1 path.
    static const char* paths[] = {
        "/_matrix/client/unstable/org.matrix.msc2965.authn/upstreams",
        "/_matrix/client/v1/authn/upstreams",
        "/_matrix/client/v3/authn/upstreams",
    };
    std::string base = normalizeBase(homeserverUrl);
    for (const char* p : paths) {
        auto resp = httpGet(base + p);
        if (!resp.isOk()) continue;
        // Upstreams are an array of objects with id / human_name / brand_name /
        // and a login_url. Parse them with a simple scanner.
        const std::string& json = resp.body;
        std::size_t arr = json.find('[');
        if (arr == std::string::npos) continue;
        std::size_t i = arr + 1;
        while (i < json.size()) {
            std::size_t objStart = json.find('{', i);
            if (objStart == std::string::npos) break;
            // find matching close brace (no nesting expected in these records)
            std::size_t depth = 0, j = objStart;
            for (; j < json.size(); ++j) {
                if (json[j] == '{') depth++;
                else if (json[j] == '}') { depth--; if (depth == 0) break; }
            }
            if (j >= json.size()) break;
            std::string rec = json.substr(objStart, j - objStart + 1);
            MasUpstream up;
            up.id = extractString(rec, "id");
            up.label = extractString(rec, "human_name");
            if (up.label.empty()) up.label = extractString(rec, "label");
            up.brand = extractString(rec, "brand_name");
            if (up.brand.empty()) up.brand = extractString(rec, "brand");
            up.loginUrl = extractString(rec, "login_url");
            if (up.id.empty() && up.loginUrl.empty()) {
                i = j + 1;
                continue;
            }
            out.push_back(std::move(up));
            i = j + 1;
        }
        if (!out.empty()) break;
    }
    return out;
}

// ==== Email verification ====

std::string masGenerateClientSecret() {
    static const char cs[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    return masRandomString(cs, sizeof(cs) - 1, 43);
}

MasEmailRequest masRequestRegistrationEmail(const std::string& homeserverUrl,
                                            const std::string& email,
                                            const std::string& clientSecret,
                                            int attempt) {
    MasEmailRequest r;
    r.clientSecret = clientSecret;
    std::ostringstream body;
    body << R"({"email":")" << email << R"(")";
    body << R"(,"client_secret":")" << clientSecret << R"(")";
    body << R"(,"send_attempt":)" << attempt << "}";

    std::string url = normalizeBase(homeserverUrl) +
                      "/_matrix/client/v3/register/email/requestToken";
    auto resp = httpPost(url, body.str());
    if (resp.isOk()) {
        r.success = true;
        r.sid = extractString(resp.body, "sid");
    } else {
        r.errcode = extractString(resp.body, "errcode");
        r.errorMessage = extractString(resp.body, "error");
        if (r.errorMessage.empty())
            r.errorMessage = "Email request failed (HTTP " + std::to_string(resp.statusCode) + ")";
    }
    return r;
}

MasEmailRequest masRequestPasswordResetEmail(const std::string& homeserverUrl,
                                             const std::string& email,
                                             const std::string& clientSecret,
                                             int attempt) {
    MasEmailRequest r;
    r.clientSecret = clientSecret;
    std::ostringstream body;
    body << R"({"email":")" << email << R"(")";
    body << R"(,"client_secret":")" << clientSecret << R"(")";
    body << R"(,"send_attempt":)" << attempt << "}";

    std::string url = normalizeBase(homeserverUrl) +
                      "/_matrix/client/v3/account/password/email/requestToken";
    auto resp = httpPost(url, body.str());
    if (resp.isOk()) {
        r.success = true;
        r.sid = extractString(resp.body, "sid");
    } else {
        r.errcode = extractString(resp.body, "errcode");
        r.errorMessage = extractString(resp.body, "error");
        if (r.errorMessage.empty())
            r.errorMessage = "Email request failed (HTTP " + std::to_string(resp.statusCode) + ")";
    }
    return r;
}

std::string masEmailIdentityAuthDict(const std::string& sid,
                                     const std::string& clientSecret,
                                     const std::string& session) {
    std::ostringstream body;
    body << R"({"type":"m.login.email.identity")";
    body << R"(,"threepid_creds":{"sid":")" << sid << R"(","client_secret":")"
         << clientSecret << R"("})";
    if (!session.empty()) body << R"(,"session":")" << session << R"(")";
    body << "}";
    return body.str();
}

// ==== Native registration (generic Matrix UIA) ====

MasRegistrationBegin masBeginRegistration(const std::string& homeserverUrl) {
    MasRegistrationBegin r;
    std::string url = normalizeBase(homeserverUrl) + "/_matrix/client/v3/register";
    auto resp = httpPost(url, "{}");
    if (!resp.isOk() && resp.statusCode != 401) {
        r.errcode = extractString(resp.body, "errcode");
        r.errorMessage = extractString(resp.body, "error");
        if (r.errorMessage.empty())
            r.errorMessage = "Registration start failed (HTTP " +
                             std::to_string(resp.statusCode) + ")";
        return r;
    }
    r.session = extractString(resp.body, "session");
    if (r.session.empty()) {
        r.errorMessage = "No UIA session returned by server";
        return r;
    }
    r.success = true;
    r.needsCaptcha = contains(resp.body, "m.login.recaptcha");
    r.needsEmail = contains(resp.body, "m.login.email.identity");
    r.captchaPublicKey = extractString(resp.body, "public_key");
    if (r.needsCaptcha)
        r.captchaFallbackUrl = masCaptchaFallbackUrl(homeserverUrl,
                                                     "m.login.recaptcha", r.session);
    return r;
}

MasRegistrationResult masCompleteRegistration(
    const std::string& homeserverUrl,
    const std::string& username,
    const std::string& password,
    const std::string& session,
    const std::string& emailSid,
    const std::string& emailClientSecret,
    bool inhibitLogin) {
    MasRegistrationResult r;
    r.session = session;

    std::string emailAuth = masEmailIdentityAuthDict(emailSid, emailClientSecret, session);

    std::ostringstream body;
    body << R"({"auth":)" << emailAuth;
    body << R"(,"username":")" << username << R"(")";
    body << R"(,"password":")" << password << R"(")";
    body << R"(,"inhibit_login":)" << (inhibitLogin ? "true" : "false");
    body << "}";

    std::string url = normalizeBase(homeserverUrl) + "/_matrix/client/v3/register";
    auto resp = httpPost(url, body.str());

    if (resp.isOk()) {
        r.success = true;
        r.userId = extractString(resp.body, "user_id");
        r.accessToken = extractString(resp.body, "access_token");
        r.deviceId = extractString(resp.body, "device_id");
        return r;
    }

    r.errcode = extractString(resp.body, "errcode");
    if (resp.statusCode == 401) {
        r.needsCaptcha = contains(resp.body, "m.login.recaptcha");
        r.needsEmail = contains(resp.body, "m.login.email.identity");
    }
    r.errorMessage = extractString(resp.body, "error");
    if (r.errorMessage.empty())
        r.errorMessage = "Registration failed (HTTP " + std::to_string(resp.statusCode) + ")";
    return r;
}

// ==== OAuth2 device authorization grant (RFC 8628) ====

std::string masDeviceAuthorizationEndpoint(const std::string& issuer) {
    std::string out = normalizeBase(issuer);
    if (out.find("/oauth2") == std::string::npos)
        out += "/oauth2/device_authorization";
    else
        out += "/device_authorization";
    return out;
}

std::string masTokenEndpoint(const std::string& issuer) {
    std::string out = normalizeBase(issuer);
    if (out.find("/oauth2") == std::string::npos)
        out += "/oauth2/token";
    else
        out += "/token";
    return out;
}

MasDeviceAuth masStartDeviceAuthorization(const std::string& deviceAuthEndpoint,
                                          const std::string& clientId,
                                          const std::string& scope) {
    MasDeviceAuth result;
    std::string body = "client_id=" + urlEncode(clientId);
    if (!scope.empty()) body += "&scope=" + urlEncode(scope);

    auto resp = httpPost(deviceAuthEndpoint, body,
                         {{"Content-Type", "application/x-www-form-urlencoded"}});
    if (!resp.isOk()) {
        result.errorMessage = "Device authorization failed (HTTP " +
                              std::to_string(resp.statusCode) + ")";
        if (resp.statusCode == 0) result.errorMessage = "No response from authorization server";
        return result;
    }

    result.success = true;
    result.deviceCode = extractString(resp.body, "device_code");
    result.userCode = extractString(resp.body, "user_code");
    result.verificationUri = extractString(resp.body, "verification_uri");
    if (result.verificationUri.empty())
        result.verificationUri = extractString(resp.body, "verification_url");
    result.verificationUriComplete =
        extractString(resp.body, "verification_uri_complete");
    result.interval = extractInt(resp.body, "interval", 5);
    if (result.interval <= 0) result.interval = 5;
    return result;
}

MasTokenResult masPollDeviceToken(const std::string& tokenEndpoint,
                                  const std::string& clientId,
                                  const std::string& deviceCode,
                                  int interval) {
    MasTokenResult result;
    std::string body = "grant_type=urn:ietf:params:oauth:grant-type:device_code";
    body += "&client_id=" + urlEncode(clientId);
    body += "&device_code=" + urlEncode(deviceCode);

    auto resp = httpPost(tokenEndpoint, body,
                         {{"Content-Type", "application/x-www-form-urlencoded"}},
                         /*timeoutMs=*/interval * 1000 + 30000);
    if (resp.isOk()) {
        result.success = true;
        result.accessToken = extractString(resp.body, "access_token");
        result.refreshToken = extractString(resp.body, "refresh_token");
        result.expiresIn = extractInt(resp.body, "expires_in", 0);
        return result;
    }

    std::string err = toLower(extractString(resp.body, "error"));
    if (err == "authorization_pending") {
        result.authorizationPending = true;
    } else if (err == "slow_down") {
        result.slowDown = true;
    } else if (err == "expired_token") {
        result.expiredToken = true;
    } else {
        result.errorMessage = extractString(resp.body, "error_description");
        if (result.errorMessage.empty()) result.errorMessage = err;
    }
    return result;
}

} // namespace progressive
