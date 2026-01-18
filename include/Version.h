#pragma once

// Firmware version - update this for each release
// Format: "major.minor.patch"
#define FIRMWARE_VERSION "1.0.0"

// Build information (automatically set by build flags if needed)
#ifndef BUILD_TIMESTAMP
#define BUILD_TIMESTAMP __DATE__ " " __TIME__
#endif
