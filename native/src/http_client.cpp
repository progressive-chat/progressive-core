#include "progressive/http_client.hpp"
#include "progressive/tls_bridge.hpp"
#include <cstring>
#include <sstream>

// Note: Full implementation requires JNI bridge for TLS socket.
// The C++ side handles HTTP protocol formatting and parsing.
// TLS connection is delegated to Android's javax.net.ssl.SSLSocket via JNI.
// See jni_bridge.cpp for the native TLS socket wrapper.

namespace progressive {

// ==== URL Parsing ====

ParsedUrl parseUrl(const std::string& url) {
    ParsedUrl result;

    // Find scheme
    auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return result;
    result.scheme = url.substr(0, schemeEnd);
    result.port = (result.scheme == "https") ? 443 : 80;

    // Find host start (after ://)
    size_t hostStart = schemeEnd + 3;
    auto pathStart = url.find('/', hostStart);
    if (pathStart == std::string::npos) {
        result.host = url.substr(hostStart);
        result.path = "/";
    } else {
        result.host = url.substr(hostStart, pathStart - hostStart);
        result.path = url.substr(pathStart);
    }

    // Check for port in host
    auto portSep = result.host.find(':');
    if (portSep != std::string::npos) {
        result.port = std::stoi(result.host.substr(portSep + 1));
        result.host = result.host.substr(0, portSep);
    }

    result.valid = !result.host.empty();
    return result;
}

// ==== HTTP Protocol Formatting ====

static std::string buildHttpRequest(const HttpRequest& req) {
    auto parsed = parseUrl(req.url);
    if (!parsed.valid) return "";

    std::ostringstream os;
    // Request line
    os << req.method << " " << parsed.path << " HTTP/1.1\r\n";
    // Host header
    os << "Host: " << parsed.host;
    if (parsed.port != 443 && parsed.port != 80)
        os << ":" << parsed.port;
    os << "\r\n";
    // Custom headers
    for (const auto& [k, v] : req.headers) {
        os << k << ": " << v << "\r\n";
    }
    // Content length if body
    if (!req.body.empty()) {
        os << "Content-Length: " << req.body.size() << "\r\n";
    }
    // Connection
    os << "Connection: close\r\n";
    // End headers
    os << "\r\n";
    // Body
    if (!req.body.empty()) {
        os << req.body;
    }
    return os.str();
}

static HttpResponse parseHttpResponse(const std::string& raw) {
    HttpResponse resp;

    // Find header/body separator
    auto bodyStart = raw.find("\r\n\r\n");
    if (bodyStart == std::string::npos) {
        resp.errorMessage = "Invalid HTTP response";
        return resp;
    }

    std::string headerBlock = raw.substr(0, bodyStart);
    resp.body = raw.substr(bodyStart + 4);

    // Parse status line
    auto firstNl = headerBlock.find("\r\n");
    if (firstNl == std::string::npos) {
        resp.errorMessage = "No status line";
        return resp;
    }
    std::string statusLine = headerBlock.substr(0, firstNl);

    // "HTTP/1.1 200 OK"
    auto codeStart = statusLine.find(' ');
    if (codeStart != std::string::npos) {
        auto codeEnd = statusLine.find(' ', codeStart + 1);
        std::string codeStr = (codeEnd != std::string::npos)
            ? statusLine.substr(codeStart + 1, codeEnd - codeStart - 1)
            : statusLine.substr(codeStart + 1);
        resp.statusCode = std::stoi(codeStr);
    }

    // Parse headers
    size_t pos = firstNl + 2;
    while (pos < headerBlock.size()) {
        auto nl = headerBlock.find("\r\n", pos);
        if (nl == std::string::npos) break;
        std::string line = headerBlock.substr(pos, nl - pos);
        auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            // Trim leading space from value
            if (!value.empty() && value[0] == ' ') value = value.substr(1);
            resp.headers[key] = value;
        }
        pos = nl + 2;
    }

    resp.success = true;
    return resp;
}

// ==== Main Execute ====
//
// On Android, the actual socket + TLS is done via the JNI bridge to
// javax.net.ssl.SSLSocket (see tls_bridge.cpp). On desktop there is no
// JVM, so we fall back to libcurl (handles TLS, proxies, redirects).

#ifndef __ANDROID__
#include <curl/curl.h>

static size_t curlWriteCb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* s = static_cast<std::string*>(userdata);
    s->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

