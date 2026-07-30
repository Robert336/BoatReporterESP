#pragma once

// Abstract outbound notification channel. send() runs on Core 0 (notifier
// task); isConfigured()/loadCache() must be in-RAM (no NVS I/O on hot path).

#include <stdint.h>
#include <stddef.h>

class NotificationChannel {
public:
    virtual ~NotificationChannel() = default;

    /// Attempt to send message.  Returns true on 2xx HTTP success.
    /// Must not block for more than ~10 s (HTTPClient timeout).
    virtual bool send(const char* message) = 0;

    /// Returns true if the channel has sufficient config to attempt a send.
    /// Used by NotificationWorker to skip unconfigured channels silently.
    virtual bool isConfigured() const = 0;

    /// Short human-readable name used in log lines, e.g. "SMS", "Discord".
    virtual const char* name() const = 0;

    /// Single-bit flag identifying this channel in a NotifMsg.channels bitmask.
    virtual uint8_t channelFlag() const = 0;

    /// Load NVS config into the in-RAM cache.  Called once at startup and
    /// again after any config update so sends pick up new values immediately.
    virtual void loadCache() = 0;
};
