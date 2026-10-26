#include "NeedsPolicy.h"

#include <cmath>
#include <cstdlib>
#include <limits>

namespace
{
    void Require(bool condition)
    {
        if (!condition)
            std::abort();
    }

    bool Near(float lhs, float rhs, float epsilon = 0.0001f)
    {
        return std::abs(lhs - rhs) <= epsilon;
    }
}

int main()
{
    using namespace RadNeedsPolicy;

    // Constraint: Clamp01 is the single gate for every value read from a TESGlobal.
    Require(Near(Clamp01(0.25f), 0.25f));
    Require(Near(Clamp01(-1.0f), 0.0f));
    Require(Near(Clamp01(2.0f), 1.0f));
    Require(Near(Clamp01(std::numeric_limits<float>::quiet_NaN()), 0.0f));
    Require(Near(Clamp01(std::numeric_limits<float>::infinity()), 0.0f));

    NeedsState state;
    state.hunger01 = 1.4f;
    state.thirst01 = -0.2f;
    state.sleepDebt01 = 0.5f;
    const auto safe = Sanitize(state);
    Require(Near(safe.hunger01, 1.0f));
    Require(Near(safe.thirst01, 0.0f));
    Require(Near(safe.sleepDebt01, 0.5f));

    // Constraint: DiseaseId * 10 + Stage; anything outside the table reads as healthy.
    NeedsSnapshot snapshot;
    snapshot.pools = state;
    snapshot.risk01 = 1.8f;
    snapshot.diseasePacked = 22;
    auto clamped = Sanitize(snapshot);
    Require(Near(clamped.risk01, 1.0f));
    Require(Near(clamped.pools.hunger01, 1.0f));
    Require(clamped.diseasePacked == 22);

    snapshot.diseasePacked = 99;
    Require(Sanitize(snapshot).diseasePacked == kDiseaseNone);
    snapshot.diseasePacked = -4;
    Require(Sanitize(snapshot).diseasePacked == kDiseaseNone);
    snapshot.diseasePacked = kDiseasePackedMax;
    Require(Sanitize(snapshot).diseasePacked == kDiseasePackedMax);
    snapshot.diseasePacked = kDiseaseNone;
    Require(Sanitize(snapshot).diseasePacked == kDiseaseNone);

    return 0;
}
