#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <string_view>

namespace RadNeedsTuningPolicy
{
    struct Spec
    {
        std::string_view key;
        float            reference;
        float            min;
        float            max;
    };

    // Constraint: the index is the key RadSurvivalQuest.psc passes to Tuned(); reference mirrors the script defaults.
    inline constexpr std::array<Spec, 6> kSpecs{ {
        { "RateMultiplier", 1.0f, 0.0f, 5.0f },
        { "HourlyHunger", 0.05f, 0.0f, 0.5f },
        { "HourlyThirst", 0.10f, 0.0f, 0.5f },
        { "HourlySleepDebt", 0.025f, 0.0f, 0.5f },
        { "FoodScale", 0.20f, 0.0f, 1.0f },
        { "DrinkScale", 0.25f, 0.0f, 1.0f },
    } };

    inline constexpr std::size_t kCount = kSpecs.size();

    inline int FindKey(std::string_view a_key)
    {
        for (std::size_t i = 0; i < kCount; ++i)
            if (kSpecs[i].key == a_key)
                return static_cast<int>(i);
        return -1;
    }

    // Constraint: a non-finite value is rejected; a finite one is clamped into the spec range.
    inline bool Accept(std::size_t a_index, float a_value, float& a_out)
    {
        if (a_index >= kCount || !std::isfinite(a_value))
            return false;
        const auto& spec = kSpecs[a_index];
        a_out = a_value < spec.min ? spec.min : (a_value > spec.max ? spec.max : a_value);
        return true;
    }

    // Constraint: plain decimal only; trailing junk, empty text and exponent/inf/nan forms are rejected.
    inline bool ParseValue(std::string_view a_text, float& a_out)
    {
        while (!a_text.empty() && (a_text.front() == ' ' || a_text.front() == '\t'))
            a_text.remove_prefix(1);
        while (!a_text.empty() && (a_text.back() == ' ' || a_text.back() == '\t'))
            a_text.remove_suffix(1);
        if (a_text.empty())
            return false;
        for (const char c : a_text)
            if (!((c >= '0' && c <= '9') || c == '.' || c == '-'))
                return false;
        const std::string copy(a_text);
        char*             end = nullptr;
        const float       value = std::strtof(copy.c_str(), &end);
        if (!end || end == copy.c_str() || *end != '\0')
            return false;
        a_out = value;
        return true;
    }
}
