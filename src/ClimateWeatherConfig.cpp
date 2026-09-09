#include "pch.h"
#include "ClimateWeatherConfig.h"

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

bool ClimateWeatherConfig::Load(const std::string& path)
{
    Reset();
    auto logger = spdlog::get("FalloutAnomaly");

    std::ifstream file(path);
    if (!file.is_open())
    {
        if (logger)
            logger->info("[Climate] No weather override file at '{}'; weather flags remain authoritative.", path);
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
                logger->error("[Climate] climate_weather.json requires object version 1; ignoring overrides.");
            return false;
        }

        const auto& entries = root.contains("weatherOverrides") ? root["weatherOverrides"] : json::array();
        if (!entries.is_array())
        {
            if (logger)
                logger->error("[Climate] weatherOverrides must be an array; ignoring overrides.");
            return false;
        }

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler)
        {
            if (logger)
                logger->error("[Climate] TESDataHandler unavailable while resolving weather overrides.");
            return false;
        }

        std::size_t rejected = 0;
        for (const auto& entry : entries)
        {
            if (!entry.is_object() || !entry.contains("plugin") || !entry["plugin"].is_string() ||
                !entry.contains("form"))
            {
                ++rejected;
                continue;
            }

            const auto plugin = entry["plugin"].get<std::string>();
            const auto localFormID = ParseLocalFormID(entry["form"]);
            if (plugin.empty() || !localFormID)
            {
                ++rejected;
                continue;
            }

            Profile profile{};
            bool hasField = false;

            if (entry.contains("temperatureOffsetC"))
            {
                if (!entry["temperatureOffsetC"].is_number())
                {
                    ++rejected;
                    continue;
                }
                const double raw = entry["temperatureOffsetC"].get<double>();
                if (!std::isfinite(raw) || raw < -50.0 || raw > 50.0)
                {
                    ++rejected;
                    continue;
                }
                profile.temperatureOffsetC = static_cast<float>(raw);
                hasField = true;
            }

            if (entry.contains("precipitation"))
            {
                if (!entry["precipitation"].is_number())
                {
                    ++rejected;
                    continue;
                }
                const double raw = entry["precipitation"].get<double>();
                if (!std::isfinite(raw) || raw < 0.0 || raw > 100.0)
                {
                    ++rejected;
                    continue;
                }
                profile.precipitation01 = static_cast<float>(raw / 100.0);
                hasField = true;
            }

            if (!hasField)
            {
                ++rejected;
                continue;
            }

            auto* form = dataHandler->LookupForm(*localFormID, plugin);
            if (!form || form->GetFormType() != RE::ENUM_FORM_ID::kWTHR)
            {
                ++rejected;
                continue;
            }

            _overrides[form->GetFormID()] = profile;
        }

        if (logger)
            logger->info("[Climate] Weather overrides resolved: {}, rejected: {}.", _overrides.size(), rejected);
        return true;
    }
    catch (const std::exception& ex)
    {
        Reset();
        if (logger)
            logger->error("[Climate] Failed to parse weather overrides: {}", ex.what());
        return false;
    }
}

void ClimateWeatherConfig::Reset() noexcept
{
    _overrides.clear();
}

std::optional<ClimateWeatherConfig::Profile> ClimateWeatherConfig::Override(std::uint32_t runtimeFormID) const noexcept
{
    const auto it = _overrides.find(runtimeFormID);
    if (it == _overrides.end())
        return std::nullopt;
    return it->second;
}
