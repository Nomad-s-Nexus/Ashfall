#pragma once

#include <cstdint>

namespace RadNeedsSerialization
{
    // Constraint: legacy record only - recognised and skipped on load, never written.
    inline constexpr std::uint32_t kRecord =
        static_cast<std::uint32_t>('N') |
        (static_cast<std::uint32_t>('E') << 8) |
        (static_cast<std::uint32_t>('E') << 16) |
        (static_cast<std::uint32_t>('D') << 24);
    inline constexpr std::uint32_t kLegacyVersion = 1;
    inline constexpr std::uint32_t kLegacyPayloadSize = sizeof(float) * 3;
}
