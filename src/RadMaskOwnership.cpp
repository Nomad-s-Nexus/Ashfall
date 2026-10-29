#include "pch.h"
#include "RadMaskOwnership.h"

#include <RE/A/Actor.h>
#include <RE/A/ActorEquipManager.h>
#include <RE/A/ActorEquipManagerEvent.h>
#include <RE/B/BGSObjectInstance.h>
#include <RE/T/TESForm.h>

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace
{
    std::mutex g_mutex;
    std::unordered_map<std::uint32_t, RadMaskOwnership::Entry> g_ownedMasks;
    bool g_sinkRegistered = false;

    // Constraint: an unequip only forfeits ownership if no re-equip of the same mask follows within this window.
    constexpr ULONGLONG kUnequipGraceMs = 2500;
    std::unordered_map<std::uint32_t, ULONGLONG> g_pendingUnequip;

    // Constraint: caller holds g_mutex.
    void PurgeIfForfeitedLocked(std::uint32_t actorFormID)
    {
        const auto pending = g_pendingUnequip.find(actorFormID);
        if (pending == g_pendingUnequip.end() || ::GetTickCount64() - pending->second < kUnequipGraceMs)
            return;
        g_pendingUnequip.erase(pending);
        g_ownedMasks.erase(actorFormID);
    }

    void PurgeAllForfeitedLocked()
    {
        const auto now = ::GetTickCount64();
        std::erase_if(g_pendingUnequip, [now](const auto& pending) {
            if (now - pending.second < kUnequipGraceMs)
                return false;
            g_ownedMasks.erase(pending.first);
            return true;
        });
    }

    class EquipSink final : public RE::BSTEventSink<RE::ActorEquipManagerEvent::Event>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(
            const RE::ActorEquipManagerEvent::Event& event,
            RE::BSTEventSource<RE::ActorEquipManagerEvent::Event>*) override
        {
            const bool unequip = event.changeType == RE::ActorEquipManagerEvent::Type::kUnequip;
            const bool equip = event.changeType == RE::ActorEquipManagerEvent::Type::kEquip;
            if ((!unequip && !equip) || !event.actorAffected || !event.itemAffected || !event.itemAffected->object)
                return RE::BSEventNotifyControl::kContinue;

            const auto actorID = event.actorAffected->GetFormID();
            const auto maskID = event.itemAffected->object->GetFormID();
            std::scoped_lock lock(g_mutex);
            PurgeIfForfeitedLocked(actorID);
            const auto it = g_ownedMasks.find(actorID);
            if (it == g_ownedMasks.end() || it->second.maskFormID != maskID || it->second.IsTemporary())
                return RE::BSEventNotifyControl::kContinue;
            if (unequip)
                g_pendingUnequip[actorID] = ::GetTickCount64();
            else
                g_pendingUnequip.erase(actorID);
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    EquipSink g_sink;
}

bool RadMaskOwnership::Activate()
{
    if (g_sinkRegistered) return true;
    auto* manager = RE::ActorEquipManager::GetSingleton();
    if (!manager) return false;
    manager->RegisterSink(&g_sink);
    g_sinkRegistered = true;
    return true;
}

bool RadMaskOwnership::Claim(std::uint32_t actorFormID, std::uint32_t maskFormID)
{
    if (actorFormID == 0 || maskFormID == 0 || !Activate()) return false;
    std::scoped_lock lock(g_mutex);
    g_pendingUnequip.erase(actorFormID);
    const auto it = g_ownedMasks.find(actorFormID);
    if (it != g_ownedMasks.end())
    {
        if (it->second.IsTemporary()) return false;
        it->second.maskFormID = maskFormID;
        return true;
    }
    if (g_ownedMasks.size() >= kMaxOwnedActors) return false;
    g_ownedMasks.emplace(actorFormID, Entry{ actorFormID, maskFormID });
    return true;
}

bool RadMaskOwnership::ClaimTemporary(
    std::uint32_t actorFormID,
    std::uint32_t maskFormID,
    const std::array<std::uint32_t, kMaxDisplacedGear>& displacedFormIDs)
{
    if (actorFormID == 0 || maskFormID == 0 || !Activate()) return false;
    const Entry candidate{ actorFormID, maskFormID, kTemporaryMaskFlag, displacedFormIDs };
    if (!IsValidSnapshot({ candidate })) return false;

    std::scoped_lock lock(g_mutex);
    const auto it = g_ownedMasks.find(actorFormID);
    if (it != g_ownedMasks.end())
        return it->second == candidate;
    if (g_ownedMasks.size() >= kMaxOwnedActors) return false;
    g_ownedMasks.emplace(actorFormID, candidate);
    return true;
}

bool RadMaskOwnership::Owns(std::uint32_t actorFormID, std::uint32_t maskFormID)
{
    std::scoped_lock lock(g_mutex);
    PurgeIfForfeitedLocked(actorFormID);
    const auto it = g_ownedMasks.find(actorFormID);
    return it != g_ownedMasks.end() && it->second.maskFormID == maskFormID;
}

std::optional<RadMaskOwnership::Entry> RadMaskOwnership::Lookup(std::uint32_t actorFormID)
{
    std::scoped_lock lock(g_mutex);
    PurgeIfForfeitedLocked(actorFormID);
    const auto it = g_ownedMasks.find(actorFormID);
    if (it == g_ownedMasks.end()) return std::nullopt;
    return it->second;
}

void RadMaskOwnership::Release(std::uint32_t actorFormID)
{
    std::scoped_lock lock(g_mutex);
    g_pendingUnequip.erase(actorFormID);
    g_ownedMasks.erase(actorFormID);
}

std::vector<RadMaskOwnership::Entry> RadMaskOwnership::Snapshot()
{
    std::scoped_lock lock(g_mutex);
    PurgeAllForfeitedLocked();
    std::vector<Entry> logical;
    logical.reserve(g_ownedMasks.size());
    for (const auto& [actorFormID, entry] : g_ownedMasks)
        logical.push_back(entry);
    std::ranges::sort(logical, {}, &Entry::actorFormID);

    std::vector<Entry> serialized;
    serialized.reserve(logical.size() * 2);
    for (const auto& entry : logical)
    {
        serialized.push_back({ entry.actorFormID, entry.maskFormID });
        if (!entry.IsTemporary()) continue;

        serialized.push_back({ entry.actorFormID, kTemporaryMarkerFormID });
        std::array<std::uint32_t, kMaxDisplacedGear> displaced = entry.displacedFormIDs;
        std::ranges::sort(displaced);
        for (const auto formID : displaced)
            if (formID != 0)
                serialized.push_back({ entry.actorFormID, formID });
    }
    return serialized;
}

bool RadMaskOwnership::Restore(const std::vector<Entry>& entries)
{
    if (!IsValidSnapshot(entries)) return false;

    bool logicalInput = false;
    for (const auto& entry : entries)
        if (HasLogicalMetadata(entry)) logicalInput = true;

    std::vector<Entry> logical;
    if (logicalInput)
    {
        logical = entries;
    }
    else
    {
        logical.reserve(entries.size());
        std::size_t i = 0;
        while (i < entries.size())
        {
            std::size_t end = i + 1;
            while (end < entries.size() && entries[end].actorFormID == entries[i].actorFormID)
                ++end;

            if (end - i == 1)
            {
                logical.push_back({ entries[i].actorFormID, entries[i].maskFormID });
                i = end;
                continue;
            }

            bool hasMask = false;
            bool hasMarker = false;
            for (std::size_t a = i; a < end; ++a)
            {
                hasMask |= entries[a].maskFormID == kTemporaryNpcMaskFormID;
                hasMarker |= entries[a].maskFormID == kTemporaryMarkerFormID;
            }
            if (!hasMask || !hasMarker) return false;

            Entry restored{ entries[i].actorFormID, kTemporaryNpcMaskFormID, kTemporaryMaskFlag };
            std::array<std::uint32_t, kMaxDisplacedGear> displaced{};
            std::size_t count = 0;
            for (std::size_t a = i; a < end; ++a)
            {
                const auto formID = entries[a].maskFormID;
                if (formID == kTemporaryNpcMaskFormID || formID == kTemporaryMarkerFormID) continue;
                if (count >= displaced.size()) return false;
                displaced[count++] = formID;
            }
            std::ranges::sort(displaced.begin(), displaced.begin() + static_cast<std::ptrdiff_t>(count));
            restored.displacedFormIDs = displaced;
            logical.push_back(restored);
            i = end;
        }
    }

    if (!IsValidSnapshot(logical) || logical.size() > kMaxOwnedActors) return false;
    std::unordered_map<std::uint32_t, Entry> restored;
    restored.reserve(logical.size());
    for (const auto& entry : logical)
        restored.emplace(entry.actorFormID, entry);
    std::scoped_lock lock(g_mutex);
    g_ownedMasks = std::move(restored);
    g_pendingUnequip.clear();
    return true;
}

void RadMaskOwnership::Clear()
{
    std::scoped_lock lock(g_mutex);
    g_ownedMasks.clear();
    g_pendingUnequip.clear();
}
