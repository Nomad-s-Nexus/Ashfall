#include "RadCorrectnessPolicy.h"
#include "RadWaterFillPolicy.h"
#include "ClimatePolicy.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
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
    using namespace RadWaterFillPolicy;

    Require(DomePosition({ 10.0f, 20.0f, 30.0f }, 6000.0f) == Position{ 10.0f, 20.0f, 6030.0f });
    const auto nuke = DistantNukePlacementXY({ 123.0f, 456.0f, 789.0f }, 0.0f, 4250.0f);
    Require(nuke.strike.x == 123.0f);
    Require(nuke.strike.y == 4706.0f);
    Require(std::abs(nuke.facingYaw - 3.14159265f) < 0.001f);
    Require(ShouldReturnToClear(true, false));
    Require(ShouldReturnToClear(false, true));
    Require(!ShouldReturnToClear(false, false));
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
    Require(!StormUsesDome(1));
    Require(StormUsesDome(0));
    Require(StormUsesDome(2));
    Require(StormUsesDome(5));
    Require(IsGasMask(true, true));
    Require(!IsGasMask(true, false));
    Require(IsConfiguredMask(false, true, true));
    Require(IsConfiguredMask(true, true, true));
    Require(IsConfiguredMask(true, false, false));
    Require(IsConfiguredMask(true, true, false));
    Require(!IsConfiguredMask(false, true, false));
    Require(!IsConfiguredMask(false, false, true));
    Require(!IsConfiguredMask(false, false, false));
    Require(IsRadShieldedMask(true, true));
    Require(!IsRadShieldedMask(true, false));
    Require(!IsRadShieldedMask(false, true));
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

    // Constraint: the built-in tables must keep matching the vanilla records the code-notes document.
    Require(kEmptyBottles.size() == 26);
    Require(kWaterSources.size() == 10);
    Require(std::strcmp(kEmptyBottles[0].editorID, "NukaColaEmpty") == 0);
    Require(std::strcmp(kEmptyBottles.back().editorID, "DN050BabyBottle") == 0);
    Require(std::strcmp(kFilledWater.editorID, "WaterDirty") == 0);
    Require(kFilledWater.localID == 0x000366BF);
    Require(std::strcmp(kWaterPumpKeyword.editorID, "AnimFurnWaterPump") == 0);
    Require(kWaterPumpKeyword.localID == 0x000DCDD9);

    const std::uint32_t expectedBottles[]{
        0x0004835A, 0x000211DD, 0x000DF221, 0x000DF222, 0x000DF223, 0x000DF24D, 0x000DEDDF, 0x0014A138,
        0x000DEDE0, 0x000DEDE1, 0x000DEDED, 0x001BEAEE, 0x0003B55B, 0x0011EA9B, 0x0011EA9E, 0x0011EA9C,
        0x0011EA9D, 0x0011EAA0, 0x0011EA9F, 0x000DEDD9, 0x000DF031, 0x000DF032, 0x0017604F, 0x001A89A0,
        0x001A899B, 0x0009B4BA,
    };
    Require(sizeof(expectedBottles) / sizeof(expectedBottles[0]) == kEmptyBottles.size());
    for (std::size_t i = 0; i < kEmptyBottles.size(); ++i)
    {
        Require(kEmptyBottles[i].localID == expectedBottles[i]);
        Require(std::strcmp(kEmptyBottles[i].plugin, "Fallout4.esm") == 0);
    }

    struct ExpectedSource
    {
        const char*   plugin;
        std::uint32_t localID;
    };
    const ExpectedSource expectedSources[]{
        { "Fallout4.esm", 0x000B1CFE },
        { "Fallout4.esm", 0x0020DE62 },
        { "Fallout4.esm", 0x000C3676 },
        { "Fallout4.esm", 0x000F5D8D },
        { "Fallout4.esm", 0x001A4AA4 },
        { "Fallout4.esm", 0x000DB83B },
        { "DLCCoast.esm", 0x00039A92 },
        { "DLCworkshop03.esm", 0x0000251F },
        { "DLCworkshop03.esm", 0x00001D3D },
        { "DLCworkshop03.esm", 0x000049AD },
    };
    Require(sizeof(expectedSources) / sizeof(expectedSources[0]) == kWaterSources.size());
    for (std::size_t i = 0; i < kWaterSources.size(); ++i)
    {
        Require(kWaterSources[i].localID == expectedSources[i].localID);
        Require(std::strcmp(kWaterSources[i].plugin, expectedSources[i].plugin) == 0);
    }

    // Constraint: entries must be local IDs, because LookupForm adds the file's own compile index.
    for (const auto& ref : kEmptyBottles)
        Require(ref.localID < 0x01000000);
    for (const auto& ref : kWaterSources)
        Require(ref.localID < 0x01000000);

    for (std::size_t i = 0; i < kEmptyBottles.size(); ++i)
        for (std::size_t j = i + 1; j < kEmptyBottles.size(); ++j)
            Require(!(kEmptyBottles[i].localID == kEmptyBottles[j].localID &&
                      std::strcmp(kEmptyBottles[i].plugin, kEmptyBottles[j].plugin) == 0));
    for (std::size_t i = 0; i < kWaterSources.size(); ++i)
        for (std::size_t j = i + 1; j < kWaterSources.size(); ++j)
            Require(!(kWaterSources[i].localID == kWaterSources[j].localID &&
                      std::strcmp(kWaterSources[i].plugin, kWaterSources[j].plugin) == 0));

    const std::uint32_t resolvedSources[]{ 0x000B1CFE, 0x000DB83B };
    Require(IsWaterSource(0x000B1CFE, false, resolvedSources, 2));
    Require(!IsWaterSource(0x000F5D8D, false, resolvedSources, 2));
    // Constraint: a pump keyword alone must qualify a mod-added pump that has no table entry.
    Require(IsWaterSource(0x000F5D8D, true, resolvedSources, 2));
    Require(IsWaterSource(0x000F5D8D, true, nullptr, 0));
    Require(!IsWaterSource(0x000B1CFE, false, nullptr, 0));
    Require(!IsWaterSource(0, false, resolvedSources, 2));

    const std::uint32_t noBottles[]{ 0, 0, 0 };
    const std::uint32_t someBottles[]{ 0, 2, 1 };
    Require(FirstAvailableBottle(noBottles, 3) == kBottleNotFound);
    Require(FirstAvailableBottle(someBottles, 3) == 1);
    Require(FirstAvailableBottle(nullptr, 3) == kBottleNotFound);

    Require(Decide(true, false, true, 1) == Decision::kFill);
    Require(Decide(false, false, true, 1) == Decision::kDisabled);
    // Constraint: vanilla Survival fills the same bottle, so the native path must yield on difficulty 6.
    Require(Decide(true, true, true, 1) == Decision::kVanillaSurvival);
    Require(Decide(true, false, false, 1) == Decision::kNotWaterSource);
    Require(Decide(true, false, true, 0) == Decision::kNoEmptyBottle);
    Require(Decide(true, false, false, 0) == Decision::kNotWaterSource);

    return 0;
}
