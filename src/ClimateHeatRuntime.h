#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace RE
{
    class PlayerCharacter;
}

class ClimateHeatRuntime
{
public:
    void Reset() noexcept;
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
    [[nodiscard]] const SourceProfile* ProfileFor(std::uint32_t runtimeFormID) const noexcept;

    std::array<SourceProfile, 4> _sources{};
    std::size_t _sourceCount{ 0 };
    std::uint32_t _lastCellID{ 0 };
    float _lastX{ 0.0f };
    float _lastY{ 0.0f };
    float _lastZ{ 0.0f };
    float _cachedHeat01{ 0.0f };
    bool _sourcesResolved{ false };
    bool _sampleKnown{ false };
    bool _lastHeated{ false };
};
