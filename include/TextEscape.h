#pragma once

// urlEncode / jsonEscape for notification senders. Fixed output buffers only.

#include <stddef.h>

namespace TextEscape {

/// URL-encode a string using percent-encoding (RFC 3986 unreserved set).
/// Spaces become '+'.  Output is always NUL-terminated within outputSize.
void urlEncode(const char* input, char* output, size_t outputSize);

/// Escape a raw string for embedding inside a JSON string literal.
/// Handles '"', '\\', '\n', '\r', '\t'.  Output is always NUL-terminated.
void jsonEscape(const char* input, char* output, size_t outputSize);

} // namespace TextEscape
