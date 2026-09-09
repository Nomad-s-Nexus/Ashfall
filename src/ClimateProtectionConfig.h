#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace RE
{
    class BGSKeyword;
    class TESObjectARMO;
}

class ClimateProtectionConfig
{
public:
    bool Load(const std::string& path);
    void Reset() noexcept;

    [[nodiscard]] std::optional<float> ArmorOverride(const RE::TESObjectARMO* armor) const noexcept;
    [[nodiscard]] std::size_t ExactOverrideCount() const noexcept { return _armorOverrides.size(); }
    [[nodiscard]] std::size_t KeywordOverrideCount() const noexcept { return _keywordOverrides.size(); }

private:
    struct KeywordOverride
    {
        RE::BGSKeyword* keyword{ nullptr };
        float insulation01{ 0.0f };
    };

    std::unordered_map<std::uint32_t, float> _armorOverrides;
    std::vector<KeywordOverride> _keywordOverrides;
};
