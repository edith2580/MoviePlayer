#pragma once

#include <QString>
#include <cmath>

// Formats a playback time as "MM:SS", or "H:MM:SS" from one hour up.
inline QString FormatTime(double seconds) {
    const long long total = static_cast<long long>(std::floor(seconds < 0.0 ? 0.0 : seconds));
    if (total >= 3600) {
        return QString::asprintf("%lld:%02lld:%02lld", total / 3600, (total / 60) % 60, total % 60);
    }
    return QString::asprintf("%02lld:%02lld", total / 60, total % 60);
}
