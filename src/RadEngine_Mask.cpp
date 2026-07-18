#include "RadEngine_Internal.h"

using namespace RadInternal;

// Gas mask: worn detection, auto-equip (player + NPCs), filter drain/replace, condition/repair, put-on fade.
// Split out of RadiationEngine.cpp (was ~4,300 lines). Shared constants/helpers live in
// RadEngine_Internal.h; the class itself is declared in RadiationEngine.h.


// Gas-mask detection for the first-person mask SCREEN OVERLAY (drawn by the PrismaUI HUD). A worn
// gas mask / respirator fills the MOUTH biped slot (19) — distinct from sunglasses (eyes only) or a
// hat — so a single worn-armor scan for that slot reliably flags "the player is breathing through a
// mask". Reuses the same biped scan as IsPlayerInPowerArmor. Const: only reads worn gear.
RE::TESObjectARMO* RadiationEngine::GetActorWornMask(RE::Actor* actor) const
{
    if (!actor || !actor->biped) return nullptr;

    auto& biped = *actor->biped;
    for (std::size_t i = 0; i < std::size(biped.object); ++i)
    {
        RE::TESForm* form = biped.object[i].parent.object;
        if (!form || form->GetFormType() != RE::ENUM_FORM_ID::kARMO) continue;
        auto* armo = static_cast<RE::TESObjectARMO*>(form);
        if (armo->FillsBipedSlot(static_cast<std::int32_t>(RE::BIPED_OBJECT::kMouth)))
            return armo;
    }
    return nullptr;
}


RE::TESObjectARMO* RadiationEngine::GetWornMask() const
{
    return GetActorWornMask(RE::PlayerCharacter::GetSingleton());
}


bool RadiationEngine::IsWearingGasMask() const { return GetWornMask() != nullptr; }


// First owned gas mask (a mouth-slot armor) in an actor's inventory, worn or not — used to auto-don a
// mask in rad weather. Mask-agnostic: matches any armor filling the mouth slot, no FormID list.
RE::TESObjectARMO* RadiationEngine::FindActorInventoryMask(RE::Actor* actor) const
{
    if (!actor) return nullptr;
    auto* inv = actor->inventoryList;
    if (!inv) return nullptr;

    RE::TESObjectARMO* found = nullptr;
    inv->ForEachStack(
        [](RE::BGSInventoryItem& a_item) -> bool {
            auto* obj = a_item.object;
            if (!obj || obj->GetFormType() != RE::ENUM_FORM_ID::kARMO) return false;
            auto* armo = static_cast<RE::TESObjectARMO*>(obj);
            return armo->FillsBipedSlot(static_cast<std::int32_t>(RE::BIPED_OBJECT::kMouth));
        },
        [&found](RE::BGSInventoryItem& a_item, RE::BGSInventoryItem::Stack&) -> bool {
            found = static_cast<RE::TESObjectARMO*>(a_item.object);
            return false;  // got one — stop
        });
    return found;
}


RE::TESObjectARMO* RadiationEngine::FindInventoryMask() const
{
    return FindActorInventoryMask(RE::PlayerCharacter::GetSingleton());
}


// Silent equip/unequip of a mask on an actor via the script VM (Actor.EquipItem / UnequipItem).
// Silent so it carries no "equipped X" message; the player's put-on fade is driven separately.
void RadiationEngine::SetActorMaskEquipped(RE::Actor* actor, RE::TESObjectARMO* mask, bool equip)
{
    if (!mask || !actor) return;
    // NATIVE equip (no Papyrus). This is reached from the 4Hz Update hook (auto-equip on rad-weather
    // entry, NPC masking), which runs on a worker thread — and dispatching Papyrus from there CTDs
    // (see CallFadeOutGame). ActorEquipManager is the engine's own equip path, the same one AI actor
    // updates use, so it is safe to call on that thread. Silent (playSounds=false): the mask draw/
    // withdraw foley is played separately off the worn-state transition.
    auto* eqMgr = RE::ActorEquipManager::GetSingleton();
    if (!eqMgr) return;
    RE::BGSObjectInstance instance(static_cast<RE::TESForm*>(mask), nullptr);
    if (equip)
        eqMgr->EquipObject(actor, instance, /*stackID*/ 0, /*number*/ 1, /*slot*/ nullptr,
                           /*queueEquip*/ true, /*forceEquip*/ false, /*playSounds*/ false,
                           /*applyNow*/ false, /*locked*/ false);
    else
        eqMgr->UnequipObject(actor, &instance, /*number*/ 1, /*slot*/ nullptr, /*stackID*/ 0,
                             /*queueEquip*/ true, /*forceEquip*/ false, /*playSounds*/ false,
                             /*applyNow*/ false, /*slotBeingReplaced*/ nullptr);
}


