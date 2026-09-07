from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / "src" / "RadiationEngine.cpp").read_text(encoding="utf-8")
lifecycle = (root / "src" / "RadLifecycle.cpp").read_text(encoding="utf-8")
mask_source = (root / "src" / "RadEngine_Mask.cpp").read_text(encoding="utf-8")
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

npc_cleanup = mask_source.split("if (!equip)", 1)[1].split("auto* pl = RE::ProcessLists::GetSingleton();", 1)[0]
for marker in (
    "const auto maskID = worn->GetFormID();",
    "const bool owned = RadMaskOwnership::Owns(id, maskID);",
    "if (RadMaskOwnership::Owns(id, maskID)) pending.insert(id);",
):
    if marker not in npc_cleanup:
        raise SystemExit(f"missing exact NPC mask cleanup ownership gate: {marker}")

for marker in (
    "event.changeType != RE::ActorEquipManagerEvent::Type::kUnequip",
    "const auto actorID = event.actorAffected->GetFormID();",
    "const auto maskID = event.itemAffected->object->GetFormID();",
    "it->second == maskID",
):
    if marker not in ownership_source:
        raise SystemExit(f"missing manual-unequip ownership release contract: {marker}")

print("filter and exact auto-mask ownership serialization policy verified")
