#include "pch.h"
#include "ClimateProtectionConfig.h"

#include <RE/B/BGSKeyword.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESObjectARMO.h>

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

    std::optional<float> ParseInsulation(const json& entry)
    {
        if (!entry.contains("insulation") || !entry["insulation"].is_number())
            return std::nullopt;
        const double raw = entry["insulation"].get<double>();
        if (!std::isfinite(raw) || raw < 0.0 || raw > 100.0)
            return std::nullopt;
        return static_cast<float>(raw / 100.0);
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
        if (!root.is_object() || !root.contains("version") || !root["version"].is_number_integer() ||
            root["version"].get<int>() != 1)
        {
            if (logger)
                logger->error("[Climate] climate_protection.json requires object version 1; ignoring overrides.");
            return false;
        }

        const auto& exactOverrides = root.contains("armorOverrides") ? root["armorOverrides"] : json::array();
        const auto& keywordOverrides = root.contains("keywordOverrides") ? root["keywordOverrides"] : json::array();
        if (!exactOverrides.is_array() || !keywordOverrides.is_array())
        {
            if (logger)
                logger->error("[Climate] armorOverrides and keywordOverrides must be arrays; ignoring overrides.");
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
        for (const auto& entry : exactOverrides)
        {
            if (!entry.is_object() || !entry.contains("plugin") || !entry["plugin"].is_string() ||
                !entry.contains("form"))
            {
                ++rejected;
                continue;
            }

            const auto plugin = entry["plugin"].get<std::string>();
            const auto localFormID = ParseLocalFormID(entry["form"]);
            const auto insulation = ParseInsulation(entry);
            if (plugin.empty() || !localFormID || !insulation)
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

            _armorOverrides[form->GetFormID()] = *insulation;
        }

        for (const auto& entry : keywordOverrides)
        {
            if (!entry.is_object() || !entry.contains("plugin") || !entry["plugin"].is_string() ||
                !entry.contains("form"))
            {
                ++rejected;
                continue;
            }

            const auto plugin = entry["plugin"].get<std::string>();
            const auto localFormID = ParseLocalFormID(entry["form"]);
            const auto insulation = ParseInsulation(entry);
            if (plugin.empty() || !localFormID || !insulation)
            {
                ++rejected;
                continue;
            }

            auto* form = dataHandler->LookupForm(*localFormID, plugin);
            if (!form || form->GetFormType() != RE::ENUM_FORM_ID::kKYWD)
            {
                ++rejected;
                continue;
            }

            _keywordOverrides.push_back({ static_cast<RE::BGSKeyword*>(form), *insulation });
        }

        if (logger)
            logger->info("[Climate] Protection overrides resolved: {} exact, {} keyword, rejected: {}.",
                _armorOverrides.size(), _keywordOverrides.size(), rejected);
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
    _keywordOverrides.clear();
}

std::optional<float> ClimateProtectionConfig::ArmorOverride(const RE::TESObjectARMO* armor) const noexcept
{
    if (!armor)
        return std::nullopt;

    if (const auto it = _armorOverrides.find(armor->GetFormID()); it != _armorOverrides.end())
        return it->second;

    for (const auto& rule : _keywordOverrides)
    {
        if (rule.keyword && armor->HasKeyword(rule.keyword))
            return rule.insulation01;
    }
    return std::nullopt;
}
