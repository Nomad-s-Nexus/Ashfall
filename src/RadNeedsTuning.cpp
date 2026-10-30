#include "pch.h"
#include "RadNeedsTuning.h"

#include "RadNeedsTuningPolicy.h"

#include <RE/B/BSScriptUtil.h>
#include <RE/B/BSScript_IVirtualMachine.h>
#include <RE/T/TESQuest.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>

#include <Windows.h>

namespace
{
    using namespace RadNeedsTuningPolicy;

    constexpr const char* kIni = ".\\Data\\F4SE\\Plugins\\FalloutAnomalyRadiation.ini";
    constexpr const char* kSection = "Tuning";
    constexpr float       kUnset = std::numeric_limits<float>::quiet_NaN();

    std::array<std::atomic<float>, kCount> g_values;
    std::array<std::atomic<float>, kCount> g_live;

    bool IsSet(float a_value)
    {
        return !std::isnan(a_value);
    }

    void WriteIni(std::size_t a_index, const char* a_text)
    {
        if (!::WritePrivateProfileStringA(kSection, std::string(kSpecs[a_index].key).c_str(), a_text, kIni))
        {
            if (auto logger = spdlog::get("FalloutAnomaly"))
                logger->warn("[Tuning] Could not write [Tuning] {} to the INI (error {}); the change is live but will not persist.",
                    kSpecs[a_index].key, ::GetLastError());
        }
    }

    void Store(std::size_t a_index, float a_value, bool a_persist)
    {
        g_values[a_index].store(a_value, std::memory_order_release);
        if (!a_persist)
            return;
        char text[32];
        std::snprintf(text, sizeof(text), "%.4f", a_value);
        WriteIni(a_index, text);
    }

    void LoadIni()
    {
        auto logger = spdlog::get("FalloutAnomaly");
        for (std::size_t i = 0; i < kCount; ++i)
        {
            const std::string key(kSpecs[i].key);
            char              buffer[64]{};
            ::GetPrivateProfileStringA(kSection, key.c_str(), "", buffer, sizeof(buffer), kIni);
            if (buffer[0] == '\0')
                continue;
            float parsed = 0.0f;
            float accepted = 0.0f;
            if (!ParseValue(buffer, parsed) || !Accept(i, parsed, accepted))
            {
                if (logger)
                    logger->warn("[Tuning] [Tuning] {}='{}' is not a usable number; the script default stays.", key, buffer);
                continue;
            }
            Store(i, accepted, false);
            if (logger)
                logger->info("[Tuning] {} = {:.4f} from the INI.", key, accepted);
        }
    }

    // Constraint: returns override + 1.0, or 0.0 when unset, so an unbound native (also 0.0) never reads as a zero rate.
    float PapyrusTuningOverride(RE::TESQuest&, std::int32_t a_key)
    {
        if (a_key < 0 || static_cast<std::size_t>(a_key) >= kPapyrusKeys)
            return 0.0f;
        const float value = g_values[static_cast<std::size_t>(a_key)].load(std::memory_order_acquire);
        return IsSet(value) ? value + 1.0f : 0.0f;
    }

    bool F4SEAPI RegisterPapyrusFunctions(RE::BSScript::IVirtualMachine* a_vm)
    {
        if (!a_vm)
            return false;
        a_vm->BindNativeMethod("RadSurvivalQuest", "TuningOverride", PapyrusTuningOverride, std::nullopt, false);
        a_vm->BindNativeMethod("RadSurvivalDiseases", "TuningOverride", PapyrusTuningOverride, std::nullopt, false);
        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->info("[Tuning] Papyrus bridge registered (TuningOverride on the quest and the diseases script).");
        return true;
    }
}

void RadNeedsTuning::Install()
{
    for (auto& slot : g_values)
        slot.store(kUnset, std::memory_order_relaxed);
    for (auto& slot : g_live)
        slot.store(kUnset, std::memory_order_relaxed);
    LoadIni();

    auto* papyrus = F4SE::GetPapyrusInterface();
    if (!papyrus || !papyrus->Register(RegisterPapyrusFunctions))
    {
        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->warn("[Tuning] Papyrus bridge unavailable or registration failed; survival rates stay at the script defaults.");
    }
}

bool RadNeedsTuning::Handle(std::string_view a_command)
{
    auto logger = spdlog::get("FalloutAnomaly");
    if (a_command == "reset")
    {
        for (std::size_t i = 0; i < kCount; ++i)
        {
            g_values[i].store(kUnset, std::memory_order_release);
            WriteIni(i, nullptr);
        }
        if (logger)
            logger->info("[Tuning] All survival overrides cleared; script defaults restored.");
        return true;
    }

    const auto split = a_command.find(':');
    if (split == std::string_view::npos)
        return false;
    const int index = FindKey(a_command.substr(0, split));
    float     parsed = 0.0f;
    float     accepted = 0.0f;
    if (index < 0 || !ParseValue(a_command.substr(split + 1), parsed) || !Accept(static_cast<std::size_t>(index), parsed, accepted))
    {
        if (logger)
            logger->warn("[Tuning] Rejected command '{}'.", std::string(a_command));
        return false;
    }
    Store(static_cast<std::size_t>(index), accepted, true);
    if (logger)
        logger->info("[Tuning] {} = {:.4f}.", kSpecs[static_cast<std::size_t>(index)].key, accepted);
    return true;
}

std::string RadNeedsTuning::Json()
{
    std::string json = "{";
    for (std::size_t i = 0; i < kCount; ++i)
    {
        const float value = g_values[i].load(std::memory_order_acquire);
        const float live = g_live[i].load(std::memory_order_acquire);
        const float shown = IsSet(value) ? value : (IsSet(live) ? live : kSpecs[i].reference);
        char        entry[192];
        std::snprintf(entry, sizeof(entry), "%s\"%.*s\":{\"v\":%.4f,\"o\":%s,\"min\":%.4f,\"max\":%.4f,\"t\":%s}", i ? "," : "",
            static_cast<int>(kSpecs[i].key.size()), kSpecs[i].key.data(),
            shown, IsSet(value) ? "true" : "false",
            kSpecs[i].min, kSpecs[i].max, kSpecs[i].toggle ? "true" : "false");
        json += entry;
    }
    json += "}";
    return json;
}

void RadNeedsTuning::SetLive(RadNeedsTuningPolicy::Key a_key, float a_value)
{
    if (a_key < kCount)
        g_live[a_key].store(a_value, std::memory_order_release);
}

float RadNeedsTuning::Value(RadNeedsTuningPolicy::Key a_key, float a_fallback)
{
    if (a_key >= kCount)
        return a_fallback;
    const float value = g_values[a_key].load(std::memory_order_acquire);
    return IsSet(value) ? value : a_fallback;
}
