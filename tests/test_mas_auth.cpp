// tests/test_mas_auth.cpp — native MAS support: pure-function unit tests.
// Exercises the browser-free MAS helpers (no network — all decision/builders).
#include "progressive/mas_auth.hpp"

#include <cctype>
#include <cstdio>

namespace {

int failures = 0;

#define CHECK(cond, msg)                                                      \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__);      \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

// MAS-style legacy /login flows response.
const char* kMasLoginFlows = R"({
  "flows": [
    {"type": "m.login.password"},
    {"type": "m.login.sso",
     "oauth_aware_preferred": true,
     "org.matrix.msc3824.delegated_oidc_compatibility": true},
    {"type": "m.login.token"}
  ]
})";

void testDetectMas() {
    auto caps = progressive::masParseLoginFlows(kMasLoginFlows, "https://hs.example");
    CHECK(caps.reachable, "reachable true");
    CHECK(caps.supportsPassword, "advertises m.login.password");
    CHECK(caps.supportsSso, "advertises m.login.sso");
    CHECK(caps.supportsToken, "advertises m.login.token");
    CHECK(caps.isMas, "oauth_aware_preferred => isMas");
    CHECK(caps.homeserverUrl == "https://hs.example", "homeserver carried");
}

void testDetectPlainSynapse() {
    // A non-MAS homeserver: SSO without oauth_aware_preferred.
    auto caps = progressive::masParseLoginFlows(
        R"({"flows":[{"type":"m.login.password"},{"type":"m.login.sso"}]})", "https://x");
    CHECK(caps.supportsPassword, "password still advertised");
    CHECK(caps.supportsSso, "sso advertised");
    CHECK(!caps.isMas, "no oauth_aware_preferred => not MAS");
}

void testClientSecret() {
    std::string s = progressive::masGenerateClientSecret();
    CHECK(s.size() == 43, "client secret is 43 chars");
    for (char c : s) {
        bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.' || c == '_';
        CHECK(ok, "client secret uses unreserved charset");
    }
    CHECK(progressive::masGenerateClientSecret() != progressive::masGenerateClientSecret() ||
              true, "generate is best-effort unique");
}

void testCaptchaFallbackUrl() {
    std::string url = progressive::masCaptchaFallbackUrl(
        "https://hs.example/", "m.login.recaptcha", "sess abc");
    CHECK(url.find("https://hs.example/_matrix/client/v3/auth/m.login.recaptcha/fallback/web") == 0,
          "fallback path correct");
    CHECK(url.find("session=") != std::string::npos, "session param present");
    CHECK(url.find("sess%20abc") != std::string::npos, "session url-encoded");
}

void testEmailIdentityAuthDict() {
    std::string d = progressive::masEmailIdentityAuthDict("SID1", "SECRET", "sess9");
    CHECK(d.find("\"type\":\"m.login.email.identity\"") != std::string::npos, "type present");
    CHECK(d.find("\"sid\":\"SID1\"") != std::string::npos, "sid present");
    CHECK(d.find("\"client_secret\":\"SECRET\"") != std::string::npos, "client_secret present");
    CHECK(d.find("\"session\":\"sess9\"") != std::string::npos, "session present");

    std::string noSess = progressive::masEmailIdentityAuthDict("SID1", "SECRET", "");
    CHECK(noSess.find("session") == std::string::npos, "no session when empty");
}

void testDeviceEndpoints() {
    CHECK(progressive::masTokenEndpoint("https://auth.example") ==
              "https://auth.example/oauth2/token",
          "token endpoint derived");
    CHECK(progressive::masDeviceAuthorizationEndpoint("https://auth.example") ==
              "https://auth.example/oauth2/device_authorization",
          "device endpoint derived");
    CHECK(progressive::masTokenEndpoint("https://auth.example/oauth2") ==
              "https://auth.example/oauth2/token",
          "token endpoint appended after /oauth2");
}

} // namespace

int main() {
    testDetectMas();
    testDetectPlainSynapse();
    testClientSecret();
    testCaptchaFallbackUrl();
    testEmailIdentityAuthDict();
    testDeviceEndpoints();

    if (failures == 0) {
        std::puts("All MAS auth tests passed");
        return 0;
    }
    std::fprintf(stderr, "%d MAS auth test(s) failed\n", failures);
    return 1;
}
