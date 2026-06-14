#ifdef UNIT_TESTING

#include <unity.h>
#include <string.h>

// Pull in mocks so headers that transitively include <Arduino.h> compile
#ifndef ARDUINO
#include "../mocks/MockArduino.h"
#endif

// The units under test — pure C++, no ESP32 dependencies
// TextEscape.cpp is compiled from src/ via test_build_src = yes in platformio.ini
#include "../../include/TextEscape.h"

// ============================================================================
// TextEscape::urlEncode tests
// ============================================================================

void test_urlEncode_alphanumeric_passthrough() {
    char out[64];
    TextEscape::urlEncode("Hello123", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Hello123", out);
}

void test_urlEncode_space_becomes_plus() {
    char out[64];
    TextEscape::urlEncode("hello world", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("hello+world", out);
}

void test_urlEncode_special_chars() {
    char out[64];
    TextEscape::urlEncode("+1 (555)", out, sizeof(out));
    // '+' -> %2B, ' ' -> '+', '(' -> %28, ')' -> %29
    TEST_ASSERT_EQUAL_STRING("%2B1+%28555%29", out);
}

void test_urlEncode_safe_chars_passthrough() {
    char out[64];
    TextEscape::urlEncode("-_.~", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("-_.~", out);
}

void test_urlEncode_empty_input() {
    char out[64];
    TextEscape::urlEncode("", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
}

void test_urlEncode_null_output_guard() {
    // Should not crash
    TextEscape::urlEncode("hello", nullptr, 0);
    char out[1];
    TextEscape::urlEncode("hello", out, 1);
    TEST_ASSERT_EQUAL_STRING("", out);
}

void test_urlEncode_truncates_when_buffer_full() {
    char out[5]; // only 4 usable chars + NUL
    TextEscape::urlEncode("ABCDE", out, sizeof(out));
    // Should be NUL-terminated and not overflow
    TEST_ASSERT_EQUAL_UINT8('\0', out[4]);
    TEST_ASSERT_EQUAL_STRING("ABCD", out);
}

// ============================================================================
// TextEscape::jsonEscape tests
// ============================================================================

void test_jsonEscape_plain_string_passthrough() {
    char out[64];
    TextEscape::jsonEscape("hello world", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("hello world", out);
}

void test_jsonEscape_double_quote() {
    char out[64];
    TextEscape::jsonEscape("say \"hi\"", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("say \\\"hi\\\"", out);
}

void test_jsonEscape_backslash() {
    char out[64];
    TextEscape::jsonEscape("path\\file", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("path\\\\file", out);
}

void test_jsonEscape_newline() {
    char out[64];
    TextEscape::jsonEscape("line1\nline2", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("line1\\nline2", out);
}

void test_jsonEscape_carriage_return() {
    char out[64];
    TextEscape::jsonEscape("a\rb", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("a\\rb", out);
}

void test_jsonEscape_tab() {
    char out[64];
    TextEscape::jsonEscape("a\tb", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("a\\tb", out);
}

void test_jsonEscape_empty_input() {
    char out[64];
    TextEscape::jsonEscape("", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
}

void test_jsonEscape_null_output_guard() {
    // Should not crash
    TextEscape::jsonEscape("hi", nullptr, 0);
    char out[1];
    TextEscape::jsonEscape("hi", out, 1);
    TEST_ASSERT_EQUAL_STRING("", out);
}

// ============================================================================
// Template substitution logic tests
// The substituteTemplate method is private on CustomChannel, so we test the
// observable behaviour via a minimal extracted helper that mirrors the logic.
// ============================================================================

// Extracted substitute logic (mirrors CustomChannel::substituteTemplate exactly)
static constexpr const char* PLACEHOLDER     = "{{message}}";
static constexpr size_t      PLACEHOLDER_LEN = 11;

static bool substitute(const char* tmpl, const char* msg,
                        char* outBuf, size_t outSize) {
    const char* ph = strstr(tmpl, PLACEHOLDER);
    if (!ph) {
        size_t len = strlen(tmpl);
        if (len >= outSize) return false;
        memcpy(outBuf, tmpl, len + 1);
        return true;
    }
    size_t preLen  = (size_t)(ph - tmpl);
    size_t msgLen  = strlen(msg);
    size_t postLen = strlen(ph + PLACEHOLDER_LEN);
    size_t total   = preLen + msgLen + postLen + 1;
    if (total > outSize) return false;
    memcpy(outBuf,                   tmpl,                preLen);
    memcpy(outBuf + preLen,          msg,                 msgLen);
    memcpy(outBuf + preLen + msgLen, ph + PLACEHOLDER_LEN, postLen + 1);
    return true;
}

void test_template_substitution_basic() {
    char out[128];
    bool ok = substitute("{\"text\":\"{{message}}\"}", "hello", out, sizeof(out));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("{\"text\":\"hello\"}", out);
}

void test_template_substitution_no_placeholder_uses_verbatim() {
    char out[128];
    bool ok = substitute("{\"static\":true}", "ignored", out, sizeof(out));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("{\"static\":true}", out);
}

void test_template_substitution_empty_message() {
    char out[128];
    bool ok = substitute("prefix{{message}}suffix", "", out, sizeof(out));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("prefixsuffix", out);
}

void test_template_substitution_buffer_too_small_returns_false() {
    char out[10]; // too small for result
    bool ok = substitute("{{message}}", "this is a long message that will not fit", out, sizeof(out));
    TEST_ASSERT_FALSE(ok);
}

void test_template_substitution_placeholder_at_start() {
    char out[128];
    bool ok = substitute("{{message}} - end", "alert", out, sizeof(out));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("alert - end", out);
}

void test_template_substitution_placeholder_at_end() {
    char out[128];
    bool ok = substitute("start: {{message}}", "alert", out, sizeof(out));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("start: alert", out);
}

// JSON-escaped message correctly embedded via substitution
void test_template_json_escaped_message() {
    char escaped[64];
    TextEscape::jsonEscape("water level: 32\"", escaped, sizeof(escaped));
    // escaped = water level: 32\"

    char out[128];
    bool ok = substitute("{\"msg\":\"{{message}}\"}", escaped, out, sizeof(out));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("{\"msg\":\"water level: 32\\\"\"}", out);
}

// URL-encoded message correctly embedded in a form template
void test_template_urlencoded_message() {
    char encoded[64];
    TextEscape::urlEncode("water level +5 cm", encoded, sizeof(encoded));
    // encoded = water+level+%2B5+cm

    char out[128];
    bool ok = substitute("body={{message}}&channel=bilge", encoded, out, sizeof(out));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("body=water+level+%2B5+cm&channel=bilge", out);
}

// ============================================================================
// Test runner
// ============================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // urlEncode
    RUN_TEST(test_urlEncode_alphanumeric_passthrough);
    RUN_TEST(test_urlEncode_space_becomes_plus);
    RUN_TEST(test_urlEncode_special_chars);
    RUN_TEST(test_urlEncode_safe_chars_passthrough);
    RUN_TEST(test_urlEncode_empty_input);
    RUN_TEST(test_urlEncode_null_output_guard);
    RUN_TEST(test_urlEncode_truncates_when_buffer_full);

    // jsonEscape
    RUN_TEST(test_jsonEscape_plain_string_passthrough);
    RUN_TEST(test_jsonEscape_double_quote);
    RUN_TEST(test_jsonEscape_backslash);
    RUN_TEST(test_jsonEscape_newline);
    RUN_TEST(test_jsonEscape_carriage_return);
    RUN_TEST(test_jsonEscape_tab);
    RUN_TEST(test_jsonEscape_empty_input);
    RUN_TEST(test_jsonEscape_null_output_guard);

    // Template substitution
    RUN_TEST(test_template_substitution_basic);
    RUN_TEST(test_template_substitution_no_placeholder_uses_verbatim);
    RUN_TEST(test_template_substitution_empty_message);
    RUN_TEST(test_template_substitution_buffer_too_small_returns_false);
    RUN_TEST(test_template_substitution_placeholder_at_start);
    RUN_TEST(test_template_substitution_placeholder_at_end);
    RUN_TEST(test_template_json_escaped_message);
    RUN_TEST(test_template_urlencoded_message);

    return UNITY_END();
}

#endif // UNIT_TESTING
