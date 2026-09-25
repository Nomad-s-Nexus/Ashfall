#include "RadEngine_Internal.h"
#include "RadCorrectnessPolicy.h"
#include "RadLifecycleDispatch.h"
#include "Hooks.h"
#include "RadMaskOwnership.h"

using namespace RadInternal;

// Gas mask: worn detection, auto-equip (player + NPCs), filter drain/replace, condition/repair, put-on fade.
// Split out of RadiationEngine.cpp (was ~4,300 lines). Shared constants/helpers live in
// RadEngine_Internal.h; the class itself is declared in RadiationEngine.h.

namespace
{
    constexpr std::uint32_t kActorTypeHumanKeywordID = 0x0002CB72;
    constexpr std::uint32_t kTemporaryNpcGasMaskID = 0x001184C1;

    bool PreservesNpcHair(const RE::TESObjectARMO* armor)
    {
        if (!armor) return false;
        constexpr std::int32_t kHairSlots[] = {
            static_cast<std::int32_t>(RE::BIPED_OBJECT::kHairTop),
            static_cast<std::int32_t>(RE::BIPED_OBJECT::kHairLong),
            static_cast<std::int32_t>(RE::BIPED_OBJECT::kScalp),
        };
        for (const auto slot : kHairSlots)
            if (armor->FillsBipedSlot(slot)) return false;
        return true;
    }

    bool WearsPowerArmorHelmet(RE::Actor* actor, const RE::BGSKeyword* powerKeyword)
    {
        if (!actor || !actor->biped || !powerKeyword) return false;
        constexpr auto kHelmetSlot = static_cast<std::int32_t>(RE::BIPED_OBJECT::kHairTop);
        auto& biped = *actor->biped;
        for (std::size_t i = 0; i < std::size(biped.object); ++i)
        {
            RE::TESForm* form = biped.object[i].parent.object;
            if (!form || form->GetFormType() != RE::ENUM_FORM_ID::kARMO) continue;
            auto* armor = static_cast<RE::TESObjectARMO*>(form);
            if (armor->HasKeyword(powerKeyword) && armor->FillsBipedSlot(kHelmetSlot))
                return true;
        }
        return false;
    }

    bool IsHumanInCell(RE::Actor* actor, RE::TESObjectCELL* cell, const RE::BGSKeyword* humanKeyword)
    {
        if (!actor || !cell || cell->IsInterior() || !humanKeyword || actor->GetParentCell() != cell) return false;
        auto* base = actor->GetObjectReference();
        if (!base || base->GetFormType() != RE::ENUM_FORM_ID::kNPC_) return false;
        auto* race = static_cast<RE::TESNPC*>(base)->GetFormRace();
        return race && race->HasKeyword(humanKeyword);
    }

    bool WearsExactArmor(RE::Actor* actor, std::uint32_t formID)
    {
        if (!actor || !actor->biped || formID == 0) return false;
        auto& biped = *actor->biped;
        for (std::size_t i = 0; i < std::size(biped.object); ++i)
        {
            auto* form = biped.object[i].parent.object;
            if (form && form->GetFormID() == formID) return true;
        }
        return false;
    }

    bool CaptureDisplacedGear(
        RE::Actor* actor,
        const RE::TESObjectARMO* mask,
        std::array<std::uint32_t, RadMaskOwnership::kMaxDisplacedGear>& displaced)
    {
        if (!actor || !actor->biped || !mask) return false;
        const auto maskSlots = mask->bipedModelData.bipedObjectSlots;
        std::size_t count = 0;
        auto& biped = *actor->biped;
        for (std::size_t i = 0; i < std::size(biped.object); ++i)
        {
            auto* form = biped.object[i].parent.object;
            if (!form || form->GetFormType() != RE::ENUM_FORM_ID::kARMO || form->GetFormID() == mask->GetFormID())
                continue;
            auto* armor = static_cast<RE::TESObjectARMO*>(form);
            if ((armor->bipedModelData.bipedObjectSlots & maskSlots) == 0) continue;
            const auto id = armor->GetFormID();
            bool duplicate = false;
            for (std::size_t j = 0; j < count; ++j)
                if (displaced[j] == id) duplicate = true;
            if (duplicate) continue;
            if (count >= displaced.size()) return false;
            displaced[count++] = id;
        }
        return true;
    }
}


