#pragma once

#include "ClimateShelterPolicy.h"

#include <cstdint>

namespace RE
{
    class PlayerCharacter;
    class TESObjectCELL;
}

class ClimateShelterRuntime
{
public:
    void Reset() noexcept;
    [[nodiscard]] RadClimateShelterPolicy::Factors Sample(RE::PlayerCharacter* player);
    [[nodiscard]] bool LastRoofed() const noexcept { return _lastRoofed; }

private:
    [[nodiscard]] bool ProbeRoof(RE::PlayerCharacter* player, RE::TESObjectCELL* cell) const;

    RadClimateShelterPolicy::Factors _cached{};
    std::uint32_t _lastCellID{ 0 };
    float _lastX{ 0.0f };
    float _lastY{ 0.0f };
    float _lastZ{ 0.0f };
    bool _lastRoofed{ false };
    bool _roofStateKnown{ false };
};
