#include "pch.h"
#include "ClimateProtectionConfig.h"

#include <RE/T/TESDataHandler.h>
#include <RE/T/TESForm.h>

#include <charconv>
#include <limits>

namespace
{
    std::optional<std::uint32_t> ParseLocalFormID(const json& value)
    {
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

bool ClimateProtectionConfig::Load(const std::string& path)
{
    Reset();
    auto logger = spdlog::get("FalloutAnomaly");

    std::ifstream file(path);
    if (!file.is_open())
    {
        if (logger)
            logger->info("[Climate] No protection override file at '{}'; automatic insulation remains authoritative.", path);
        return true;
    }

    try
    {
        json root;
        file >> root;
        if (!root.is_object() || root.value("version", 0) != 1)
        {
            if (logger)
                logger->error("[Climate] climate_protection.json requires object version 1; ignoring overrides.");
            return false;
        }

        const auto& overrides = root.contains("armorOverrides") ? root["armorOverrides"] : json::array();
        if (!overrides.is_array())
        {
            if (logger)
                logger->error("[Climate] armorOverrides must be an array; ignoring overrides.");
            return false;
        }

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler)
        {
            if (logger)
                logger->error("[Climate] TESDataHandler unavailable while resolving protection overrides.");
            return false;
        }

        std::size_t rejected = 0;
        for (const auto& entry : overrides)
        {
            if (!entry.is_object())
            {
                ++rejected;
                continue;
            }

            const auto plugin = entry.value("plugin", std::string{});
            const auto localFormID = entry.contains("form") ? ParseLocalFormID(entry["form"]) : std::nullopt;
            const float insulation = entry.value("insulation", -1.0f);
            if (plugin.empty() || !localFormID || !std::isfinite(insulation) || insulation < 0.0f || insulation > 100.0f)
            {
                ++rejected;
                continue;
            }

            auto* form = dataHandler->LookupForm(*localFormID, plugin);
            if (!form || form->GetFormType() != RE::ENUM_FORM_ID::kARMO)
            {
                ++rejected;
                continue;
            }

            _armorOverrides[form->GetFormID()] = insulation / 100.0f;
        }

        if (logger)
            logger->info("[Climate] Protection overrides resolved: {}, rejected: {}.", _armorOverrides.size(), rejected);
        return true;
    }
    catch (const std::exception& ex)
    {
        Reset();
        if (logger)
            logger->error("[Climate] Failed to parse protection overrides: {}", ex.what());
        return false;
    }
}

void ClimateProtectionConfig::Reset() noexcept
{
    _armorOverrides.clear();
}

std::optional<float> ClimateProtectionConfig::ArmorOverride(std::uint32_t runtimeFormID) const noexcept
{
    const auto it = _armorOverrides.find(runtimeFormID);
    if (it == _armorOverrides.end())
        return std::nullopt;
    return it->second;
}
