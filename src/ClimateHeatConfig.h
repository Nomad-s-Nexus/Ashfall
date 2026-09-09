#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

class ClimateHeatConfig
{
public:
    struct Profile
    {
        float radius{ 0.0f };
        float strength01{ 0.0f };
    };

    bool Load(const std::string& path);
    void Reset() noexcept;

    [[nodiscard]] std::optional<Profile> Override(std::uint32_t runtimeFormID) const noexcept;
    [[nodiscard]] float MaxRadius() const noexcept { return _maxRadius; }
    [[nodiscard]] std::size_t OverrideCount() const noexcept { return _overrides.size(); }

private:
    std::unordered_map<std::uint32_t, Profile> _overrides;
    float _maxRadius{ 0.0f };
};
