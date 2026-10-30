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
    Require(FindKey("DrinkScale") == static_cast<int>(kCount) - 1);
    Require(FindKey("hourlyhunger") == -1);
    Require(FindKey("") == -1);

    float out = 0.0f;
    Require(Accept(1, 0.03f, out) && Near(out, 0.03f));
    Require(Accept(1, 9.0f, out) && Near(out, 0.5f));
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
        Require(spec.min <= spec.reference && spec.reference <= spec.max);

    return 0;
}
