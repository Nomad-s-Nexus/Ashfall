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
        bool             toggle;
    };

    enum Key : std::size_t
    {
        kRateMultiplier, kHourlyHunger, kHourlyThirst, kHourlySleepDebt, kFoodScale, kDrinkScale,
        kSleepRecovery,
        kDiseasesEnabled, kDiseaseBaseRisk, kRiskRadWeather, kRiskRain, kRiskWound, kRiskSwim,
        kRiskTaintedFood, kRiskTaintedWater, kRiskContaminated,
        kPapyrusKeys,
        kFilterEnabled = kPapyrusKeys, kFilterDuration, kFilterChokeRads, kFilterAutoReplace,
        kConditionEnabled, kConditionDamage, kConditionAutoRepair, kAutoEquipMask, kAutoEquipInCombat,
        kChokingEnabled, kChokeHealthPerSec, kChokeFailedFactor, kChokeSounds,
        kAmbientEnabled, kAmbientVolume, kStormVolume,
        kStormsEnabled, kStormMinMinutes, kStormMaxMinutes,
        kTemperatureEnabled, kColdIntensity, kWetnessEnabled, kWetnessSoakRate, kWetnessDryRate,
        kAlwaysShowNeeds, kWaterRequireSneak,
        kKeyCount
    };

    inline constexpr std::array<Spec, kKeyCount> kSpecs{ {
        { "RateMultiplier", 1.0f, 0.0f, 5.0f, false },
        { "HourlyHunger", 0.05f, 0.0f, 0.5f, false },
        { "HourlyThirst", 0.10f, 0.0f, 0.5f, false },
        { "HourlySleepDebt", 0.025f, 0.0f, 0.5f, false },
        { "FoodScale", 0.20f, 0.0f, 1.0f, false },
        { "DrinkScale", 0.25f, 0.0f, 1.0f, false },
        { "SleepRecovery", 0.12f, 0.0f, 0.5f, false },
        { "DiseasesEnabled", 1.0f, 0.0f, 1.0f, true },
        { "DiseaseBaseRisk", 0.012f, 0.0f, 0.2f, false },
        { "RiskRadWeather", 0.08f, 0.0f, 1.0f, false },
        { "RiskRain", 0.02f, 0.0f, 1.0f, false },
        { "RiskWound", 0.01f, 0.0f, 0.5f, false },
        { "RiskSwim", 0.03f, 0.0f, 0.5f, false },
        { "RiskTaintedFood", 0.10f, 0.0f, 1.0f, false },
        { "RiskTaintedWater", 0.15f, 0.0f, 1.0f, false },
        { "RiskContaminated", 0.05f, 0.0f, 1.0f, false },
        { "FilterEnabled", 1.0f, 0.0f, 1.0f, true },
        { "FilterDuration", 1200.0f, 60.0f, 7200.0f, false },
        { "FilterChokeRads", 8.0f, 0.0f, 30.0f, false },
        { "FilterAutoReplace", 0.0f, 0.0f, 1.0f, true },
        { "ConditionEnabled", 1.0f, 0.0f, 1.0f, true },
        { "ConditionDamage", 6.0f, 0.0f, 50.0f, false },
        { "ConditionAutoRepair", 0.0f, 0.0f, 1.0f, true },
        { "AutoEquipMask", 1.0f, 0.0f, 1.0f, true },
        { "AutoEquipInCombat", 1.0f, 0.0f, 1.0f, true },
        { "ChokingEnabled", 1.0f, 0.0f, 1.0f, true },
        { "ChokeHealthPerSec", 1.5f, 0.0f, 10.0f, false },
        { "ChokeFailedFactor", 0.5f, 0.0f, 1.0f, false },
        { "ChokeSounds", 1.0f, 0.0f, 1.0f, true },
        { "AmbientEnabled", 1.0f, 0.0f, 1.0f, true },
        { "AmbientVolume", 45.0f, 0.0f, 100.0f, false },
        { "StormVolume", 85.0f, 0.0f, 100.0f, false },
        { "StormsEnabled", 1.0f, 0.0f, 1.0f, true },
        { "StormMinMinutes", 35.0f, 1.0f, 240.0f, false },
        { "StormMaxMinutes", 90.0f, 1.0f, 480.0f, false },
        { "TemperatureEnabled", 1.0f, 0.0f, 1.0f, true },
        { "ColdIntensity", 1.0f, 0.0f, 3.0f, false },
        { "WetnessEnabled", 1.0f, 0.0f, 1.0f, true },
        { "WetnessSoakRate", 1.0f, 0.0f, 3.0f, false },
        { "WetnessDryRate", 1.0f, 0.1f, 5.0f, false },
        { "AlwaysShowNeeds", 0.0f, 0.0f, 1.0f, true },
        { "WaterRequireSneak", 1.0f, 0.0f, 1.0f, true },
    } };

    inline constexpr std::size_t kCount = kSpecs.size();

    inline int FindKey(std::string_view a_key)
    {
        for (std::size_t i = 0; i < kCount; ++i)
            if (kSpecs[i].key == a_key)
                return static_cast<int>(i);
        return -1;
    }

    inline bool Accept(std::size_t a_index, float a_value, float& a_out)
    {
        if (a_index >= kCount || !std::isfinite(a_value))
            return false;
        const auto& spec = kSpecs[a_index];
        if (spec.toggle)
        {
            a_out = a_value >= 0.5f ? 1.0f : 0.0f;
            return true;
        }
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