bool RadiationEngine::IsRecognizedGasMask(const RE::TESObjectARMO* armor) const
{
    return armor && RadCorrectnessPolicy::IsGasMask(
        armor->FillsBipedSlot(static_cast<std::int32_t>(RE::BIPED_OBJECT::kMouth)),
        _gasMaskKeyword && armor->HasKeyword(_gasMaskKeyword));
}


// Gas-mask detection for the first-person mask screen overlay (drawn by the PrismaUI HUD). A worn
// armor is protective only when it fills the mouth slot and carries FARO_GasMask. Const: only reads
// worn gear.
RE::TESObjectARMO* RadiationEngine::GetActorWornMask(RE::Actor* actor) const
{
    if (!actor || !actor->biped) return nullptr;

    auto& biped = *actor->biped;
    for (std::size_t i = 0; i < std::size(biped.object); ++i)
    {
        RE::TESForm* form = biped.object[i].parent.object;
        if (!form || form->GetFormType() != RE::ENUM_FORM_ID::kARMO) continue;
        auto* armo = static_cast<RE::TESObjectARMO*>(form);
        if (IsRecognizedGasMask(armo))
            return armo;
    }
    return nullptr;
}


RE::TESObjectARMO* RadiationEngine::GetWornMask() const
{
    return GetActorWornMask(RE::PlayerCharacter::GetSingleton());
}


bool RadiationEngine::IsWearingGasMask() const { return GetWornMask() != nullptr; }


