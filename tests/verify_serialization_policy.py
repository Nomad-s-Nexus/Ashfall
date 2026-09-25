from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / "src" / "RadiationEngine.cpp").read_text(encoding="utf-8")
lifecycle = (root / "src" / "RadLifecycle.cpp").read_text(encoding="utf-8")
mask_source = (root / "src" / "RadEngine_Mask.cpp").read_text(encoding="utf-8")
ownership_header = (root / "src" / "RadMaskOwnership.h").read_text(encoding="utf-8")
ownership_source = (root / "src" / "RadMaskOwnership.cpp").read_text(encoding="utf-8")
save = source.split("void RadiationEngine::SaveState", 1)[1].split("void RadiationEngine::LoadState", 1)[0]
load = source.split("void RadiationEngine::LoadState", 1)[1].split("void RadiationEngine::RevertState", 1)[0]
revert = source.split("void RadiationEngine::RevertState", 1)[1].split("void RadiationEngine::Notify", 1)[0]
postload = source.split("void RadiationEngine::OnPostLoad", 1)[1]

for marker in (
    "std::vector<RadiationSerialization::FilterState> filterCandidates;",
    "filterSeen = 0, filterRejected = 0;",
    "RadiationSerialization::FilterState candidate{ charge, condition };",
    "RadiationSerialization::IsValidFilterState(candidate)",
    "filterCandidates.push_back(candidate);",
    "RadiationSerialization::ClassifyFilterCandidates(filterCandidates)",
    "_filterCharge = filter.charge;",
    "_maskCondition = filter.condition;",
    "conflicting valid filter records",
):
    if marker not in load:
        raise SystemExit(f"missing filter serialization contract: {marker}")

filter_record = load.split("if (type == kFilterRecord)", 1)[1].split("if (type != kStormClockRecord)", 1)[0]
for forbidden in ("_filterCharge =", "_maskCondition ="):
    if forbidden in filter_record:
        raise SystemExit("filter records must be collected before live state is committed")

scan_end = load.index("if (clockCandidates.size() == 1")
restore = load.index("const auto& filter = filterCandidates.front();")
if restore < scan_end:
    raise SystemExit("filter state must be committed only after the full record scan")

classification = load.index("RadiationSerialization::ClassifyFilterCandidates(filterCandidates)")
charge_commit = load.index("_filterCharge = filter.charge;")
condition_commit = load.index("_maskCondition = filter.condition;")
if not (classification < charge_commit < condition_commit):
    raise SystemExit("filter candidate classification must precede both live-state assignments")

for marker in (
    "kMaskOwnershipVersion = 1",
    "kMaskOwnershipEntryLen = sizeof(std::uint32_t) * 2",
):
    if marker not in source:
        raise SystemExit(f"MOWN pair-format compatibility changed unexpectedly: {marker}")

for marker in (
    "RadMaskOwnership::Snapshot()",
    "OpenRecord(kMaskOwnershipRecord, kMaskOwnershipVersion)",
    "entry.actorFormID",
    "entry.maskFormID",
):
    if marker not in save:
        raise SystemExit(f"missing mask ownership save contract: {marker}")

for marker in (
    "std::vector<std::vector<RadMaskOwnership::Entry>> ownershipCandidates;",
    "count > RadMaskOwnership::kMaxEntries",
    "RadMaskOwnership::IsValidSnapshot(candidate)",
    "ownershipCandidates.push_back(std::move(candidate));",
    "RadMaskOwnership::ClassifyCandidates(ownershipCandidates)",
    "a_intfc->ResolveFormID(entry.actorFormID)",
    "a_intfc->ResolveFormID(entry.maskFormID)",
    "RadMaskOwnership::Restore(resolved)",
    "conflicting valid mask-ownership records",
):
    if marker not in load:
        raise SystemExit(f"missing mask ownership load contract: {marker}")

ownership_record = load.split("if (type == kMaskOwnershipRecord)", 1)[1].split("if (type == kFilterRecord)", 1)[0]
for forbidden in ("RadMaskOwnership::Restore", "_autoMaskEquipped", "_autoMaskedNpcs"):
    if forbidden in ownership_record:
        raise SystemExit("mask ownership records must be collected before live state is committed")

ownership_classification = load.index("RadMaskOwnership::ClassifyCandidates(ownershipCandidates)")
ownership_restore = load.index("RadMaskOwnership::Restore(resolved)")
if ownership_restore < ownership_classification:
    raise SystemExit("mask ownership classification must precede live ownership restore")

