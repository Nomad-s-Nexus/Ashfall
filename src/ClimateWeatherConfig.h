#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

class ClimateWeatherConfig
{
public:
    struct Profile
    {
        float temperatureOffsetC{ 0.0f };
        std::optional<float> precipitation01;
    };

    bool Load(const std::string& path);
    void Reset() noexcept;

    [[nodiscard]] std::optional<Profile> Override(std::uint32_t runtimeFormID) const noexcept;
    [[nodiscard]] std::size_t OverrideCount() const noexcept { return _overrides.size(); }

    // Constraint: only a listed interior cell may be treated as open to the sky; an unresolved entry is never listed.
    [[nodiscard]] bool IsRooflessInterior(std::uint32_t runtimeFormID) const noexcept
    {
        return _rooflessInteriorCells.contains(runtimeFormID);
    }
    [[nodiscard]] std::size_t RooflessInteriorCount() const noexcept { return _rooflessInteriorCells.size(); }

private:
    std::unordered_map<std::uint32_t, Profile> _overrides;
    std::unordered_set<std::uint32_t>          _rooflessInteriorCells;
};
