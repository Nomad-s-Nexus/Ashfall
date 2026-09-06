from pathlib import Path

source = (Path(__file__).resolve().parents[1] / "src" / "RadiationEngine.cpp").read_text(encoding="utf-8")
load = source.split("void RadiationEngine::LoadState", 1)[1].split("void RadiationEngine::RevertState", 1)[0]

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

print("filter serialization duplicate handling verified")
