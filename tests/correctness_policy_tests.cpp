#include "RadCorrectnessPolicy.h"

#include <cstdlib>

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
    using namespace RadCorrectnessPolicy;

    Require(DomePosition({ 10.0f, 20.0f, 30.0f }, 6000.0f) == Position{ 10.0f, 20.0f, 6030.0f });
    Require(!ShouldFollowDomeOnTick(3));
    Require(ShouldFollowDomeOnTick(4));
    Require(!ShouldRecreateDomeForOwner(false, false, false));
    Require(!ShouldRecreateDomeForOwner(true, true, true));
    Require(ShouldRecreateDomeForOwner(true, false, true));
    Require(ShouldRecreateDomeForOwner(true, true, false));
    Require(IsGasMask(true, true));
    Require(!IsGasMask(true, false));
    Require(!CuresPermanentTissue(CureSignal::kNone));
    Require(!CuresPermanentTissue(CureSignal::kInventoryMovement));
    Require(CuresPermanentTissue(CureSignal::kVerifiedChelationUse));
    Require(!CuresPermanentTissue(CureSignal::kVerifiedRadAwayUse));
    Require(!ArmsRadAwayHealing(CureSignal::kNone));
    Require(!ArmsRadAwayHealing(CureSignal::kInventoryMovement));
    Require(!ArmsRadAwayHealing(CureSignal::kVerifiedChelationUse));
    Require(ArmsRadAwayHealing(CureSignal::kVerifiedRadAwayUse));
    Require(IsCurrentLoadGeneration(42, 42));
    Require(!IsCurrentLoadGeneration(42, 43));
}