void RadiationEngine::SetMaskEquipped(RE::TESObjectARMO* mask, bool equip)
{
    SetActorMaskEquipped(RE::PlayerCharacter::GetSingleton(), mask, equip);
}


// Mask up (or unmask) nearby NPCs for a rad weather. Scans the high-process actor list and, for each
// eligible NPC, equips the first gas mask it OWNS (mask-agnostic, mouth slot). Ghouls (rad-immune) and
// power-armor NPCs are skipped. Only NPCs we equipped are unmasked on the way out (tracked by formID),
// so an NPC that was already wearing a mask is left alone. Main-thread only (4Hz Update hook), so the
// ProcessLists iteration + VM equip calls are safe.
void RadiationEngine::EquipNearbyNpcMasks(bool equip)
{
    int n = 0;

    if (!equip)
    {
        // Unmask by the TRACKED set, not the current high-process list: an NPC we masked who
        // unloaded mid-storm (or wandered off) would otherwise keep the auto-equipped mask
        // permanently. GetFormByID resolves the ACHR whether or not the actor is still loaded;
        // the VM equip call is safe on unloaded actors.
        for (const std::uint32_t id : _autoMaskedNpcs)
        {
            RE::TESForm* form = RE::TESForm::GetFormByID(id);
            if (!form || form->GetFormType() != RE::ENUM_FORM_ID::kACHR) continue;
            auto* actor = static_cast<RE::Actor*>(form);
            if (actor->IsDead(false)) continue;
            if (auto* worn = GetActorWornMask(actor))
            {
                SetActorMaskEquipped(actor, worn, false);
                ++n;
            }
        }
        _autoMaskedNpcs.clear();
        if (n > 0)
            Log()->info("[Mask] removed {} NPC mask(s) after rad weather.", n);
        return;
    }

    auto* pl = RE::ProcessLists::GetSingleton();
    if (!pl) return;

    for (auto& handle : pl->highActorHandles)
    {
        auto actorPtr = handle.get();
        RE::Actor* actor = actorPtr.get();
        if (!actor || actor->IsDead(false) || actor->IsPlayerRef()) continue;
        if (IsGhoulActor(actor)) continue;            // ghouls revel in rads — no mask
        if (IsActorInPowerArmor(actor)) continue;      // sealed in a PA frame

        if (GetActorWornMask(actor)) continue;         // already masked (manually or by us)
        if (auto* mask = FindActorInventoryMask(actor))
        {
            SetActorMaskEquipped(actor, mask, true);
            _autoMaskedNpcs.insert(actor->formID);
            ++n;
        }
    }
    if (n > 0)
        Log()->info("[Mask] equipped {} NPC mask(s) for rad weather.", n);
}


