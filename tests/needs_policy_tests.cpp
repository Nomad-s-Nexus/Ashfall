#include "NeedsPolicy.h"
#include "NeedsSerializationPolicy.h"

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

    HourlyRates rates{};
    rates.hungerPerHour = 0.05f;
    rates.thirstPerHour = 0.10f;
    rates.sleepDebtPerHour = 0.025f;

    const auto advanced = Advance({}, 2.0f, rates);
    Require(Near(advanced.hunger01, 0.10f));
    Require(Near(advanced.thirst01, 0.20f));
    Require(Near(advanced.sleepDebt01, 0.05f));

    const auto noBackwardTime = Advance(advanced, -8.0f, rates);
    Require(Near(noBackwardTime.hunger01, advanced.hunger01));
    Require(Near(noBackwardTime.thirst01, advanced.thirst01));
    Require(Near(noBackwardTime.sleepDebt01, advanced.sleepDebt01));

    const auto saturated = Advance({}, 1000.0f, rates);
    Require(Near(saturated.hunger01, 1.0f));
    Require(Near(saturated.thirst01, 1.0f));
    Require(Near(saturated.sleepDebt01, 1.0f));

    Relief relief{};
    relief.hunger01 = 0.04f;
    relief.thirst01 = 0.15f;
    relief.sleepDebt01 = 0.02f;
    const auto relieved = ApplyRelief(advanced, relief);
    Require(Near(relieved.hunger01, 0.06f));
    Require(Near(relieved.thirst01, 0.05f));
    Require(Near(relieved.sleepDebt01, 0.03f));

    const NeedsState invalid{
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -5.0f
    };
    const auto sanitized = Sanitize(invalid);
    Require(Near(sanitized.hunger01, 0.0f));
    Require(Near(sanitized.thirst01, 0.0f));
    Require(Near(sanitized.sleepDebt01, 0.0f));

    using namespace RadNeedsSerialization;
    const auto saved = Snapshot(advanced);
    Require(IsValid(saved));
    const auto restored = Restore(saved);
    Require(Near(restored.hunger01, advanced.hunger01));
    Require(Near(restored.thirst01, advanced.thirst01));
    Require(Near(restored.sleepDebt01, advanced.sleepDebt01));

    PersistedState badSave{};
    badSave.hunger01 = 2.0f;
    Require(!IsValid(badSave));
    const auto fallback = Restore(badSave);
    Require(Near(fallback.hunger01, 0.0f));
    Require(Near(fallback.thirst01, 0.0f));
    Require(Near(fallback.sleepDebt01, 0.0f));

    Require(kVersion == 1);
    return 0;
}
