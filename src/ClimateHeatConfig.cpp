#include "pch.h"
#include "ClimateHeatConfig.h"

#include <RE/T/TESDataHandler.h>
#include <RE/T/TESForm.h>

#include <charconv>
#include <limits>

namespace
{
    std::optional<std::uint32_t> ParseLocalFormID(const json& value)
    {
        if (value.is_number_unsigned())
        {
            const auto raw = value.get<std::uint64_t>();
            if (raw <= std::numeric_limits<std::uint32_t>::max())
                return static_cast<std::uint32_t>(raw);
            return std::nullopt;
        }

        if (value.is_number_integer())
        {
            const auto raw = value.get<std::int64_t>();
            if (raw >= 0 && static_cast<std::uint64_t>(raw) <= std::numeric_limits<std::uint32_t>::max())
                return static_cast<std::uint32_t>(raw);
            return std::nullopt;
        }

        if (!value.is_string())
            return std::nullopt;

        std::string text = value.get<std::string>();
        int base = 10;
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        {
            text.erase(0, 2);
            base = 16;
        }
        if (text.empty())
            return std::nullopt;

        std::uint32_t result = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result, base);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
            return std::nullopt;
        return result;
    }
}

bool ClimateHeatConfig::Load(const std::string& path)
{
    Reset();
    auto logger = spdlog::get("FalloutAnomaly");

    std::ifstream file(path);
    if (!file.is_open())
    {
        if (logger)
            logger->info("[Climate] No heat override file at '{}'; built-in heat sources remain active.", path);
        return true;
    }

    try
    {
        json root;
        file >> root;
        if (!root.is_object() || !root.contains("version") || !root["version"].is_number_integer() ||
            root["version"].get<int>() != 1)
        {
            if (logger)
                logger->error("[Climate] climate_heat.json requires object version 1; ignoring overrides.");
            return false;
        }

        const auto& sources = root.contains("heatSources") ? root["heatSources"] : json::array();
        if (!sources.is_array())
        {
            if (logger)
                logger->error("[Climate] heatSources must be an array; ignoring overrides.");
            return false;
        }

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler)
        {
            if (logger)
                logger->error("[Climate] TESDataHandler unavailable while resolving heat overrides.");
            return false;
        }

        std::size_t rejected = 0;
        for (const auto& entry : sources)
        {
            if (!entry.is_object() || !entry.contains("plugin") || !entry["plugin"].is_string() ||
                !entry.contains("form") || !entry.contains("radius") || !entry["radius"].is_number() ||
                !entry.contains("strength") || !entry["strength"].is_number())
            {
                ++rejected;
                continue;
            }

            const auto plugin = entry["plugin"].get<std::string>();
            const auto localFormID = ParseLocalFormID(entry["form"]);
            const double rawRadius = entry["radius"].get<double>();
            const double rawStrength = entry["strength"].get<double>();
            if (plugin.empty() || !localFormID || !std::isfinite(rawRadius) || !std::isfinite(rawStrength) ||
                rawRadius <= 0.0 || rawRadius > 10000.0 || rawStrength < 0.0 || rawStrength > 100.0)
            {
                ++rejected;
                continue;
            }

            auto* form = dataHandler->LookupForm(*localFormID, plugin);
            if (!form)
            {
                ++rejected;
                continue;
            }

            const Profile profile{
                static_cast<float>(rawRadius),
                static_cast<float>(rawStrength / 100.0)
            };
            _overrides[form->GetFormID()] = profile;
            _maxRadius = std::max(_maxRadius, profile.radius);
        }

        if (logger)
            logger->info("[Climate] Heat overrides resolved: {}, rejected: {}.", _overrides.size(), rejected);
        return true;
    }
    catch (const std::exception& ex)
    {
        Reset();
        if (logger)
            logger->error("[Climate] Failed to parse heat overrides: {}", ex.what());
        return false;
    }
}

void ClimateHeatConfig::Reset() noexcept
{
    _overrides.clear();
    _maxRadius = 0.0f;
}

std::optional<ClimateHeatConfig::Profile> ClimateHeatConfig::Override(
    std::uint32_t runtimeFormID) const noexcept
{
    const auto it = _overrides.find(runtimeFormID);
    if (it == _overrides.end())
        return std::nullopt;
    return it->second;
}
