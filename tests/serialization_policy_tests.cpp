#include "SerializationPolicy.h"
#include "RadMaskOwnership.h"

#include <cstdlib>
#include <limits>

namespace
{
    void Require(bool a_condition)
    {
        if (!a_condition)
            std::abort();
    }
}

int main()
{
	using namespace RadiationSerialization;

	const StormClockState longRunningSave{ 1'000'001.0, 1'000'601.0, kNoPreviousVanillaRadstorm, true };
	Require(IsValidStormClock(longRunningSave));

	const StormClockState overdueStorm{ 1200.0, 600.0, 900.0, true };
	Require(IsValidStormClock(overdueStorm));

	const StormClockState impossibleLastStorm{ 1200.0, 1800.0, 1201.0, true };
	Require(!IsValidStormClock(impossibleLastStorm));

	const StormClockState nonFinite{ std::numeric_limits<double>::infinity(), 1800.0, 0.0, true };
	Require(!IsValidStormClock(nonFinite));

	const StormClockState nanClock{ std::numeric_limits<double>::quiet_NaN(), 1800.0, 0.0, true };
	Require(!IsValidStormClock(nanClock));

	const StormClockState negativeLastStorm{ 1200.0, 1800.0, -1.0, true };
	Require(!IsValidStormClock(negativeLastStorm));

	const StormClockState sentinelLastStorm{ 1200.0, 1800.0, kNoPreviousVanillaRadstorm, true };
	Require(IsValidStormClock(sentinelLastStorm));

	const StormClockState negativeScheduledNext{ 1200.0, -1.0, 0.0, true };
	Require(!IsValidStormClock(negativeScheduledNext));

	const StormClockState candidate{ 1200.0, 1800.0, 900.0, true };
	Require(ClassifyStormClockCandidates({}) == StormClockCandidateDisposition::kAmbiguous);
	Require(ClassifyStormClockCandidates({ candidate }) == StormClockCandidateDisposition::kRestore);
	Require(ClassifyStormClockCandidates({ candidate, candidate }) == StormClockCandidateDisposition::kRestore);
	Require(ClassifyStormClockCandidates({ candidate, overdueStorm }) == StormClockCandidateDisposition::kAmbiguous);

	const FilterState fullFilter{ 100.0f, 100.0f };
	const FilterState wornFilter{ 35.0f, 60.0f };
	const FilterState legacyEffective{ 35.0f, 100.0f };
	Require(IsValidFilterState(fullFilter));
	Require(IsValidFilterState(wornFilter));
	Require(!IsValidFilterState({ -1.0f, 100.0f }));
	Require(!IsValidFilterState({ 101.0f, 100.0f }));
	Require(!IsValidFilterState({ 100.0f, std::numeric_limits<float>::infinity() }));
	Require(!IsValidFilterState({ std::numeric_limits<float>::quiet_NaN(), 100.0f }));
	Require(ClassifyFilterCandidates({}) == FilterCandidateDisposition::kAmbiguous);
	Require(ClassifyFilterCandidates({ wornFilter }) == FilterCandidateDisposition::kRestore);
	Require(ClassifyFilterCandidates({ wornFilter, wornFilter }) == FilterCandidateDisposition::kRestore);
	Require(ClassifyFilterCandidates({ wornFilter, fullFilter }) == FilterCandidateDisposition::kAmbiguous);
	Require(ClassifyFilterCandidates({ legacyEffective, FilterState{ 35.0f, 100.0f } }) == FilterCandidateDisposition::kRestore);
	Require(ClassifyFilterCandidates({ legacyEffective, wornFilter }) == FilterCandidateDisposition::kAmbiguous);

	using RadMaskOwnership::Entry;
	const std::vector<Entry> ownedMasks{ { 0x14, 0x800 }, { 0x1234, 0x801 } };
	const Entry temporaryMask{ 0x2000, 0x1184C1, RadMaskOwnership::kTemporaryMaskFlag, { 0x3000, 0x3001, 0, 0 } };
	Require(RadMaskOwnership::IsValidSnapshot({}));
	Require(RadMaskOwnership::IsValidSnapshot(ownedMasks));
	Require(RadMaskOwnership::IsValidSnapshot({ temporaryMask }));
	Require(!RadMaskOwnership::IsValidSnapshot({ { 0, 0x800 } }));
	Require(!RadMaskOwnership::IsValidSnapshot({ { 0x14, 0 } }));
	Require(!RadMaskOwnership::IsValidSnapshot({ { 0x1234, 0x800 }, { 0x14, 0x801 } }));
	Require(!RadMaskOwnership::IsValidSnapshot({ { 0x14, 0x800 }, { 0x14, 0x801 } }));
	Require(!RadMaskOwnership::IsValidSnapshot({ { 0x2000, 0x1184C1, 2, {} } }));
	Require(!RadMaskOwnership::IsValidSnapshot({ { 0x2000, 0x1184C1, 0, { 0x3000, 0, 0, 0 } } }));
	Require(!RadMaskOwnership::IsValidSnapshot({ { 0x2000, 0x1184C1, RadMaskOwnership::kTemporaryMaskFlag,
		{ 0x3000, 0, 0x3001, 0 } } }));
	Require(!RadMaskOwnership::IsValidSnapshot({ { 0x2000, 0x1184C1, RadMaskOwnership::kTemporaryMaskFlag,
		{ 0x3000, 0x3000, 0, 0 } } }));
	Require(!RadMaskOwnership::IsValidSnapshot({ { 0x2000, 0x1184C1, RadMaskOwnership::kTemporaryMaskFlag,
		{ 0x1184C1, 0, 0, 0 } } }));
	std::vector<Entry> tooMany;
	tooMany.reserve(RadMaskOwnership::kMaxEntries + 1);
	for (std::size_t i = 0; i <= RadMaskOwnership::kMaxEntries; ++i)
		tooMany.push_back({ static_cast<std::uint32_t>(i + 1), 0x800 });
	Require(!RadMaskOwnership::IsValidSnapshot(tooMany));
	Require(RadMaskOwnership::ClassifyCandidates({}) == RadMaskOwnership::CandidateDisposition::kAmbiguous);
	Require(RadMaskOwnership::ClassifyCandidates({ ownedMasks }) == RadMaskOwnership::CandidateDisposition::kRestore);
	Require(RadMaskOwnership::ClassifyCandidates({ ownedMasks, ownedMasks }) == RadMaskOwnership::CandidateDisposition::kRestore);
	Require(RadMaskOwnership::ClassifyCandidates({ { temporaryMask }, { temporaryMask } }) ==
		RadMaskOwnership::CandidateDisposition::kRestore);
	Require(RadMaskOwnership::ClassifyCandidates({ { temporaryMask }, { Entry{ 0x2000, 0x1184C1 } } }) ==
		RadMaskOwnership::CandidateDisposition::kAmbiguous);
	Require(RadMaskOwnership::ClassifyCandidates({ ownedMasks, { { 0x14, 0x800 } } }) ==
		RadMaskOwnership::CandidateDisposition::kAmbiguous);
}
