#pragma once

#include "core/Types.h"

namespace nhn {

/// Token bucket, one per session per limited action.
///
/// With no accounts there is nothing to ban and nothing to appeal to, so rate
/// limiting is the only thing standing between the global chat channel and a
/// single client with a loop. A bucket allows a short burst — people paste and
/// press enter twice — while bounding the sustained rate.
class RateLimiter {
public:
    /// @param burst      tokens available at once
    /// @param perSecond  sustained refill rate
    RateLimiter(uint32 burst, double perSecond)
        : _capacity(static_cast<double>(burst)),
          _tokens(static_cast<double>(burst)),
          _refillPerMs(perSecond / 1000.0) {}

    bool TryConsume(TickCount nowMs, double tokens = 1.0) {
        if (_lastRefillMs == 0) {
            _lastRefillMs = nowMs;
        }
        if (nowMs > _lastRefillMs) {
            _tokens += static_cast<double>(nowMs - _lastRefillMs) * _refillPerMs;
            if (_tokens > _capacity) {
                _tokens = _capacity;
            }
            _lastRefillMs = nowMs;
        }

        if (_tokens < tokens) {
            return false;
        }
        _tokens -= tokens;
        return true;
    }

private:
    double _capacity;
    double _tokens;
    double _refillPerMs;
    TickCount _lastRefillMs = 0;
};

}  // namespace nhn
