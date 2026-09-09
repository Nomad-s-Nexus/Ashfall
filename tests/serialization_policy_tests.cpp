#include "SerializationPolicy.h"
#include "ClimateSerializationPolicy.h"

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

    using namespace RadClimateSerialization;

    const PersistedState climate{ 35.25f, 0.42f };
    const PersistedState differentCore{ 35.50f, 0.42f };
    const PersistedState differentWetness{ 35.25f, 0.43f };
    Require(IsValid(climate));
    Require(ClassifyCandidates({}) == CandidateDisposition::kAmbiguous);
    Require(ClassifyCandidates({ climate }) == CandidateDisposition::kRestore);
    Require(ClassifyCandidates({ climate, climate }) == CandidateDisposition::kRestore);
    Require(ClassifyCandidates({ climate, differentCore }) == CandidateDisposition::kAmbiguous);
    Require(ClassifyCandidates({ climate, differentWetness }) == CandidateDisposition::kAmbiguous);
}
