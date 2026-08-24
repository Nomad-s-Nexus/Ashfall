#include "SerializationPolicy.h"

#include <cassert>
#include <limits>

int main()
{
	using namespace RadiationSerialization;

	const StormClockState longRunningSave{ 1'000'001.0, 1'000'601.0, kNoPreviousVanillaRadstorm, true };
	assert(IsValidStormClock(longRunningSave));

	const StormClockState overdueStorm{ 1200.0, 600.0, 900.0, true };
	assert(IsValidStormClock(overdueStorm));

	const StormClockState impossibleLastStorm{ 1200.0, 1800.0, 1201.0, true };
	assert(!IsValidStormClock(impossibleLastStorm));

	const StormClockState nonFinite{ std::numeric_limits<double>::infinity(), 1800.0, 0.0, true };
	assert(!IsValidStormClock(nonFinite));

	const StormClockState candidate{ 1200.0, 1800.0, 900.0, true };
	assert(ClassifyStormClockCandidates({ candidate }) == StormClockCandidateDisposition::kRestore);
	assert(ClassifyStormClockCandidates({ candidate, candidate }) == StormClockCandidateDisposition::kRestore);
	assert(ClassifyStormClockCandidates({ candidate, overdueStorm }) == StormClockCandidateDisposition::kAmbiguous);
}
