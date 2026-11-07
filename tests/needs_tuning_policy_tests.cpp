#include "RadNeedsTuningPolicy.h"

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
    using namespace RadNeedsTuningPolicy;

    Require(FindKey("RateMultiplier") == 0);
    Require(FindKey("HourlyHunger") == 1);
    Require(FindKey("DrinkScale") == static_cast<int>(kDrinkScale));
    Require(FindKey("SleepRecovery") == static_cast<int>(kSleepRecovery));
    Require(FindKey("RiskContaminated") == static_cast<int>(kPapyrusKeys) - 1);
    Require(FindKey("FilterEnabled") == static_cast<int>(kPapyrusKeys));
    Require(FindKey("AutoEquipMask") == static_cast<int>(kAutoEquipMask));
    Require(FindKey("AutoEquipInCombat") == static_cast<int>(kAutoEquipInCombat));
    Require(FindKey("AlwaysShowNeeds") == static_cast<int>(kAlwaysShowNeeds));
    Require(FindKey("WaterRequireSneak") == static_cast<int>(kWaterRequireSneak));
    Require(FindKey("WaterRequireSneak") == static_cast<int>(kCount) - 1);
    Require(kCount == kKeyCount);
    Require(FindKey("hourlyhunger") == -1);
    Require(FindKey("") == -1);

    float out = 0.0f;
    Require(Accept(1, 0.03f, out) && Near(out, 0.03f));
    Require(Accept(1, 9.0f, out) && Near(out, 0.5f));
    Require(Accept(kDiseasesEnabled, 0.7f, out) && Near(out, 1.0f));
    Require(Accept(kDiseasesEnabled, 0.2f, out) && Near(out, 0.0f));
    Require(Accept(kWetnessDryRate, 0.0f, out) && Near(out, 0.1f));
    Require(Accept(kStormMaxMinutes, 9999.0f, out) && Near(out, 480.0f));
    Require(Accept(1, -1.0f, out) && Near(out, 0.0f));
    Require(!Accept(1, std::numeric_limits<float>::quiet_NaN(), out));
    Require(!Accept(1, std::numeric_limits<float>::infinity(), out));
    Require(!Accept(kCount, 0.1f, out));

    Require(ParseValue("0.0300", out) && Near(out, 0.03f));
    Require(ParseValue("  1 ", out) && Near(out, 1.0f));
    Require(ParseValue("-0.5", out) && Near(out, -0.5f));
    Require(!ParseValue("", out));
    Require(!ParseValue("   ", out));
    Require(!ParseValue("0.1abc", out));
    Require(!ParseValue("nan", out));
    Require(!ParseValue("inf", out));
    Require(!ParseValue("1e3", out));
    Require(!ParseValue("0x10", out));
    Require(!ParseValue("1.2.3", out));

    for (const auto& spec : kSpecs)
    {
        Require(spec.min <= spec.reference && spec.reference <= spec.max);
        Require(!spec.toggle || (spec.min == 0.0f && spec.max == 1.0f));
        Require(FindKey(spec.key) >= 0 && kSpecs[static_cast<std::size_t>(FindKey(spec.key))].key == spec.key);
    }

    return 0;
}
