#include "RadCorrectnessPolicy.h"

#include <atomic>
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
    // Exterior parent-cell changes are normal streaming inside one worldspace. Keep the existing
    // dome references alive so the follow path moves them without a destructive clear/respawn snap.
    Require(!ShouldRecreateDomeForOwner(true, false, true));
    Require(ShouldRecreateDomeForOwner(true, true, false));
    Require(ShouldRecreateDomeForOwner(true, false, false));
    Require(!ShouldRetryDomeSpawn(false, true, false, 4));
    Require(!ShouldRetryDomeSpawn(true, false, false, 4));
    Require(!ShouldRetryDomeSpawn(true, true, true, 4));
    Require(!ShouldRetryDomeSpawn(true, true, false, 3));
    Require(ShouldRetryDomeSpawn(true, true, false, 4));
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

    // Load/new-game state mutation is legal only when both activation gates succeeded.
    Require(!IsEngineActive(false, false));
    Require(!IsEngineActive(true, false));
    Require(!IsEngineActive(false, true));
    Require(IsEngineActive(true, true));

    Require(ShouldStartHudWait(false, 42, 42));
    Require(!ShouldStartHudWait(true, 42, 42));
    Require(!ShouldStartHudWait(false, 42, 43));
    Require(IsCurrentLoadGeneration(42, 42));
    Require(!IsCurrentLoadGeneration(42, 43));

    std::atomic<std::uint64_t> newGameGeneration{ 41 };
    std::atomic<bool> newGameHudWaiting{ true };
    Require(BeginNewGameLoad(newGameGeneration, newGameHudWaiting) == 42);
    Require(!newGameHudWaiting.load());
}