static size_t curlHeaderCb(char* buffer, size_t size, size_t nitems, void* userdata) {
    auto* h = static_cast<std::unordered_map<std::string, std::string>*>(userdata);
    std::string line(buffer, size * nitems);
    auto colon = line.find(':');
    if (colon != std::string::npos) {
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        while (!val.empty() && (val.back() == '\r' || val.back() == '\n')) val.pop_back();
        if (!val.empty() && val[0] == ' ') val = val.substr(1);
        (*h)[key] = val;
    }
    return size * nitems;
}

// libcurl-backed execution for desktop builds.
static HttpResponse httpExecuteCurl(const HttpRequest& req) {
    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) {
        resp.errorMessage = "curl init failed";
        return resp;
    }
    std::string bodyBuf;
    std::unordered_map<std::string, std::string> hdrBuf;
    curl_easy_setopt(curl, CURLOPT_URL, req.url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &bodyBuf);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curlHeaderCb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &hdrBuf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, req.timeoutMs);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 15000);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, req.followRedirects ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "progressive-native/1.0");

    if (req.method == "GET") {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    } else if (req.method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (!req.body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(req.body.size()));
        }
    } else if (req.method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        if (!req.body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(req.body.size()));
        }
    } else if (req.method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, req.method.c_str());
    }

    struct curl_slist* hl = nullptr;
    for (const auto& [k, v] : req.headers) {
        std::string entry = k + ": " + v;
        hl = curl_slist_append(hl, entry.c_str());
    }
    if (hl) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hl);

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        resp.errorMessage = "curl: " + std::string(curl_easy_strerror(rc));
        if (hl) curl_slist_free_all(hl);
        curl_easy_cleanup(curl);
        return resp;
    }
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    resp.statusCode = static_cast<int>(code);
    resp.body = std::move(bodyBuf);
    resp.headers = std::move(hdrBuf);
    resp.success = (resp.statusCode >= 200 && resp.statusCode < 300);
    if (hl) curl_slist_free_all(hl);
    curl_easy_cleanup(curl);
    return resp;
}
#endif // __ANDROID__

HttpResponse httpExecute(const HttpRequest& req) {
    // Build HTTP request string
    std::string httpRequest = buildHttpRequest(req);
    if (httpRequest.empty()) {
        return {0, "", {}, false, "Failed to build HTTP request"};
    }

    // Parse URL to get host/port
    auto parsed = parseUrl(req.url);
    if (!parsed.valid) {
        return {0, "", {}, false, "Failed to parse URL: " + req.url};
    }

    // Try TLS bridge (JNI → Java SSLSocket) if available (Android)
    if (tlsBridgeAvailable()) {
        std::string rawResponse = tlsBridgeRequest(
            parsed.host, parsed.port, httpRequest, req.timeoutMs);

        if (!rawResponse.empty()) {
            HttpResponse resp = parseHttpResponse(rawResponse);
            if (resp.success) return resp;
        }
    }

#ifndef __ANDROID__
    // Desktop fallback: libcurl handles TLS, proxies and redirects.
    return httpExecuteCurl(req);
#else
    // Fallback: return error, caller should use Kotlin Retrofit
    return {0, "", {}, false, "JNI TLS bridge not available — use Retrofit fallback"};
#endif
}

// ==== Form Body ====

std::string buildFormBody(const std::unordered_map<std::string, std::string>& params) {
    std::ostringstream os;
    bool first = true;
    for (const auto& [k, v] : params) {
        if (!first) os << "&";
        first = false;
        os << k << "=" << v; // URL encoding delegated to advanced impl
    }
    return os.str();
}

// ==== Rate Limit ====

RateLimitInfo parseRateLimitHeaders(const HttpResponse& response) {
    RateLimitInfo info;
    auto it = response.headers.find("X-RateLimit-Limit");
    if (it != response.headers.end()) info.limit = std::stoi(it->second);
    it = response.headers.find("X-RateLimit-Remaining");
    if (it != response.headers.end()) info.remaining = std::stoi(it->second);
    it = response.headers.find("X-RateLimit-Reset");
    if (it != response.headers.end()) info.resetMs = std::stoll(it->second) * 1000;
    return info;
}

// ==== Matrix Error ====

static std::string extractJsonString(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '"')) pos++;
    size_t end = pos;
    while (end < json.size() && json[end] != '"') { if (json[end] == '\\') end++; end++; }
    return json.substr(pos, end - pos);
}

MatrixErrorResponse parseMatrixError(const std::string& responseBody) {
    MatrixErrorResponse err;
    err.errcode = extractJsonString(responseBody, "errcode");
    err.error = extractJsonString(responseBody, "error");
    auto retry = extractJsonString(responseBody, "retry_after_ms");
    if (!retry.empty()) err.retryAfterMs = std::stoi(retry);
    return err;
}

} // namespace progressive
