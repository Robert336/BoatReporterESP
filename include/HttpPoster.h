#pragma once

#ifndef UNIT_TESTING

// Shared HTTP POST helper for notification channels. Auth credentials are
// never logged; do not embed secrets in the URL.

#include <Arduino.h>

enum class HttpAuthMode : uint8_t {
    NONE   = 0,
    BASIC  = 1,
    BEARER = 2,
};

class HttpPoster {
public:
    // POST body to url. authUser = Basic username or Bearer token;
    // authSecret = Basic password. Returns true on HTTP 2xx.
    static bool post(const char* tag,
                     const char* url,
                     const char* contentType,
                     const char* body,
                     HttpAuthMode authMode  = HttpAuthMode::NONE,
                     const char*  authUser   = nullptr,
                     const char*  authSecret = nullptr);
};

#endif // UNIT_TESTING
