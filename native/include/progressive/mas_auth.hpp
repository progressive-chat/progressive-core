#pragma once

#include <string>
#include <vector>

#include "progressive/http_client.hpp"

namespace progressive {

// ==== Matrix Authentication Service (MAS) native support ====
//
// Native, browser-free support for Element's matrix-authentication-service
// (https://github.com/element-hq/matrix-authentication-service).
//
// MAS is the reference OIDC/OAuth2 provider that fronts a Matrix homeserver
// (MSC3861). It exposes several flows that can be driven entirely natively
// from C++ — no embedded webview or browser required:
//
//   * legacy password login        POST /_matrix/client/v3/login  (m.login.password)
//   * OAuth2 device authorization  RFC 8628 — approve on *another* device
//   * email verification (UIA)      m.login.email.identity for register / password reset
//   * upstream SSO provider listing (so the UI can show SSO buttons natively)
//
// The ONLY stage that still needs a browser is a CAPTCHA challenge
// (m.login.recaptcha). For that we expose a fallback URL the app opens in the
// system browser. Everything else is handled in-process.
//
// All functions are synchronous and use the shared progressive HTTP client.

// ---- Capabilities / detection ----

struct MasCapabilities {
    bool reachable = false;             // homeserver answered /login
    bool supportsPassword = false;      // m.login.password advertised
    bool supportsSso = false;           // m.login.sso advertised (OIDC)
    bool supportsToken = false;         // m.login.token advertised
    bool isMas = false;                 // looks like a MAS-backed homeserver
    std::string homeserverUrl;
    std::string issuer;                 // OIDC issuer, when discoverable
    std::string errorMessage;
};

// Probe a homeserver and report which MAS-backed auth flows it supports.
// Pure network read — safe to call before showing any dialog.
MasCapabilities masDetect(const std::string& homeserverUrl);

// Parse the legacy login flows JSON (GET /login) into capability flags.
MasCapabilities masParseLoginFlows(const std::string& json,
                                   const std::string& homeserverUrl);

// Discover the OIDC issuer from .well-known/matrix/client.
// Returns empty string if the homeserver does not advertise one.
std::string masIssuerFromWellKnown(const std::string& homeserverUrl);

// ---- Native password login (legacy /login, m.login.password) ----

struct MasLoginResult {
    bool success = false;
    std::string userId;
    std::string accessToken;
    std::string refreshToken;
    std::string deviceId;
    std::string errorMessage;
    std::string errcode;
    bool needsCaptcha = false;          // m.login.recaptcha required
    std::string captchaSession;         // UIA session id for the captcha stage
    bool needsEmail = false;            // m.login.email.identity required
    std::string emailSession;
};

// Log in with username + password natively (no browser). On success returns
// tokens; on M_FORBIDDEN/captcha returns needsCaptcha with a session to
// satisfy via masCaptchaFallbackUrl().
MasLoginResult masPasswordLogin(
    const std::string& homeserverUrl,
    const std::string& username,
    const std::string& password,
    const std::string& initialDeviceDisplayName,
    bool refreshToken = true);

// ---- CAPTCHA (browser-only stage) ----

// Build the fallback URL a client opens in a browser to solve a CAPTCHA (or any
// other stage) for a given UIA session. stageType is e.g. "m.login.recaptcha".
// Format: <hs>/_matrix/client/v3/auth/<stageType>/fallback/web?session=<session>
std::string masCaptchaFallbackUrl(const std::string& homeserverUrl,
                                  const std::string& stageType,
                                  const std::string& session);

// ---- Upstream SSO providers (native listing) ----

struct MasUpstream {
    std::string id;                     // stable upstream id (ULID)
    std::string label;                  // human name ("Google")
    std::string brand;                  // "google"
    std::string loginUrl;               // URL to start this upstream's OIDC login
};

// List configured upstream OIDC providers natively (no browser needed to
// render the SSO buttons). Returns an empty list when the homeserver does not
// expose them (the caller then falls back to the OIDC browser flow).
std::vector<MasUpstream> masListUpstreams(const std::string& homeserverUrl);

// ---- Email verification (native, m.login.email.identity) ----

struct MasEmailRequest {
    bool success = false;
    std::string sid;                    // email validation session id
    std::string clientSecret;          // client-generated secret (echoed back)
    std::string errorMessage;
    std::string errcode;
};

// Generate a client secret for email validation (per MSC: 43-char unreserved).
std::string masGenerateClientSecret();

// Request an email verification code for REGISTRATION.
// POST /_matrix/client/v3/register/email/requestToken
MasEmailRequest masRequestRegistrationEmail(
    const std::string& homeserverUrl,
    const std::string& email,
    const std::string& clientSecret,
    int attempt = 1);

// Request an email verification code for PASSWORD RECOVERY.
// POST /_matrix/client/v3/account/password/email/requestToken
MasEmailRequest masRequestPasswordResetEmail(
    const std::string& homeserverUrl,
    const std::string& email,
    const std::string& clientSecret,
    int attempt = 1);

// Build the UIA auth dict fragment for m.login.email.identity, to embed in a
// subsequent /register or /account/password request.
std::string masEmailIdentityAuthDict(const std::string& sid,
                                     const std::string& clientSecret,
                                     const std::string& session);

// ---- OAuth2 device authorization grant (RFC 8628) — native, no browser ----

struct MasDeviceAuth {
    bool success = false;
    std::string deviceCode;
    std::string userCode;
    std::string verificationUri;            // open on another device
    std::string verificationUriComplete;    // includes user_code (one-click)
    int interval = 5;                       // poll interval (seconds)
    std::string errorMessage;
};

// Derive the device-authorization endpoint from the OIDC issuer.
// MAS serves it at <issuer>/oauth2/device_authorization.
std::string masDeviceAuthorizationEndpoint(const std::string& issuer);

// Derive the OAuth2 token endpoint from the OIDC issuer.
std::string masTokenEndpoint(const std::string& issuer);

// Start a device authorization flow against MAS. The user opens verificationUri
// on another device and approves; the client then polls masPollDeviceToken().
MasDeviceAuth masStartDeviceAuthorization(
    const std::string& deviceAuthEndpoint,
    const std::string& clientId,
    const std::string& scope =
        "openid urn:matrix:org.matrix.msc2967.client:api:*");

// Poll the token endpoint for a device-authorization grant.
// authorizationPending => not approved yet, keep polling on `interval`.
// slowDown => increase the poll interval and keep polling.
struct MasTokenResult {
    bool success = false;
    std::string accessToken;
    std::string refreshToken;
    int expiresIn = 0;
    std::string errorMessage;
    bool authorizationPending = false;
    bool slowDown = false;
    bool expiredToken = false;          // device code no longer valid
};

MasTokenResult masPollDeviceToken(const std::string& tokenEndpoint,
                                  const std::string& clientId,
                                  const std::string& deviceCode,
                                  int interval);

} // namespace progressive