for marker in (
    "kMaxOwnedActors = 128",
    "kMaxDisplacedGear = 4",
    "kTemporaryMaskFlag = 1u",
    "kTemporaryNpcMaskFormID = 0x001184C1",
    "kTemporaryMarkerFormID = 0x0002CB72",
    "HasLogicalMetadata",
    "IsValidLogicalEntry",
    "entry.maskFormID != kTemporaryNpcMaskFormID",
    "2 + kMaxDisplacedGear",
    "hasMask",
    "hasMarker",
):
    if marker not in ownership_header:
        raise SystemExit(f"missing encoded temporary ownership policy: {marker}")

for marker in (
    "serialized.push_back({ entry.actorFormID, entry.maskFormID });",
    "if (!entry.IsTemporary()) continue;",
    "serialized.push_back({ entry.actorFormID, kTemporaryMarkerFormID });",
    "std::array<std::uint32_t, kMaxDisplacedGear> displaced = entry.displacedFormIDs;",
    "hasMask |= entries[a].maskFormID == kTemporaryNpcMaskFormID;",
    "hasMarker |= entries[a].maskFormID == kTemporaryMarkerFormID;",
    "Entry restored{ entries[i].actorFormID, kTemporaryNpcMaskFormID, kTemporaryMaskFlag }",
    "restored.displacedFormIDs = displaced;",
    "g_ownedMasks.size() >= kMaxOwnedActors",
):
    if marker not in ownership_source:
        raise SystemExit(f"missing temporary ownership pair encoding/restore: {marker}")

snapshot_start = ownership_source.index("std::vector<RadMaskOwnership::Entry> RadMaskOwnership::Snapshot()")
snapshot_end = ownership_source.index("bool RadMaskOwnership::Restore", snapshot_start)
snapshot = ownership_source[snapshot_start:snapshot_end]
if snapshot.count("serialized.push_back({ entry.actorFormID, entry.maskFormID });") != 1:
    raise SystemExit("temporary MOWN encoding must not duplicate legacy mask ownership pairs")
if "serialized.push_back({ entry.actorFormID, kTemporaryMarkerFormID });" not in snapshot:
    raise SystemExit("temporary MOWN encoding must carry the fixed marker form")

if "RadMaskOwnership::Clear();" not in revert:
    raise SystemExit("mask ownership must clear on serialization revert")

for marker in (
    "const auto ownership = RadMaskOwnership::Snapshot();",
    "_wasInRadWeather = 1;",
    "_autoMaskEquipped = true;",
    "_autoMaskedNpcs.insert(entry.actorFormID);",
):
    if marker not in postload:
        raise SystemExit(f"missing post-load ownership reconstruction: {marker}")

for marker in (
    '#include "RadMaskOwnership.h"',
    "if (!RadMaskOwnership::Activate())",
    "RadMaskOwnership::Clear();",
):
    if marker not in lifecycle:
        raise SystemExit(f"missing main-thread mask ownership activation: {marker}")

main_thread_guard = lifecycle.index("if (!Hooks::IsGameMainThread())")
activation = lifecycle.index("if (!RadMaskOwnership::Activate())")
if activation < main_thread_guard:
    raise SystemExit("mask ownership event sink must activate only after the game-main-thread guard")

set_actor_mask = mask_source.split("void RadiationEngine::SetActorMaskEquipped", 1)[1].split("void RadiationEngine::SetMaskEquipped", 1)[0]
for marker in (
    "RadMaskOwnership::Claim(actorID, maskID)",
    "if (!RadMaskOwnership::Owns(actorID, maskID)) return;",
    "RadMaskOwnership::Release(actorID)",
):
    if marker not in set_actor_mask:
        raise SystemExit(f"missing exact-mask equip ownership gate: {marker}")

npc_policy = mask_source.split("void RadiationEngine::EquipNearbyNpcMasks", 1)[1].split(
    "// ----------------------------------------------------------------------------- gas-mask filter", 1
)[0]
for marker in (
    "RadMaskOwnership::Lookup(id)",
    "RadMaskOwnership::ClaimTemporary",
    "entry.displacedFormIDs",
    "actor->AddObjectToContainer(tempMask",
    "RE::TESObjectREFR::RemoveItemData data(tempMask, 1)",
    "cleanupEntry(actor, *entry)",
):
    if marker not in npc_policy:
        raise SystemExit(f"missing temporary NPC mask ownership contract: {marker}")

for marker in (
    "event.changeType != RE::ActorEquipManagerEvent::Type::kUnequip",
    "const auto actorID = event.actorAffected->GetFormID();",
    "const auto maskID = event.itemAffected->object->GetFormID();",
    "it->second.maskFormID == maskID",
    "!it->second.IsTemporary()",
):
    if marker not in ownership_source:
        raise SystemExit(f"missing manual-unequip ownership release contract: {marker}")

print("filter and exact auto-mask ownership serialization policy verified")