// ----------------------------------------------------------------------------- gas-mask filter
// Read the [Filter] section of the mod INI (same file the HUD uses). All tuneable without a rebuild.
void RadiationEngine::LoadFilterIni()
{
    constexpr const char* kIni = ".\\Data\\F4SE\\Plugins\\FalloutAnomalyRadiation.ini";
    _filterDurationSec = ::GetPrivateProfileIntA("Filter", "DurationSeconds", 300, kIni);
    if (_filterDurationSec < 1) _filterDurationSec = 1;
    _filterReplaceKey  = ::GetPrivateProfileIntA("Filter", "ReplaceKey", 0, kIni);   // VK code; 0 = off
    _filterAutoReplace = ::GetPrivateProfileIntA("Filter", "AutoReplace", 0, kIni) != 0;
    {
        char b[16];
        ::GetPrivateProfileStringA("Filter", "ChokeRadsPerSec", "8.0", b, sizeof(b), kIni);
        _filterChokeRads = std::max(0.0f, static_cast<float>(std::atof(b)));
    }
    // [Condition] — mask durability.
    _conditionEnabled    = ::GetPrivateProfileIntA("Condition", "Enabled", 1, kIni) != 0;
    _conditionRepairKey  = ::GetPrivateProfileIntA("Condition", "RepairKey", 0, kIni);
    _conditionAutoRepair = ::GetPrivateProfileIntA("Condition", "AutoRepair", 0, kIni) != 0;
    {
        char b[16];
        ::GetPrivateProfileStringA("Condition", "DamagePerHit", "6.0", b, sizeof(b), kIni);
        _conditionHitDamage = std::max(0.0f, static_cast<float>(std::atof(b)));   // negative would REPAIR on hit
    }
    // [Choking] — health damage when breathing rad weather without working protection.
    _chokingEnabled = ::GetPrivateProfileIntA("Choking", "Enabled", 1, kIni) != 0;
    _chokeSounds    = ::GetPrivateProfileIntA("Choking", "Sounds", 1, kIni) != 0;
    {
        char b[16];
        ::GetPrivateProfileStringA("Choking", "HealthPerSecond", "1.5", b, sizeof(b), kIni);
        _chokeHealthPerSec = std::max(0.0f, static_cast<float>(std::atof(b)));
        ::GetPrivateProfileStringA("Choking", "FailedMaskFactor", "0.5", b, sizeof(b), kIni);
        _chokeFailedFactor = std::clamp(static_cast<float>(std::atof(b)), 0.0f, 1.0f);
    }
    // [Ambient] — the general exterior background bed (plays when no storm bed applies). Volume is
    // 0..100 (100 = loudest), mapped to the engine's attenuation in centibels (higher = quieter).
    _ambientEnabled = ::GetPrivateProfileIntA("Ambient", "Enabled", 1, kIni) != 0;
    {
        const int vol = std::clamp(static_cast<int>(::GetPrivateProfileIntA("Ambient", "Volume", 45, kIni)), 0, 100);
        _ambientAtten = static_cast<std::uint16_t>((100 - vol) * 30);  // 0->3000cb (silent), 100->0cb
        const int svol = std::clamp(static_cast<int>(::GetPrivateProfileIntA("Ambient", "StormVolume", 85, kIni)), 0, 100);
        _stormBedAtten = static_cast<std::uint16_t>((100 - svol) * 30);
    }
    // [Storm] — storm FREQUENCY, the #1 player complaint ("storms happen too much"). Gap between our
    // managed storms is in REAL minutes of play, so it does NOT drift with the modlist's timescale.
    // Users can dial storms down (or off) here without a rebuild.
    _stormsEnabled = ::GetPrivateProfileIntA("Storm", "Enabled", 1, kIni) != 0;
    {
        char b[32]{};
        ::GetPrivateProfileStringA("Storm", "MinMinutes", "35.0", b, sizeof(b), kIni);
        _stormMinRealMin = std::strtof(b, nullptr);
        ::GetPrivateProfileStringA("Storm", "MaxMinutes", "90.0", b, sizeof(b), kIni);
        _stormMaxRealMin = std::strtof(b, nullptr);
        // Sanity: never let a bad/zero INI value spam storms back-to-back.
        if (!(_stormMinRealMin >= 1.0f)) _stormMinRealMin = kStormMinRealMin;
        if (!(_stormMaxRealMin >= _stormMinRealMin)) _stormMaxRealMin = _stormMinRealMin * 2.0f;

        const int vgapDefault = static_cast<int>(kRadstormMinGapSec / 60.0);
        const int vgap = ::GetPrivateProfileIntA("Storm", "VanillaRadstormGapMinutes", vgapDefault, kIni);
        _vanillaRadstormGapSec = static_cast<double>(std::max(0, vgap)) * 60.0;

        // Silent dome-only APPROACH lead before the alarm + storm proper. Float minutes so it can be
        // dialled down for testing (e.g. 0.25 = 15 s). Clamped >= 0; 0 = no lead (alarm fires immediately).
        char lb[32]{};
        ::GetPrivateProfileStringA("Storm", "DomeLeadMinutes", "3.0", lb, sizeof(lb), kIni);
        float leadMin = std::strtof(lb, nullptr);
        if (!(leadMin >= 0.0f)) leadMin = 3.0f;
        _domeLeadTicks = static_cast<int>(leadMin * 60.0f * 4.0f);
    }

    Log()->info(
        "[Filter] duration={}s, replaceKey=0x{:X}, autoReplace={}, choke={:.1f}/s | "
        "[Condition] enabled={}, dmg/hit={:.1f}, repairKey=0x{:X}, autoRepair={} | "
        "[Choking] enabled={}, hp/s={:.1f}, failedMaskFactor={:.2f}, sounds={} | "
        "[Ambient] enabled={}, atten={} stormAtten={} | "
        "[Storm] enabled={}, gap={:.0f}-{:.0f} real min, vanillaRadstormGap={:.0f} min",
        _filterDurationSec, _filterReplaceKey, _filterAutoReplace, _filterChokeRads,
        _conditionEnabled, _conditionHitDamage, _conditionRepairKey, _conditionAutoRepair,
        _chokingEnabled, _chokeHealthPerSec, _chokeFailedFactor, _chokeSounds,
        _ambientEnabled, _ambientAtten, _stormBedAtten,
        _stormsEnabled, _stormMinRealMin, _stormMaxRealMin, _vanillaRadstormGapSec / 60.0);
}


// Count of the consumable filter in the player's inventory (native, no VM).
int RadiationEngine::FilterItemCount() const
{
    if (!_filterItem) return 0;
    auto* player = RE::PlayerCharacter::GetSingleton();
    return player ? static_cast<int>(player->GetInventoryObjectCount(_filterItem)) : 0;
}


