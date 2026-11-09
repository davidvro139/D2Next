#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

namespace d2bin {
// Validate the count prefix before any record access or x86-to-native widening.
inline bool recordLayout(const void* data, std::size_t bytes, std::size_t nativeStride,
                         std::int32_t& count, std::size_t& fileStride) {
    count = 0;
    fileStride = 0;
    if (!data || bytes < 4 || !nativeStride) return false;
    const auto* p = static_cast<const unsigned char*>(data);
    const std::uint32_t raw = std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8)
        | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
    if (raw > std::uint32_t(INT32_MAX)) return false;
    const std::size_t payload = bytes - 4;
    if (!raw) {
        if (payload) return false;
        fileStride = nativeStride;
        return true;
    }
    if (payload % raw || payload / raw == 0
        || raw > (std::numeric_limits<std::size_t>::max() - 4) / nativeStride) return false;
    count = static_cast<std::int32_t>(raw);
    fileStride = payload / raw;
    return true;
}
}