// First owned recognized gas mask in an actor's inventory, worn or not — used to auto-don a mask in
// rad weather. A candidate must carry FARO_GasMask and fill the mouth slot.
RE::TESObjectARMO* RadiationEngine::FindActorInventoryMask(RE::Actor* actor) const
{
    if (!actor) return nullptr;
    auto* inv = actor->inventoryList;
    if (!inv) return nullptr;

    RE::TESObjectARMO* found = nullptr;
    inv->ForEachStack(
        [this](RE::BGSInventoryItem& a_item) -> bool {
            auto* obj = a_item.object;
            if (!obj || obj->GetFormType() != RE::ENUM_FORM_ID::kARMO) return false;
            auto* armo = static_cast<RE::TESObjectARMO*>(obj);
            return IsRecognizedGasMask(armo);
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


// Silent native equip/unequip of a mask on an actor. These calls are made from the verified game-main
// thread: the 4 Hz radiation tick is marshaled there by Hooks, and any hit-event path that reaches
// engine state is likewise dispatched there. Silent so the player's put-on fade/foley stays separate.
void RadiationEngine::SetActorMaskEquipped(RE::Actor* actor, RE::TESObjectARMO* mask, bool equip)
{
    if (!mask || !actor) return;
    auto* eqMgr = RE::ActorEquipManager::GetSingleton();
    if (!eqMgr) return;
    const auto actorID = actor->GetFormID();
    const auto maskID = mask->GetFormID();
    RE::BGSObjectInstance instance(static_cast<RE::TESForm*>(mask), nullptr);
    if (equip)
    {
        if (!RadMaskOwnership::Claim(actorID, maskID)) return;
        if (!eqMgr->EquipObject(actor, instance, 0, 1, nullptr, true, false, false, false, false))
            RadMaskOwnership::Release(actorID);
    }
    else
    {
        if (!RadMaskOwnership::Owns(actorID, maskID)) return;
        if (eqMgr->UnequipObject(actor, &instance, 1, nullptr, 0, true, false, false, false, nullptr))
            RadMaskOwnership::Release(actorID);
    }
}


void RadiationEngine::SetMaskEquipped(RE::TESObjectARMO* mask, bool equip)
{
    SetActorMaskEquipped(RE::PlayerCharacter::GetSingleton(), mask, equip);
}


// Mask up (or unmask) nearby NPCs for a rad weather. Scans the high-process actor list and, for each
// eligible NPC, equips the first gas mask it OWNS (mask-agnostic, mouth slot). Ghouls (rad-immune) and
// so an NPC that was already wearing a mask is left alone. Main-thread only: ProcessLists iteration,
// inventory reads, worn-biped reads, and native equip calls all stay on the verified dispatcher path.
void RadiationEngine::EquipNearbyNpcMasks(bool equip)
{
    int n = 0;
    auto* eqMgr = RE::ActorEquipManager::GetSingleton();
    if (!eqMgr) return;

    auto cleanupEntry = [this, eqMgr](RE::Actor* actor, const RadMaskOwnership::Entry& entry) -> bool {
        if (!actor) return false;
        if (!actor->biped) return false;
        auto* maskForm = RE::TESForm::GetFormByID(entry.maskFormID);
        auto* mask = maskForm && maskForm->GetFormType() == RE::ENUM_FORM_ID::kARMO
            ? static_cast<RE::TESObjectARMO*>(maskForm) : nullptr;
        if (!mask)
        {
            RadMaskOwnership::Release(entry.actorFormID);
            return true;
        }

        if (!entry.IsTemporary())
        {
            if (WearsExactArmor(actor, entry.maskFormID))
            {
                SetActorMaskEquipped(actor, mask, false);
                if (RadMaskOwnership::Owns(entry.actorFormID, entry.maskFormID)) return false;
            }
            else
            {
                RadMaskOwnership::Release(entry.actorFormID);
            }
            return true;
        }

        if (WearsExactArmor(actor, entry.maskFormID))
        {
            RE::BGSObjectInstance instance(static_cast<RE::TESForm*>(mask), nullptr);
            if (!eqMgr->UnequipObject(actor, &instance, 1, nullptr, 0, false, true, false, true, nullptr))
                return false;
        }

        const auto before = actor->GetInventoryObjectCount(mask);
        if (before > 0)
        {
            RE::TESObjectREFR::RemoveItemData data(mask, 1);
            actor->RemoveItem(data);
            if (actor->GetInventoryObjectCount(mask) >= before) return false;
        }

        bool restorePending = false;
        for (const auto formID : entry.displacedFormIDs)
        {
            if (formID == 0) break;
            auto* form = RE::TESForm::GetFormByID(formID);
            if (!form || form->GetFormType() != RE::ENUM_FORM_ID::kARMO) continue;
            auto* armor = static_cast<RE::TESObjectARMO*>(form);
            if (actor->GetInventoryObjectCount(armor) <= 0 || WearsExactArmor(actor, formID)) continue;
            RE::BGSObjectInstance instance(static_cast<RE::TESForm*>(armor), nullptr);
            if (!eqMgr->EquipObject(actor, instance, 0, 1, nullptr, false, true, false, true, false))
                restorePending = true;
        }
        if (restorePending) return false;
        RadMaskOwnership::Release(entry.actorFormID);
        return true;
    };

    if (!equip)
    {
        std::unordered_set<std::uint32_t> pending;
        for (const std::uint32_t id : _autoMaskedNpcs)
        {
            auto entry = RadMaskOwnership::Lookup(id);
            if (!entry) continue;
            RE::TESForm* form = RE::TESForm::GetFormByID(id);
            if (!form || form->GetFormType() != RE::ENUM_FORM_ID::kACHR)
            {
                RadMaskOwnership::Release(id);
                continue;
            }
            auto* actor = static_cast<RE::Actor*>(form);
            if (!cleanupEntry(actor, *entry))
                pending.insert(id);
            else
                ++n;
        }
        _autoMaskedNpcs = std::move(pending);
        if (n > 0)
            Log()->info("[Mask] removed {} NPC mask(s) after rad weather.", n);
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* playerCell = player ? player->GetParentCell() : nullptr;
    auto* dh = RE::TESDataHandler::GetSingleton();
    if (!player || !playerCell || !dh) return;

    auto* humanForm = dh->LookupForm(kActorTypeHumanKeywordID, "Fallout4.esm");
    auto* humanKeyword = humanForm && humanForm->GetFormType() == RE::ENUM_FORM_ID::kKYWD
        ? static_cast<RE::BGSKeyword*>(humanForm) : nullptr;
    auto* maskForm = dh->LookupForm(kTemporaryNpcGasMaskID, "Fallout4.esm");
    auto* tempMask = maskForm && maskForm->GetFormType() == RE::ENUM_FORM_ID::kARMO
        ? static_cast<RE::TESObjectARMO*>(maskForm) : nullptr;
    if (!_armorTypePowerKeyword || !humanKeyword || !tempMask || !IsRecognizedGasMask(tempMask) || !PreservesNpcHair(tempMask)) return;

    std::unordered_set<std::uint32_t> retained;
    for (const auto id : _autoMaskedNpcs)
    {
        auto entry = RadMaskOwnership::Lookup(id);
        if (!entry) continue;
        auto* form = RE::TESForm::GetFormByID(id);
        if (!form || form->GetFormType() != RE::ENUM_FORM_ID::kACHR)
        {
            RadMaskOwnership::Release(id);
            continue;
        }
        auto* actor = static_cast<RE::Actor*>(form);
        const bool eligible = !actor->IsDead(false) &&
            IsHumanInCell(actor, playerCell, humanKeyword) &&
            !WearsPowerArmorHelmet(actor, _armorTypePowerKeyword);
        if (eligible)
            retained.insert(id);
        else if (!cleanupEntry(actor, *entry))
            retained.insert(id);
    }
    _autoMaskedNpcs = std::move(retained);

    auto* pl = RE::ProcessLists::GetSingleton();
    if (!pl) return;

    for (auto& handle : pl->highActorHandles)
    {
        auto actorPtr = handle.get();
        RE::Actor* actor = actorPtr.get();
        if (!actor || actor->IsDead(false) || actor->IsPlayerRef()) continue;
        if (!IsHumanInCell(actor, playerCell, humanKeyword)) continue;
        if (WearsPowerArmorHelmet(actor, _armorTypePowerKeyword)) continue;

        const auto actorID = actor->GetFormID();
        if (auto entry = RadMaskOwnership::Lookup(actorID))
        {
            if (entry->IsTemporary())
            {
                if (WearsExactArmor(actor, entry->maskFormID))
                {
                    _autoMaskedNpcs.insert(actorID);
                    continue;
                }
                if (GetActorWornMask(actor))
                {
                    if (!cleanupEntry(actor, *entry)) _autoMaskedNpcs.insert(actorID);
                    continue;
                }
                auto* ownedForm = RE::TESForm::GetFormByID(entry->maskFormID);
                auto* ownedMask = ownedForm && ownedForm->GetFormType() == RE::ENUM_FORM_ID::kARMO
                    ? static_cast<RE::TESObjectARMO*>(ownedForm) : nullptr;
                if (ownedMask && actor->GetInventoryObjectCount(ownedMask) > 0)
                {
                    RE::BGSObjectInstance instance(static_cast<RE::TESForm*>(ownedMask), nullptr);
                    if (eqMgr->EquipObject(actor, instance, 0, 1, nullptr, false, true, false, true, false))
                        _autoMaskedNpcs.insert(actorID);
                    continue;
                }
                RadMaskOwnership::Release(actorID);
            }
            else
            {
                _autoMaskedNpcs.insert(actorID);
                continue;
            }
        }

        if (GetActorWornMask(actor)) continue;
        if (!actor->biped) continue;

        std::array<std::uint32_t, RadMaskOwnership::kMaxDisplacedGear> displaced{};
        if (!CaptureDisplacedGear(actor, tempMask, displaced)) continue;

        const auto before = actor->GetInventoryObjectCount(tempMask);
        actor->AddObjectToContainer(tempMask, nullptr, 1, nullptr, RE::ITEM_REMOVE_REASON::kNone);
        if (actor->GetInventoryObjectCount(tempMask) <= before) continue;

        if (!RadMaskOwnership::ClaimTemporary(actorID, tempMask->GetFormID(), displaced))
        {
            RE::TESObjectREFR::RemoveItemData data(tempMask, 1);
            actor->RemoveItem(data);
            continue;
        }

        RE::BGSObjectInstance instance(static_cast<RE::TESForm*>(tempMask), nullptr);
        if (!eqMgr->EquipObject(actor, instance, 0, 1, nullptr, false, true, false, true, false))
        {
            RadMaskOwnership::Release(actorID);
            RE::TESObjectREFR::RemoveItemData data(tempMask, 1);
            actor->RemoveItem(data);
            continue;
        }

        _autoMaskedNpcs.insert(actorID);
        ++n;
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


// Player struck -> the worn mask loses durability. TESHitEvent delivery is not treated as a thread-
// affinity guarantee: if this arrives off the verified game-main thread, marshal it before reading
// the player's biped state or mutating mask-condition state shared with the 4 Hz update.
void RadiationEngine::OnPlayerHit()
{
    const auto generation = RadLifecycleDispatch::ReadyPlayerUpdateGeneration();
    if (generation == 0) return;

    if (!Hooks::IsGameMainThread())
    {
        if (!Hooks::QueueMainThreadTask([generation]() {
                if (!RadLifecycleDispatch::PlayerUpdatesReady(generation)) return;
                RadiationEngine::GetSingleton().OnPlayerHit();
            }))
            Log()->error("[Condition] Dropped player-hit mask damage because main-thread dispatch was unavailable.");
        return;
    }

    if (!RadLifecycleDispatch::PlayerUpdatesReady(generation)) return;
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
