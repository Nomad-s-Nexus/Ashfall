#include "RadEngine_Internal.h"
#include "RadCorrectnessPolicy.h"
#include <RE/P/PlayerCamera.h>   // RE::ShakeCamera (commonlib_NonVR camera-shake native)
#include <RE/N/NiControllerManager.h>
#include <RE/N/NiControllerSequence.h>
#include <RE/N/NiAVObject.h>
#include <RE/N/NiObjectNET.h>

using namespace RadInternal;



void RadiationEngine::EnsureQuestRunning(RE::TESQuest* quest)
{
    if (!quest) return;
    DumpQuestStageTable(quest);
    if ((quest->data.flags & static_cast<std::uint16_t>(RE::QuestFlag::kEnabled)) == 0)
    {
        quest->data.flags |= static_cast<std::uint16_t>(RE::QuestFlag::kEnabled);
        Log()->info("[StormFX] Quest 0x{:03X} was dormant (ESL-on-existing-save) -> enabled natively.",
            quest->GetFormID() & 0xFFF);
        DumpQuestStageTable(quest);
    }
}
void RadiationEngine::DumpQuestStageTable(RE::TESQuest* quest)
{
    static int dumps = 0;
    if (!quest || dumps >= 2) return;
    ++dumps;
    Log()->info("[StormFX/StageDiag] ---- dump #{} ({}) ----", dumps,
        dumps == 1 ? "initial" : "post-enable check");

    const auto& table = quest->stageTable;
    Log()->info(
        "[StormFX/StageDiag] quest={:p} flags=0x{:04X} stages.size={} stages.data={:p} "
        "table.ptr={:p} size={} mask=0x{:X} avail={} lastFree={}",
        static_cast<void*>(quest), static_cast<unsigned>(quest->data.flags), quest->stages.size(),
        static_cast<void*>(quest->stages.data()), static_cast<void*>(table.table), table.size,
        table.mask, table.avail, table.lastFree);

    for (std::uint32_t i = 0; i < quest->stages.size(); ++i)
        Log()->info("[StormFX/StageDiag] stages[{}]={:p}", i,
            static_cast<void*>(quest->stages[i]));

    if (table.table && table.size > 0 && table.size <= 256)
    {
        for (std::uint32_t i = 0; i < table.size; ++i)
        {
            const auto& e = table.table[i];
            if (e.index != 0xFFFF || e.next != 0xFFFF)
                Log()->info("[StormFX/StageDiag] table[{}] next={} index={}", i, e.next, e.index);
        }
    }
}
void RadiationEngine::TriggerStormFX(bool on)
{
    if (!_stormFXQuest) return;
    EnsureQuestRunning(_stormFXQuest);   // dormant on saves predating the quest; force-start
    if (!on || IsPlayerSheltered())
    {
        _stormFXQuest->SetStage(20);
        return;
    }
    int stage = 0;
    const char* desc = "ambient: weather effects only";
    if (_activeStormType >= 0 && _activeStormType < kStormTypeTableSize)
    {
        const StormType& st = kStormTypes[_activeStormType];
        if (st.themeId == 1)
        {
            _stormFXQuest->SetStage(20);
            return;
        }
        else if (st.lethal)
        {
            stage = 10;
            desc = "lethal: native shake";
        }
    }
    const bool ok = _stormFXQuest->SetStage(static_cast<std::uint16_t>(stage));
    Log()->debug("[StormFX] SetStage({}) result={} ({}) — informational; FX are native.",
        stage, ok, desc);
}
void RadiationEngine::UpdateStormFXVisibility()
{
    const bool exposed = !IsPlayerSheltered();
    if (exposed == _stormFXVisible) return;
    _stormFXVisible = exposed;
    if (exposed)
    {
        if (_stormPhase != StormPhase::Warning)
            TriggerStormFX(true);              // shake (Papyrus, lethal); nuke only if not yet fired
        const bool firestorm = _activeStormType >= 0 && _activeStormType < kStormTypeTableSize &&
            kStormTypes[_activeStormType].themeId == 1;
        const bool firestormHolding = firestorm &&
            (_stormPhase == StormPhase::Warning || _firestormAwaitingNuke || _stormRollInTicks > 0);
        if (firestormHolding)
            FadeOutStormIMAD(1.0f);
        else if (_stormPhase == StormPhase::Warning)
            SetStormIMADTarget(_activeStormType, 0.35f, 3.0f);
        else
            ApplyStormIMAD(_activeStormType);
        if (_stormPhase == StormPhase::Active && _activeStormType >= 0 &&
            _activeStormType < kStormTypeTableSize && kStormTypes[_activeStormType].themeId == 1)
            SpawnFireRing();
    }
    else
    {
        TriggerStormFX(false);                 // stop shake
        FadeOutStormIMAD(1.0f);
    }
}
void RadiationEngine::ApplyStormExposure()
{
    if (_activeStormType < 0 || _activeStormType >= kStormTypeTableSize) return;
    const StormType& st = kStormTypes[_activeStormType];

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* av = RE::ActorValue::GetSingleton();
    if (!player || !av) return;

    const bool sheltered = IsPlayerSheltered();

    if (sheltered) return;

    if (IsPlayerInPowerArmor()) return;

    float resist = av->radExposureResistance ? player->GetActorValue(*av->radExposureResistance) : 0.0f;
    if (resist < 0.0f) resist = 0.0f;
    const float gearFactor = kStormResistHalf / (kStormResistHalf + resist);
    const float radsMul = gearFactor, hpMul = gearFactor;

    if (av->rads && st.radsPerTick > 0.0f)
        player->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *av->rads, st.radsPerTick * radsMul);
    if (av->health && st.healthPerTick > 0.0f)
        player->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *av->health, -st.healthPerTick * hpMul);
}
bool RadiationEngine::IsGhoulActor(RE::Actor* a) const
{
    return a && _ghoulKeyword && a->race && a->race->HasKeyword(_ghoulKeyword);
}
void RadiationEngine::ApplyStormPeakToNPCs()
{
    auto* pl = RE::ProcessLists::GetSingleton();
    auto* av = RE::ActorValue::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* playerCell = player ? player->GetParentCell() : nullptr;
    if (!pl || !av || !playerCell) return;

    auto logger = Log();
    int healed = 0;
    for (auto& handle : pl->highActorHandles)
    {
        auto actorPtr = handle.get();
        RE::Actor* actor = actorPtr.get();
        if (!actor || actor->IsDead(false) || actor->IsPlayerRef()) continue;
        if (actor->GetParentCell() != playerCell) continue;

        if (IsGhoulActor(actor))
        {
            if (av->health)
                actor->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *av->health, kStormGhoulHeal);
            if (av->rads)   // ghouls never carry rads
            {
                const float r = actor->GetActorValue(*av->rads);
                if (r > 0.0f)
                    actor->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *av->rads, -r);
            }
            ++healed;
        }
    }
    logger->info("[Storm] Peak pulse — healed {} ghoul NPC(s); all other NPCs protected.", healed);
}
void RadiationEngine::MakeNearbyNPCsFlee()
{
    auto* pl     = RE::ProcessLists::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* dh     = RE::TESDataHandler::GetSingleton();
    auto* playerCell = player ? player->GetParentCell() : nullptr;
    if (!pl || !player || !dh || !playerCell || playerCell->IsInterior()) return;

    auto* humanForm = dh->LookupForm(0x0002CB72, "Fallout4.esm");
    auto* humanKeyword = humanForm && humanForm->GetFormType() == RE::ENUM_FORM_ID::kKYWD
        ? static_cast<RE::BGSKeyword*>(humanForm) : nullptr;
    if (!humanKeyword) return;

    int fled = 0;
    for (auto& handle : pl->highActorHandles)
    {
        auto actorPtr = handle.get();
        RE::Actor* actor = actorPtr.get();
        if (!actor || actor->IsDead(false) || actor->IsPlayerRef()) continue;
        if (IsGhoulActor(actor)) continue;                    // ghouls revel in the storm — no flee

        auto* cell = actor->GetParentCell();
        if (cell != playerCell) continue;
        auto* base = actor->GetObjectReference();
        if (!base || base->GetFormType() != RE::ENUM_FORM_ID::kNPC_) continue;
        auto* race = static_cast<RE::TESNPC*>(base)->GetFormRace();
        if (!race || !race->HasKeyword(humanKeyword)) continue;

        actor->InitiateFlee(player, true, true, false, cell, player, kFleeFromDist, kFleeToDist);
        ++fled;
    }
    if (fled > 0)
        Log()->info("[Storm] {} NPC(s) panicked and fled the storm.", fled);
}
