#pragma once

#include "ClimateShelterPolicy.h"

#include <chrono>
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
    void SetEnclosureEnabled(bool enabled) noexcept { _enclosureEnabled = enabled; }
    [[nodiscard]] RadClimateShelterPolicy::Factors Sample(RE::PlayerCharacter* player);
    // Constraint: a listed roofless interior keeps indoor thermal shelter but samples rain cover from the roof raycast.
    [[nodiscard]] RadClimateShelterPolicy::Factors SampleRooflessInterior(RE::PlayerCharacter* player, RE::TESObjectCELL* cell);
    [[nodiscard]] bool LastRoofed() const noexcept { return _lastRoofed; }

private:
    [[nodiscard]] bool ProbeRoof(RE::PlayerCharacter* player, RE::TESObjectCELL* cell) const;
    [[nodiscard]] int ProbeWallHits(RE::PlayerCharacter* player, RE::TESObjectCELL* cell) const;

    RadClimateShelterPolicy::Factors _cached{};
    RadClimateShelterPolicy::Settled _settled{};
    std::uint32_t _lastCellID{ 0 };
    float _lastX{ 0.0f };
    float _lastY{ 0.0f };
    float _lastZ{ 0.0f };
    std::chrono::steady_clock::time_point _lastProbeTime{};
    bool _lastRoofed{ false };
    bool _roofStateKnown{ false };
    bool _lastProbeRoofless{ false };
    bool _enclosureEnabled{ true };
};
