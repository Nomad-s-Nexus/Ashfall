#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

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

private:
    std::unordered_map<std::uint32_t, Profile> _overrides;
};
