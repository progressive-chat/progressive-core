// tests/test_register_flows.cpp — registration UIA flow-decision unit test.
// decideRegistrationFlow() is pure (JSON in, decision out) — no server needed.
#include "core/matrix_client.hpp"

#include <cstdio>

namespace {

int failures = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__);     \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

using progressive::desktop::RegistrationFlowDecision;

void testDummy() {
    std::string sess;
    auto d = progressive::desktop::decideRegistrationFlow(
        R"({"flows":[{"stages":["m.login.dummy"]}],"session":"sess1"})", false, sess);
    CHECK(d == RegistrationFlowDecision::RetryDummy, "dummy flow -> RetryDummy");
    CHECK(sess == "sess1", "dummy flow carries the session");
}

void testTokenGiven() {
    std::string sess;
    auto d = progressive::desktop::decideRegistrationFlow(
        R"({"flows":[{"stages":["m.login.registration_token"]}],"session":"sess2"})",
        true, sess);
    CHECK(d == RegistrationFlowDecision::RetryToken, "token flow + token given -> RetryToken");
}

void testTokenMissing() {
    std::string sess;
    auto d = progressive::desktop::decideRegistrationFlow(
        R"({"flows":[{"stages":["m.login.registration_token"]}],"session":"sess3"})",
        false, sess);
    CHECK(d == RegistrationFlowDecision::TokenRequired,
          "token flow + NO token -> TokenRequired (in-app prompt, no browser)");
}

void testCaptcha() {
    std::string sess;
    auto d = progressive::desktop::decideRegistrationFlow(
        R"({"flows":[{"stages":["m.login.recaptcha"]}],"session":"sess4"})", false, sess);
    CHECK(d == RegistrationFlowDecision::Captcha, "recaptcha flow -> Captcha (browser)");
}

void testDummyBeatsToken() {
    // Servers advertise several flows; dummy is completable without user input.
    std::string sess;
    auto d = progressive::desktop::decideRegistrationFlow(
        R"({"flows":[{"stages":["m.login.dummy"]},
                      {"stages":["m.login.registration_token"]}],"session":"s"})",
        false, sess);
    CHECK(d == RegistrationFlowDecision::RetryDummy, "dummy preferred over token");
}

void testTokenBeatsCaptcha() {
    // Token is completable in-app; captcha is not — token wins when both are
    // advertised (no pointless browser trip).
    std::string sess;
    auto d = progressive::desktop::decideRegistrationFlow(
        R"({"flows":[{"stages":["m.login.recaptcha"]},
                      {"stages":["m.login.registration_token"]}],"session":"s"})",
        false, sess);
    CHECK(d == RegistrationFlowDecision::TokenRequired, "token preferred over captcha");
}

void testUnsupported() {
    std::string sess;
    auto d = progressive::desktop::decideRegistrationFlow(
        R"({"flows":[{"stages":["m.login.sso"]}],"session":"s"})", false, sess);
    CHECK(d == RegistrationFlowDecision::Unsupported, "sso-only -> Unsupported");
}

void testMalformed() {
    std::string sess;
    auto d = progressive::desktop::decideRegistrationFlow("not json", false, sess);
    CHECK(d == RegistrationFlowDecision::Unsupported, "malformed body -> Unsupported");
}

}  // namespace

int main() {
    testDummy();
    testTokenGiven();
    testTokenMissing();
    testCaptcha();
    testDummyBeatsToken();
    testTokenBeatsCaptcha();
    testUnsupported();
    testMalformed();

    if (failures == 0) {
        std::puts("All register-flow tests passed");
        return 0;
    }
    std::fprintf(stderr, "%d register-flow test(s) failed\n", failures);
    return 1;
}
