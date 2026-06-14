#pragma once

/*
    TextEscape.h

    Shared text-encoding utilities for notification senders.
    Lifted verbatim from SendSMS (urlEncode) and SendDiscord (jsonEscape) so
    both channels share a single implementation, and the upcoming CustomChannel
    can use them without duplicating code.

    All functions write into caller-supplied fixed buffers — no heap allocation.
*/

#include <stddef.h>

namespace TextEscape {

/// URL-encode a string using percent-encoding (RFC 3986 unreserved set).
/// Spaces become '+'.  Output is always NUL-terminated within outputSize.
void urlEncode(const char* input, char* output, size_t outputSize);

/// Escape a raw string for embedding inside a JSON string literal.
/// Handles '"', '\\', '\n', '\r', '\t'.  Output is always NUL-terminated.
void jsonEscape(const char* input, char* output, size_t outputSize);

} // namespace TextEscape
