#include "TextEscape.h"
#include <ctype.h>

namespace TextEscape {

void urlEncode(const char* input, char* output, size_t outputSize) {
    if (!input || !output || outputSize == 0) {
        if (output && outputSize > 0) output[0] = '\0';
        return;
    }

    size_t outIdx = 0;

    for (size_t i = 0; input[i] != '\0' && outIdx < outputSize - 1; i++) {
        char c = input[i];

        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            output[outIdx++] = c;
        } else if (c == ' ') {
            output[outIdx++] = '+';
        } else {
            // Percent-encode: need 3 chars (%XX) plus room for NUL
            if (outIdx < outputSize - 4) {
                output[outIdx++] = '%';
                // Upper nibble
                char hex = (c >> 4) & 0x0F;
                output[outIdx++] = hex < 10 ? '0' + hex : 'A' + hex - 10;
                // Lower nibble
                hex = c & 0x0F;
                output[outIdx++] = hex < 10 ? '0' + hex : 'A' + hex - 10;
            }
        }
    }
    output[outIdx] = '\0';
}

void jsonEscape(const char* input, char* output, size_t outputSize) {
    if (!input || !output || outputSize == 0) {
        if (output && outputSize > 0) output[0] = '\0';
        return;
    }

    size_t outIdx = 0;

    for (size_t i = 0; input[i] != '\0' && outIdx < outputSize - 2; i++) {
        char c = input[i];
        switch (c) {
            case '"':
                if (outIdx < outputSize - 2) { output[outIdx++] = '\\'; output[outIdx++] = '"';  }
                break;
            case '\\':
                if (outIdx < outputSize - 2) { output[outIdx++] = '\\'; output[outIdx++] = '\\'; }
                break;
            case '\n':
                if (outIdx < outputSize - 2) { output[outIdx++] = '\\'; output[outIdx++] = 'n';  }
                break;
            case '\r':
                if (outIdx < outputSize - 2) { output[outIdx++] = '\\'; output[outIdx++] = 'r';  }
                break;
            case '\t':
                if (outIdx < outputSize - 2) { output[outIdx++] = '\\'; output[outIdx++] = 't';  }
                break;
            default:
                if (outIdx < outputSize - 1) { output[outIdx++] = c; }
                break;
        }
    }
    output[outIdx] = '\0';
}

} // namespace TextEscape