// Consume one filter from the player's inventory and refill the charge to 100%.
void RadiationEngine::ReplaceFilter(bool silent)
{
    if (!_filterItem) return;
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    if (FilterItemCount() <= 0)
    {
        if (!silent) Notify("[Mask] No spare filters.", true);
        return;
    }
    // Default reason (kNone) + no destination container = consume/destroy one from inventory.
    RE::TESObjectREFR::RemoveItemData data(_filterItem, 1);
    player->RemoveItem(data);
    _filterCharge   = 100.0f;
    _filterLowWarned = 0;
    // Replace foley: the hurried "fast" take for auto-replace (mid-action), the full take for a
    // deliberate hotkey/menu replace. Alternates between the two recordings of each pair.
    {
        static bool alt = false;
        alt = !alt;
        RE::BSSoundHandle h{};
        Play2DSound(silent ? (alt ? kSndFilterFastA : kSndFilterFastB)
                           : (alt ? kSndFilterA : kSndFilterB), h);
    }
    if (!silent) Notify("[Mask] Filter replaced.", false);
    Log()->info("[Filter] replaced (remaining {}).", FilterItemCount());
}


// Player struck -> the worn mask loses durability. Called by the hit sink (main thread).
void RadiationEngine::OnPlayerHit()
{
    if (!_conditionEnabled) return;
    if (_maskCondition <= 0.0f) return;       // already broken
    if (!IsWearingGasMask()) return;          // only a worn mask takes damage
    _maskCondition = std::max(0.0f, _maskCondition - _conditionHitDamage);
    // the broken/damaged warnings are emitted from the per-tick block (centralized).
}


int RadiationEngine::RepairKitCount() const
{
    if (!_repairKitItem) return 0;
    auto* player = RE::PlayerCharacter::GetSingleton();
    return player ? static_cast<int>(player->GetInventoryObjectCount(_repairKitItem)) : 0;
}


// Consume one repair kit and restore the mask condition to 100%.
void RadiationEngine::RepairMask(bool silent)
{
    if (!_repairKitItem) return;
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    if (RepairKitCount() <= 0)
    {
        if (!silent) Notify("[Mask] No repair kits.", true);
        return;
    }
    RE::TESObjectREFR::RemoveItemData data(_repairKitItem, 1);
    player->RemoveItem(data);
    _maskCondition     = 100.0f;
    _conditionLowWarned = 0;
    if (!silent) Notify("[Mask] Mask repaired.", false);
    Log()->info("[Condition] mask repaired (kits left {}).", RepairKitCount());
}


// Call the Papyrus native Game.FadeOutGame from C++ via the script VM (no Prisma, no quest, no
// attached script). fadingOut=true fades TO black, false fades back; black fade; no pre-delay.
void RadiationEngine::CallFadeOutGame(bool /*fadingOut*/, float /*duration*/)
{
    // DISABLED — the Papyrus `Game.FadeOutGame` screen fade crashed on every mask put-on
    // (crash-2026-07-06-21-28-51 and -22-08-14: AV in VirtualMachine::DispatchStaticCall, rax=0).
    // The 4Hz Update hook runs on a JobListManager/BSJobs worker thread, and in this runtime the
    // F4SE task queue ALSO drains on a worker (Main::UpdateNonRenderSafeAITasks under BSJobs::
    // JobThread) — so even deferring via AddTask still dispatched Papyrus off the main thread and
    // crashed identically. Dispatching Papyrus from this path is simply unsafe here.
    //
    // The fade is cosmetic (a put-on/off blink); the gas-mask goggle OVERLAY still appears via
    // RadHud::PushMask with no fade. If the blink is wanted back, do it in the PrismaUI view (a JS
    // black-flash div in radhud), NOT through the script VM. No-op for now.
}


// Quick screen blink for putting the mask on / taking it off — a lightweight stand-in for a 3D
// equip animation. Fades to black now and arms the fade-back a couple of 4Hz ticks later, so the
// screen holds on black briefly then eases back as the goggle overlay settles in.
void RadiationEngine::PlayMaskFade(bool putOn)
{
    if (putOn)
    {
        CallFadeOutGame(true, 0.18f);   // fast to black
        _maskFadeBackTicks = 2;         // ~0.5 s hold at 4 Hz
        _maskFadeBackDur   = 0.45f;     // then ease back
    }
    else
    {
        CallFadeOutGame(true, 0.14f);   // shorter blink coming off
        _maskFadeBackTicks = 1;         // ~0.25 s hold
        _maskFadeBackDur   = 0.35f;
    }
}
