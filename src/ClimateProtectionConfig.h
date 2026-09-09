#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

class ClimateProtectionConfig
{
public:
    bool Load(const std::string& path);
    void Reset() noexcept;

    [[nodiscard]] std::optional<float> ArmorOverride(std::uint32_t runtimeFormID) const noexcept;
    [[nodiscard]] std::size_t OverrideCount() const noexcept { return _armorOverrides.size(); }

private:
    std::unordered_map<std::uint32_t, float> _armorOverrides;
};
