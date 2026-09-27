#include "pch.h"
#include "NeedsRuntime.h"

#include "Hooks.h"
#include "RadHud.h"
#include <RE/C/Calendar.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESGlobal.h>

namespace
{
    RadNeedsPolicy::HourlyRates LoadRates()
    {
        try
        {
            std::ifstream file("Data/F4SE/Plugins/FalloutAnomaly/needs.json");
            json config;
            file >> config;
            if (!config.at("version").is_number_integer() || config.at("version") != 1)
                throw std::runtime_error("expected needs.json version 1");
            const auto rate = [&config](const char* key) {
                const auto& value = config.at("hourlyRates").at(key);
                if (!value.is_number()) throw std::runtime_error("needs rate must be numeric");
                const double number = value.get<double>();
                if (!std::isfinite(number) || number < 0.0 || number > 1.0)
                    throw std::runtime_error("needs rate must be between zero and one");
                return static_cast<float>(number);
            };
            return { rate("hunger"), rate("thirst"), rate("sleepDebt") };
        }
        catch (const std::exception& ex)
        {
            if (auto logger = spdlog::get("FalloutAnomaly"))
                logger->warn("[Needs] Observation progression disabled: {}", ex.what());
            return {};
        }
    }
}

void NeedsRuntime::Update()
{
    if (!Hooks::IsGameMainThread()) return;
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !player->GetParentCell()) return;
    auto* calendar = RE::Calendar::GetSingleton();
    const double days = calendar && calendar->gameDaysPassed
        ? calendar->gameDaysPassed->GetValue() : -1.0;

    RadNeedsPolicy::NeedsState state;
    {
        std::lock_guard lock(_mutex);
        if (!_configLoaded)
        {
            _rates = LoadRates();
            _configLoaded = true;
        }
        _clock.Observe(days, _rates);
        state = _clock.state;
    }
    RadHud::GetSingleton().PushNeeds(state);
}

void NeedsRuntime::Reset()
{
    std::lock_guard lock(_mutex);
    _clock = {};
}

RadNeedsSerialization::PersistedState NeedsRuntime::SnapshotForSave() const
{
    std::lock_guard lock(_mutex);
    return RadNeedsSerialization::Snapshot(_clock.state);
}

void NeedsRuntime::RestoreFromSave(const RadNeedsPolicy::NeedsState& state)
{
    std::lock_guard lock(_mutex);
    _clock = { RadNeedsPolicy::Sanitize(state) };
}
