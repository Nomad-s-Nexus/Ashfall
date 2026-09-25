#include "NeedsTracePolicy.h"

#include <cstdlib>
#include <limits>

namespace
{
    void Require(bool condition)
    {
        if (!condition)
            std::abort();
    }
}

int main()
{
    using namespace RadNeedsTracePolicy;
    constexpr std::uint32_t player = 0x14;

    Require(ClassifyContainerChange(player, 0, player) == ContainerDirection::kLeftPlayer);
    Require(ClassifyContainerChange(player, 0x1234, player) == ContainerDirection::kLeftPlayer);
    Require(ClassifyContainerChange(0x1234, player, player) == ContainerDirection::kEnteredPlayer);
    Require(ClassifyContainerChange(0, player, player) == ContainerDirection::kEnteredPlayer);
    Require(ClassifyContainerChange(player, player, player) == ContainerDirection::kWithinPlayer);
    Require(ClassifyContainerChange(0x1234, 0x5678, player) == ContainerDirection::kUnrelated);
    Require(ClassifyContainerChange(0, 0, 0) == ContainerDirection::kUnrelated);
    Require(ClassifyContainerChange(0, 0x1234, 0) == ContainerDirection::kUnrelated);

    Require(!IsTraceEnabledSetting(0));
    Require(IsTraceEnabledSetting(1));
    Require(!IsTraceEnabledSetting(2));
    Require(!IsTraceEnabledSetting(-1));

    Require(CountDelta(3, 2) == -1);
    Require(CountDelta(0, 5) == 5);
    Require(CountDelta(std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()) ==
            std::numeric_limits<std::int32_t>::max());
    Require(CountDelta(std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int32_t>::min()) ==
            std::numeric_limits<std::int32_t>::min());
    return 0;
}
