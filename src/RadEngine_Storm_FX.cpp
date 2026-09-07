#include "RadEngine_Internal.h"
#include "RadCorrectnessPolicy.h"
#include <RE/P/PlayerCamera.h>   // RE::ShakeCamera (commonlib_NonVR camera-shake native)
#include <RE/N/NiControllerManager.h>
#include <RE/N/NiControllerSequence.h>
#include <RE/N/NiAVObject.h>
#include <RE/N/NiObjectNET.h>

using namespace RadInternal;

// Storm lifecycle: scheduling, phases, FX (fire ring / ghouls / IMAD / quest stages), NPC effects, exposure.
// Split out of RadiationEngine.cpp (was ~4,300 lines). Shared constants/helpers live in
// RadEngine_Internal.h; the class itself is declared in RadiationEngine.h.


void RadiationEngine::EnsureQuestRunning(RE::TESQuest* quest)
{
    if (!quest) return;
    DumpQuestStageTable(quest);
    // TESQuest::SetStage (decompiled @ RVA 0x5D71E0) returns FALSE and does nothing unless the quest's
    // runtime ENABLED bit is set: `if ((quest+0xF4 & 1) != 0)`, i.e. QUEST_DATA.flags bit 0. A
    // StartGameEnabled quest added to an existing save via an ESL stays DORMANT (that record flag only
    // auto-fires on a NEW GAME), so the bit is clear, so every SetStage(10/11/12/20) silently failed —
    // the dome / camera shake / firestorm nuke never ran (log: "SetStage(11) result=false").
    //
    // Set the enabled bit NATIVELY. This is a plain memory write (safe on the main-thread Update tick)
    // — NOT Papyrus Quest.Start(), which is a latent native that CTDs when dispatched from here.
    if ((quest->data.flags & static_cast<std::uint16_t>(RE::QuestFlag::kEnabled)) == 0)
    {
        quest->data.flags |= static_cast<std::uint16_t>(RE::QuestFlag::kEnabled);
        Log()->info("[StormFX] Quest 0x{:03X} was dormant (ESL-on-existing-save) -> enabled natively.",
            quest->GetFormID() & 0xFFF);
    }

    // NOTE: an older build here also rebuilt the quest's SetStage lookup table via a raw OG-only module
    // offset for TESQuest::InitSearchTables (0x5E07D0). That has been REMOVED. SetStage is no longer used
    // to drive any FX — the dome, camera shake, and firestorm nuke are all native now, and TriggerStormFX
    // treats SetStage's result as informational only — so rebuilding its scatter table served no purpose
    // (and, per the earlier logs, SetStage returned false even with the rebuild). No private RVA remains.
    static bool questTablesInit = false;   // session latch (EnsureQuestRunning is a static member)
    if (!questTablesInit)
    {
        questTablesInit = true;
        DumpQuestStageTable(quest);   // diagnostic dump of the stage table as the engine built it

        // STORM DOME: the RadStormFX script ships with `Bool Property EnableCloudDome = false`, so
        // _SpawnDome() has always bailed out immediately ("cloud dome disabled (using weather fog
        // instead)"). Flip it ON at runtime.
        //
        // Deliberately NOT done by editing the ESP: RadiationOverhaul.esp's raw TES4 master order is
        // [0] DLCCoast, [1] DLCNukaWorld, [2] Fallout4.esm — Fallout4.esm is NOT index 0. The plugin's
        // FormIDs are built against that mapping, so it is self-consistent, but a save_plugin that
        // re-sorts masters to load order would remap every Fallout4.esm reference and silently corrupt
        // the plugin (hang on load, no crash log). Not worth it for one boolean.
        //
        // This is a script PROPERTY read/write on the bound script object under attachedScriptsLock —
        // a locked data access, NOT a VM dispatch, so it is safe on the main-thread Update tick
        // (only DispatchMethodCall/latent natives would be unsafe here).
        if (auto* gameVM = RE::GameVM::GetSingleton())
        {
            if (auto* vm = gameVM->GetVM().get())
            {
                auto* concreteVM = static_cast<RE::BSScript::Internal::VirtualMachine*>(vm);
                RE::BSAutoLock lock(concreteVM->attachedScriptsLock);
                const auto handle = vm->GetObjectHandlePolicy().GetHandleForObject(
                    static_cast<std::uint32_t>(quest->GetFormType()), quest);
                if (const auto it = concreteVM->attachedScripts.find(handle);
                    it != concreteVM->attachedScripts.end())
                {
                    for (auto& attached : it->second)
                    {
                        auto* obj = attached.get();
                        if (!obj) continue;
                        if (auto* prop = obj->GetProperty(RE::BSFixedString("EnableCloudDome")))
                        {
                            *prop = true;
                            Log()->info("[StormFX] Storm dome ENABLED (RadStormFX.EnableCloudDome=true) — "
                                        "KORs CloudArena layers will spawn with the storm.");
                        }
                    }
                }
                else
                {
                    Log()->warn("[StormFX] Quest script not bound yet — could not enable the storm dome.");
                }
            }
        }
    }
}
void RadiationEngine::DumpQuestStageTable(RE::TESQuest* quest)
{
    // Called twice: once BEFORE InitSearchTables (expected: table empty -> why SetStage returned false)
    // and once AFTER (expected: stages 0/10/11/12/20 present -> SetStage can now resolve them).
    static int dumps = 0;
    if (!quest || dumps >= 2) return;
    ++dumps;
    Log()->info("[StormFX/StageDiag] ---- dump #{} ({}) ----", dumps,
        dumps == 1 ? "BEFORE InitSearchTables" : "AFTER InitSearchTables");

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
    // Sheltered = NO screen FX. The dome is a mesh that would follow the player indoors and clip
    // through walls (green fog inside buildings, see bug report), so a storm shows nothing on-screen
    // while the player is sheltered — only the exterior, exposed player sees the dome/shake/nuke.
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
        // FIRESTORM nuke — now NATIVE, and driven per-tick from the Active handler (not fired here).
        // It used to be fired inline at this exposure change and latched _stormNukeFired=true *before*
        // knowing whether it detonated, so a transient streaming failure (cell not loaded yet) silently
        // consumed the storm's one shot. The Active-phase tick now calls SpawnDistantNuke() with a
        // bounded retry and only latches _stormNukeFired on a successful detonation.
        if (st.themeId == 1)
        {
            stage = 11;
            desc = "firestorm: native nuke (fired on Active tick, bounded retry) + native shake";
        }
        else if (st.lethal)
        {
            stage = 10;
            desc = "ash: native shake + dome";
        }
    }
    // The stage call is retained ONLY so a user-supplied RadStormFX script can still react if the
    // quest is ever repaired. Every FX it used to drive (dome, shake, nuke) is native now, so a
    // false return here is expected and harmless — it is not a failure path.
    const bool ok = _stormFXQuest->SetStage(static_cast<std::uint16_t>(stage));
    Log()->debug("[StormFX] SetStage({}) result={} ({}) — informational; FX are native.",
        stage, ok, desc);
}
void RadiationEngine::UpdateStormFXVisibility()
{
    // Storm fog/dome + atmosphere IMAD are EXTERIOR-only, mirroring the audio (UpdateAmbientAudio).
    // The dome is a mesh anchored to the player, so without this it follows them indoors and clips
    // through walls; the IMAD is a full-screen post-process colour grade that ignores geometry, so
    // it tints interiors too. Hide both when the player is sheltered, restore them when exposed.
    // Edge-triggered (only on a change) so we don't re-issue the quest stage / IMAD every tick.
    const bool exposed = !IsPlayerSheltered();
    if (exposed == _stormFXVisible) return;
    _stormFXVisible = exposed;
    if (exposed)
    {
        if (_stormPhase != StormPhase::Warning)
            TriggerStormFX(true);              // shake (Papyrus, lethal); nuke only if not yet fired
        SpawnStormDome();                      // re-spawn the native dome on stepping back outside
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
        // Firestorm's ground fire ring also restores on re-exposure (every other FX does). Only during
        // Active — it's a peak effect, not a warning one — and only for the firestorm theme; SpawnFireRing
        // self-clears its old refs at entry, so re-calling is safe.
        if (_stormPhase == StormPhase::Active && _activeStormType >= 0 &&
            _activeStormType < kStormTypeTableSize && kStormTypes[_activeStormType].themeId == 1)
            SpawnFireRing();
    }
    else
    {
        TriggerStormFX(false);                 // stop shake
        ClearStormDome();                      // despawn the native dome indoors (it would clip walls)
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

    // Indoors = fully safe from storm exposure, for ALL storm types. Previously AMBIENT storms
    // still applied half damage when sheltered, which read as "storms hurt me even inside".
    if (sheltered) return;

    // A full Power Armor suit is a sealed shield: no storm rads, no storm HP loss. The block also
    // runs in Update's rad-refund path so this is the matching HP-side guard.
    if (IsPlayerInPowerArmor()) return;

    // Rad-gear protection: the direct ModActorValue path below bypasses the engine's rad-resist
    // mitigation, so apply it here. Storms now respect the player's radiation EXPOSURE resistance
    // (hazmat suit, gas masks, etc.) — a hazmat suit's high resist makes the player nearly immune,
    // gas masks get a meaningful reduction. (kStormResistHalf = damage halves at that resist value.)
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
    // Main-thread only: called from the 4Hz Update hook, which runs on the main thread, so the
    // ProcessLists handle iteration below is safe without a lock.
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

        // Ghoul NPCs are rad-immune and HEAL in radiation (lore-accurate), so the storm peak mends
        // them. Every other NPC is left completely untouched — storms must NEVER wipe wandering NPCs
        // such as Diamond City guards, caravans, or patrols, which were not flagged essential.
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
        // else: protected — the storm does no harm to any non-ghoul NPC.
    }
    logger->info("[Storm] Peak pulse — healed {} ghoul NPC(s); all other NPCs protected.", healed);
}
void RadiationEngine::MakeNearbyNPCsFlee()
{
    // Main-thread only (4Hz Update hook) — ProcessLists iteration without a lock is safe here.
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

        // Engine flee: panic and run from the storm-stricken area (referenced to the player, who is
        // the storm's moving epicenter). Best-effort — FO4 flee AI is approximate.
        actor->InitiateFlee(player, true, true, false, cell, player, kFleeFromDist, kFleeToDist);
        ++fled;
    }
    if (fled > 0)
        Log()->info("[Storm] {} NPC(s) panicked and fled the storm.", fled);
}
