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

    class EquipSink final : public RE::BSTEventSink<RE::ActorEquipManagerEvent::Event>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(
            const RE::ActorEquipManagerEvent::Event& event,
            RE::BSTEventSource<RE::ActorEquipManagerEvent::Event>*) override
        {
            if (event.changeType != RE::ActorEquipManagerEvent::Type::kUnequip ||
                !event.actorAffected || !event.itemAffected || !event.itemAffected->object)
                return RE::BSEventNotifyControl::kContinue;

            const auto actorID = event.actorAffected->GetFormID();
            const auto maskID = event.itemAffected->object->GetFormID();
            std::scoped_lock lock(g_mutex);
            const auto it = g_ownedMasks.find(actorID);
            if (it != g_ownedMasks.end() && it->second.maskFormID == maskID && !it->second.IsTemporary())
                g_ownedMasks.erase(it);
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
    const auto it = g_ownedMasks.find(actorFormID);
    if (it != g_ownedMasks.end())
    {
        if (it->second.IsTemporary()) return false;
        it->second.maskFormID = maskFormID;
        return true;
    }
    if (g_ownedMasks.size() >= kMaxEntries) return false;
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
    if (g_ownedMasks.size() >= kMaxEntries) return false;
    g_ownedMasks.emplace(actorFormID, candidate);
    return true;
}

bool RadMaskOwnership::Owns(std::uint32_t actorFormID, std::uint32_t maskFormID)
{
    std::scoped_lock lock(g_mutex);
    const auto it = g_ownedMasks.find(actorFormID);
    return it != g_ownedMasks.end() && it->second.maskFormID == maskFormID;
}

std::optional<RadMaskOwnership::Entry> RadMaskOwnership::Lookup(std::uint32_t actorFormID)
{
    std::scoped_lock lock(g_mutex);
    const auto it = g_ownedMasks.find(actorFormID);
    if (it == g_ownedMasks.end()) return std::nullopt;
    return it->second;
}

void RadMaskOwnership::Release(std::uint32_t actorFormID)
{
    std::scoped_lock lock(g_mutex);
    g_ownedMasks.erase(actorFormID);
}

std::vector<RadMaskOwnership::Entry> RadMaskOwnership::Snapshot()
{
    std::scoped_lock lock(g_mutex);
    std::vector<Entry> entries;
    entries.reserve(g_ownedMasks.size());
    for (const auto& [actorFormID, entry] : g_ownedMasks)
        entries.push_back(entry);
    std::ranges::sort(entries, {}, &Entry::actorFormID);
    return entries;
}

bool RadMaskOwnership::Restore(const std::vector<Entry>& entries)
{
    if (!IsValidSnapshot(entries)) return false;
    std::unordered_map<std::uint32_t, Entry> restored;
    restored.reserve(entries.size());
    for (const auto& entry : entries)
        restored.emplace(entry.actorFormID, entry);
    std::scoped_lock lock(g_mutex);
    g_ownedMasks = std::move(restored);
    return true;
}

void RadMaskOwnership::Clear()
{
    std::scoped_lock lock(g_mutex);
    g_ownedMasks.clear();
}
