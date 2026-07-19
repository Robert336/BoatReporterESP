#pragma once

/*
    JsonResponder.h

    Small JSON response builder for ConfigServer handlers. Replaces the
    repeated `String json = "{"; json += ...` blocks (30+ of them) and the
    copy-pasted error/success send boilerplate in every handler.

    Design notes:
    - Builds into a single Arduino String (one heap buffer, reserved up
      front) — the WebServer hands responses to the TCP stack as a String
      anyway, so this avoids double copies without changing behavior.
    - Strings are JSON-escaped on the way in (quotes, backslash, control
      chars), which the old hand-concatenated code did NOT do — a SSID or
      webhook URL containing a `"` would previously emit invalid JSON.
    - Only compiled into ConfigServer.cpp (it's the only includer); guarded
      out of unit-test builds like ConfigServer itself.
*/

#ifndef UNIT_TESTING

#include <Arduino.h>
#include <WebServer.h>

class JsonResponder {
public:
    explicit JsonResponder(size_t reserveBytes = 256) {
        json_.reserve(reserveBytes);
        json_ = "{";
    }

    // --- Field adders (chainable) ---
    JsonResponder& raw(const char* key, const char* rawJson) {
        sep(); json_ += "\""; json_ += key; json_ += "\":"; json_ += rawJson; return *this;
    }
    JsonResponder& str(const char* key, const String& value) {
        sep(); json_ += "\""; json_ += key; json_ += "\":\""; escape(value); json_ += "\""; return *this;
    }
    JsonResponder& boolean(const char* key, bool value) {
        return raw(key, value ? "true" : "false");
    }
    JsonResponder& num(const char* key, int value) {
        sep(); json_ += "\""; json_ += key; json_ += "\":"; json_ += String(value); return *this;
    }
    JsonResponder& num(const char* key, uint32_t value) {
        sep(); json_ += "\""; json_ += key; json_ += "\":"; json_ += String(value); return *this;
    }
    JsonResponder& num(const char* key, float value, uint8_t decimals = 2) {
        // Cast selects String(float, unsigned int decimalPlaces) — with a raw
        // uint8_t the call is ambiguous against String(unsigned char, unsigned char).
        sep(); json_ += "\""; json_ += key; json_ += "\":"; json_ += String(value, (unsigned int)decimals); return *this;
    }

    // --- Senders ---
    // Close the object and return it as a String. Used both by send() and by
    // handlers that embed this object as a nested value in an outer response.
    const String& body() {
        if (!closed_) { json_ += "}"; closed_ = true; }
        return json_;
    }

    void send(WebServer* server, int code = 200) {
        server->send(code, "application/json", body());
    }

    // Standardized error body: {"error":"..."}
    static void sendError(WebServer* server, int code, const String& message) {
        JsonResponder r(64 + message.length());
        r.str("error", message);
        r.send(server, code);
    }

    // Standardized success body: {"success":true,"message":"..."} plus any
    // extra fields the caller chains on before send().
    static JsonResponder success(const char* message) {
        JsonResponder r;
        r.boolean("success", true).str("message", message);
        return r;
    }

private:
    String json_;
    bool first_  = true;
    bool closed_ = false;

    void sep() {
        if (!first_) json_ += ",";
        first_ = false;
    }

    void escape(const String& s) {
        for (size_t i = 0; i < s.length(); i++) {
            char c = s[i];
            switch (c) {
                case '"':  json_ += "\\\""; break;
                case '\\': json_ += "\\\\"; break;
                case '\n': json_ += "\\n";  break;
                case '\r': json_ += "\\r";  break;
                case '\t': json_ += "\\t";  break;
                default:
                    if ((uint8_t)c < 0x20) {
                        char buf[7];
                        snprintf(buf, sizeof(buf), "\\u%04x", c);
                        json_ += buf;
                    } else {
                        json_ += c;
                    }
            }
        }
    }
};

#endif // UNIT_TESTING
