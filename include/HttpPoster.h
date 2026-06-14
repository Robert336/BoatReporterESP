#pragma once

#ifndef UNIT_TESTING

/*
    HttpPoster.h

    Shared HTTP POST helper for all notification channels.
    Centralises WiFi-connected check, HTTPClient setup, 10 s timeout,
    2xx success detection, and non-2xx response-body snippet logging.

    Auth modes:
      AUTH_NONE   — no Authorization header
      AUTH_BASIC  — Basic user:pass (base64 via HTTPClient::setAuthorization)
      AUTH_BEARER — Bearer token

    Rules:
      - Auth credentials are NEVER logged.
      - The url is logged at DEBUG level (no secrets in well-formed URLs, but
        the caller must not embed credentials in the URL).
*/

#include <Arduino.h>

enum class HttpAuthMode : uint8_t {
    NONE   = 0,
    BASIC  = 1,
    BEARER = 2,
};

class HttpPoster {
public:
    /// POST body to url with optional auth.
    ///
    /// @param tag         Short channel tag for log lines, e.g. "[SMS]"
    /// @param url         Full HTTPS/HTTP endpoint
    /// @param contentType Content-Type header value
    /// @param body        POST body string (NUL-terminated)
    /// @param authMode    Authentication scheme
    /// @param authUser    Basic: username;  Bearer: token;  NONE: ignored
    /// @param authSecret  Basic: password;  Bearer: ignored; NONE: ignored
    /// @return true if the server responded with a 2xx status code
    static bool post(const char* tag,
                     const char* url,
                     const char* contentType,
                     const char* body,
                     HttpAuthMode authMode  = HttpAuthMode::NONE,
                     const char*  authUser   = nullptr,
                     const char*  authSecret = nullptr);
};

#endif // UNIT_TESTING
