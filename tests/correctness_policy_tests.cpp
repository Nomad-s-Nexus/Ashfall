#include "RadCorrectnessPolicy.h"
#include "ClimatePolicy.h"

#include <atomic>
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

    bool Near(float lhs, float rhs, float epsilon = 0.001f)
    {
        return std::abs(lhs - rhs) <= epsilon;
    }

    RadClimatePolicy::ClimateState AdvanceClimate(
        RadClimatePolicy::ClimateState state,
        const RadClimatePolicy::EnvironmentSample& environment,
        const RadClimatePolicy::ProtectionSample& protection,
        int seconds)
    {
        for (int i = 0; i < seconds; ++i)
            state = RadClimatePolicy::Step(state, environment, protection, 1.0f).state;
        return state;
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

    using namespace RadClimatePolicy;

    Require(StageForCore(37.0f) == HypothermiaStage::kNormal);
    Require(StageForCore(36.5f) == HypothermiaStage::kNormal);
    Require(StageForCore(36.49f) == HypothermiaStage::kChilled);
    Require(StageForCore(35.5f) == HypothermiaStage::kChilled);
    Require(StageForCore(35.49f) == HypothermiaStage::kMild);
    Require(StageForCore(34.49f) == HypothermiaStage::kModerate);
    Require(StageForCore(32.99f) == HypothermiaStage::kSevere);
    Require(StageForCore(29.99f) == HypothermiaStage::kCritical);

    ClimateState badState{
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        HypothermiaStage::kCritical
    };
    const auto sanitized = SanitizeState(badState);
    Require(Near(sanitized.coreC, kNormalCoreC));
    Require(Near(sanitized.wetness01, 0.0f));
    Require(sanitized.stage == HypothermiaStage::kNormal);

    EnvironmentSample heavyRain{};
    heavyRain.ambientC = 5.0f;
    heavyRain.precipitation01 = 1.0f;
    const auto rainState = AdvanceClimate({}, heavyRain, {}, 120);
    Require(rainState.wetness01 > 0.30f);

    EnvironmentSample submerged{};
    submerged.submerged = true;
    ClimateState partlyWet{};
    partlyWet.wetness01 = 0.2f;
    const auto submergedState = Step(partlyWet, submerged, {}, 2.0f).state;
    Require(Near(submergedState.wetness01, 1.0f));

    EnvironmentSample indoors{};
    indoors.ambientC = 10.0f;
    indoors.indoors = true;
    ClimateState soaked{};
    soaked.wetness01 = 1.0f;
    const auto indoorDry = AdvanceClimate(soaked, indoors, {}, 120);

    EnvironmentSample heatedIndoor = indoors;
    heatedIndoor.heat01 = 1.0f;
    const auto heatedDry = AdvanceClimate(soaked, heatedIndoor, {}, 120);
    Require(heatedDry.wetness01 < indoorDry.wetness01);
    Require(Near(heatedDry.wetness01, 0.0f));

    EnvironmentSample dryCold{};
    dryCold.ambientC = -10.0f;
    const auto dryColdState = AdvanceClimate({}, dryCold, {}, 600);

    EnvironmentSample wetWindCold = dryCold;
    wetWindCold.windMps = 18.0f;
    wetWindCold.precipitation01 = 1.0f;
    const auto wetWindState = AdvanceClimate({}, wetWindCold, {}, 600);
    Require(wetWindState.coreC < dryColdState.coreC);

    ProtectionSample insulated{};
    insulated.insulation01 = 0.8f;
    const auto insulatedState = AdvanceClimate({}, dryCold, insulated, 600);
    Require(insulatedState.coreC > dryColdState.coreC);

    ProtectionSample powerArmor{};
    powerArmor.powerArmor = true;
    const auto powerArmorState = AdvanceClimate({}, dryCold, powerArmor, 600);
    Require(powerArmorState.coreC >= insulatedState.coreC);

    ClimateState chilled{};
    chilled.coreC = 35.0f;
    chilled.wetness01 = 0.5f;
    const auto unheatedState = AdvanceClimate(chilled, dryCold, insulated, 600);

    EnvironmentSample heated = dryCold;
    heated.shelter01 = 0.5f;
    heated.heat01 = 1.0f;
    const auto heatedState = AdvanceClimate(chilled, heated, insulated, 600);
    Require(heatedState.coreC > unheatedState.coreC);
    Require(Near(heatedState.coreC, kNormalCoreC));

    EnvironmentSample extremeCold{};
    extremeCold.ambientC = -20.0f;
    extremeCold.windMps = 20.0f;
    ClimateState saturated{};
    saturated.wetness01 = 1.0f;
    const auto extremeState = AdvanceClimate(saturated, extremeCold, {}, 1800);
    Require(extremeState.stage == HypothermiaStage::kCritical);

    const auto normalEffects = EffectsForStage(HypothermiaStage::kNormal);
    const auto moderateEffects = EffectsForStage(HypothermiaStage::kModerate);
    const auto criticalEffects = EffectsForStage(HypothermiaStage::kCritical);
    Require(Near(normalEffects.movementMultiplier, 1.0f));
    Require(moderateEffects.movementMultiplier < normalEffects.movementMultiplier);
    Require(criticalEffects.movementMultiplier < moderateEffects.movementMultiplier);
    Require(criticalEffects.healthDamagePerSecond > moderateEffects.healthDamagePerSecond);
    Require(EffectsForStage(HypothermiaStage::kNormal).strengthDelta == 0);

    EnvironmentSample comfortable{};
    comfortable.ambientC = 24.0f;
    comfortable.indoors = true;
    ClimateState recovering{};
    recovering.coreC = 35.0f;
    const auto recovered = AdvanceClimate(recovering, comfortable, {}, 1800);
    Require(recovered.coreC > recovering.coreC);
    Require(recovered.coreC <= kNormalCoreC);

    return 0;
}
