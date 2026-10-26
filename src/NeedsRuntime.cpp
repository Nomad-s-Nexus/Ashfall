#include "pch.h"
#include "NeedsRuntime.h"

#include "Hooks.h"
#include "RadHud.h"
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESGlobal.h>

#include <cmath>
#include <cstdint>

namespace
{
    constexpr const char* kPlugin = "RadSurvival.esp";

    // Constraint: local form ids only; TESDataHandler applies the load-order prefix.
    constexpr std::uint32_t kHungerForm = 0x000801;
    constexpr std::uint32_t kThirstForm = 0x000802;
    constexpr std::uint32_t kSleepForm = 0x000803;
    constexpr std::uint32_t kRiskForm = 0x000822;
    constexpr std::uint32_t kDiseaseForm = 0x000823;

    // Constraint: non-template LookupForm; TESForm::As lives in TESFormUtil.h, absent from this TU.
    RE::TESGlobal* ResolveGlobal(RE::TESDataHandler* a_data, std::uint32_t a_formID)
    {
        RE::TESForm* form = a_data->LookupForm(a_formID, kPlugin);
        return (form && form->GetFormType() == RE::ENUM_FORM_ID::kGLOB)
            ? static_cast<RE::TESGlobal*>(form) : nullptr;
    }

    float ReadGlobal(void* a_global)
    {
        const auto* global = static_cast<const RE::TESGlobal*>(a_global);
        if (!global || !std::isfinite(global->value))
            return 0.0f;
        return global->value;
    }
}

void NeedsRuntime::ResolveGlobals()
{
    auto* data = RE::TESDataHandler::GetSingleton();
    if (!data)
        return;
    if (!_hunger) _hunger = ResolveGlobal(data, kHungerForm);
    if (!_thirst) _thirst = ResolveGlobal(data, kThirstForm);
    if (!_sleep) _sleep = ResolveGlobal(data, kSleepForm);
    if (!_risk) _risk = ResolveGlobal(data, kRiskForm);
    if (!_disease) _disease = ResolveGlobal(data, kDiseaseForm);
}

void NeedsRuntime::Update()
{
    if (!Hooks::IsGameMainThread())
        return;
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !player->GetParentCell())
        return;

    RadNeedsPolicy::NeedsSnapshot snapshot;
    {
        std::lock_guard lock(_mutex);
        ResolveGlobals();
        snapshot.pools.hunger01 = ReadGlobal(_hunger);
        snapshot.pools.thirst01 = ReadGlobal(_thirst);
        snapshot.pools.sleepDebt01 = ReadGlobal(_sleep);
        snapshot.risk01 = ReadGlobal(_risk);
        snapshot.diseasePacked = static_cast<int>(std::lround(ReadGlobal(_disease)));
        snapshot = RadNeedsPolicy::Sanitize(snapshot);
    }
    RadHud::GetSingleton().PushNeeds(snapshot);
}

void NeedsRuntime::Reset()
{
    // Constraint: a load re-resolves every global so a stale pointer can never outlive a save.
    std::lock_guard lock(_mutex);
    _hunger = nullptr;
    _thirst = nullptr;
    _sleep = nullptr;
    _risk = nullptr;
    _disease = nullptr;
}
