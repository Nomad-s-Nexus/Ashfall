#pragma once

#include "ClimateHeatConfig.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace RE
{
    class PlayerCharacter;
}

class ClimateHeatRuntime
{
public:
    void Reset() noexcept;
    void Invalidate() noexcept;
    [[nodiscard]] float Sample(RE::PlayerCharacter* player);
    [[nodiscard]] float LastHeat01() const noexcept { return _cachedHeat01; }

private:
    struct SourceProfile
    {
        std::uint32_t runtimeFormID{ 0 };
        float radius{ 0.0f };
        float strength01{ 0.0f };
    };

    void EnsureSourcesResolved();
    void EnsureEventRegistration();
    [[nodiscard]] std::optional<SourceProfile> ProfileFor(std::uint32_t runtimeFormID) const noexcept;

    ClimateHeatConfig _config;
    std::array<SourceProfile, 4> _sources{};
    std::size_t _sourceCount{ 0 };
    float _searchRadius{ 0.0f };
    std::uint32_t _lastCellID{ 0 };
    float _lastX{ 0.0f };
    float _lastY{ 0.0f };
    float _lastZ{ 0.0f };
    float _cachedHeat01{ 0.0f };
    std::atomic<bool> _dirty{ true };
    bool _sourcesResolved{ false };
    bool _eventsRegistered{ false };
    bool _sampleKnown{ false };
    bool _lastHeated{ false };
};
