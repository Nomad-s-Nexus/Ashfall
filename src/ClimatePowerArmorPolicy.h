#pragma once

#include <cstddef>

namespace RadClimatePowerArmorPolicy
{
    inline constexpr std::size_t kFullSuitPieces = 6;

    [[nodiscard]] constexpr bool IsCompleteSuit(bool hasHelmet, std::size_t distinctPowerArmorPieces) noexcept
    {
        return hasHelmet && distinctPowerArmorPieces >= kFullSuitPieces;
    }
}
