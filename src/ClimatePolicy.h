#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace RadClimatePolicy
{
    inline constexpr float kNormalCoreC = 37.0f;
    inline constexpr float kMinimumCoreC = 28.0f;
    inline constexpr float kMaximumCoreC = 37.0f;
    inline constexpr float kNeutralAmbientC = 20.0f;
    inline constexpr float kMaximumStepSeconds = 5.0f;
    inline constexpr float kTimeJumpThresholdDays = 0.01f;
    inline constexpr float kMaximumTimeJumpHours = 24.0f;

    enum class HypothermiaStage : std::uint8_t
    {
        kNormal = 0,
        kChilled,
        kMild,
        kModerate,
        kSevere,
        kCritical
    };

    struct EnvironmentSample
    {
        float ambientC{ 12.0f };
        float windMps{ 0.0f };
        float precipitation01{ 0.0f };
        bool submerged{ false };
        bool indoors{ false };
        float shelter01{ 0.0f };
        float precipitationShelter01{ 0.0f };
        float heat01{ 0.0f };
        float activity01{ 0.0f };
    };

    struct ProtectionSample
    {
        float insulation01{ 0.0f };
        bool powerArmor{ false };
    };

    struct ClimateState
    {
        float coreC{ kNormalCoreC };
        float wetness01{ 0.0f };
        HypothermiaStage stage{ HypothermiaStage::kNormal };
    };

    struct StageEffects
    {
        float movementMultiplier{ 1.0f };
        float apRegenMultiplier{ 1.0f };
        float weaponHandlingMultiplier{ 1.0f };
        float healthDamagePerSecond{ 0.0f };
        std::int8_t strengthDelta{ 0 };
        std::int8_t enduranceDelta{ 0 };
        std::int8_t agilityDelta{ 0 };
    };

    struct StepResult
    {
        ClimateState state{};
        float effectiveInsulation01{ 0.0f };
        float coolingCPerSecond{ 0.0f };
        float warmingCPerSecond{ 0.0f };
    };

    [[nodiscard]] inline float Clamp01(float value) noexcept
    {
        if (!std::isfinite(value))
            return 0.0f;
        return std::clamp(value, 0.0f, 1.0f);
    }

    [[nodiscard]] inline float BodyWarmth01(float coreC) noexcept
    {
        return Clamp01((coreC - kMinimumCoreC) / (kNormalCoreC - kMinimumCoreC));
    }

    [[nodiscard]] inline HypothermiaStage StageForCore(float coreC) noexcept
    {
        if (!std::isfinite(coreC) || coreC >= 36.5f)
            return HypothermiaStage::kNormal;
        if (coreC >= 35.5f)
            return HypothermiaStage::kChilled;
        if (coreC >= 34.5f)
            return HypothermiaStage::kMild;
        if (coreC >= 33.0f)
            return HypothermiaStage::kModerate;
        if (coreC >= 30.0f)
            return HypothermiaStage::kSevere;
        return HypothermiaStage::kCritical;
    }

    [[nodiscard]] inline StageEffects EffectsForStage(HypothermiaStage stage) noexcept
    {
        switch (stage)
        {
        case HypothermiaStage::kChilled:
            return { 0.98f, 0.95f, 0.97f, 0.0f, 0, 0, 0 };
        case HypothermiaStage::kMild:
            return { 0.94f, 0.85f, 0.92f, 0.0f, -1, -1, 0 };
        case HypothermiaStage::kModerate:
            return { 0.88f, 0.70f, 0.82f, 0.15f, -2, -2, -1 };
        case HypothermiaStage::kSevere:
            return { 0.75f, 0.50f, 0.65f, 0.45f, -3, -3, -2 };
        case HypothermiaStage::kCritical:
            return { 0.60f, 0.30f, 0.50f, 1.00f, -4, -4, -3 };
        case HypothermiaStage::kNormal:
        default:
            return {};
        }
    }

    [[nodiscard]] inline float EffectiveInsulation(const ProtectionSample& protection) noexcept
    {
        const float armorInsulation = Clamp01(protection.insulation01);
        const float powerArmorFloor = protection.powerArmor ? 0.85f : 0.0f;
        return std::max(armorInsulation, powerArmorFloor);
    }

    [[nodiscard]] inline ClimateState SanitizeState(ClimateState state) noexcept
    {
        if (!std::isfinite(state.coreC))
            state.coreC = kNormalCoreC;
        state.coreC = std::clamp(state.coreC, kMinimumCoreC, kMaximumCoreC);
        state.wetness01 = Clamp01(state.wetness01);
        state.stage = StageForCore(state.coreC);
        return state;
    }

    [[nodiscard]] inline float NextWetness(
        float currentWetness01,
        const EnvironmentSample& environment,
        float deltaSeconds) noexcept
    {
        const float dt = std::clamp(
            std::isfinite(deltaSeconds) ? deltaSeconds : 0.0f,
            0.0f,
            kMaximumStepSeconds);
        float wetness = Clamp01(currentWetness01);

        if (environment.submerged)
            return Clamp01(wetness + 0.50f * dt);

        const float precipitationShelter = environment.indoors
            ? 1.0f
            : Clamp01(environment.precipitationShelter01);
        const float rainExposure =
            Clamp01(environment.precipitation01) * (1.0f - precipitationShelter);
        const float rainGainPerSecond = 0.0045f * rainExposure;
        const float dryPerSecond =
            0.0012f *
            (environment.indoors ? 2.5f : 1.0f) *
            (1.0f + 4.0f * Clamp01(environment.heat01)) *
            (1.0f + 0.5f * Clamp01(environment.activity01));

        return Clamp01(wetness + (rainGainPerSecond - dryPerSecond) * dt);
    }

    [[nodiscard]] inline StepResult Step(
        ClimateState state,
        const EnvironmentSample& environment,
        const ProtectionSample& protection,
        float deltaSeconds) noexcept
    {
        state = SanitizeState(state);
        const float dt = std::clamp(
            std::isfinite(deltaSeconds) ? deltaSeconds : 0.0f,
            0.0f,
            kMaximumStepSeconds);

        const float wetness = NextWetness(state.wetness01, environment, dt);
        const float shelter = environment.indoors ? 1.0f : Clamp01(environment.shelter01);
        const float insulation = EffectiveInsulation(protection);
        const float ambientC = std::isfinite(environment.ambientC) ? environment.ambientC : kNeutralAmbientC;
        const float windMps = std::isfinite(environment.windMps) ? std::max(0.0f, environment.windMps) : 0.0f;
        const float coldSeverity = std::clamp((kNeutralAmbientC - ambientC) / 40.0f, 0.0f, 1.5f);
        const float windExposure = Clamp01(windMps / 20.0f) * (1.0f - shelter);
        const float windFactor = 1.0f + 0.75f * windExposure;
        const float wetnessFactor = 1.0f + 1.15f * wetness;
        const float insulationFactor = std::max(0.12f, 1.0f - 0.82f * insulation);
        const float shelterFactor = std::max(0.20f, 1.0f - 0.70f * shelter);
        const float coolingCPerSecond =
            0.0040f *
            coldSeverity *
            windFactor *
            wetnessFactor *
            insulationFactor *
            shelterFactor;

        const float heatWarming = 0.0065f * Clamp01(environment.heat01);
        const float activityWarming = 0.0015f * Clamp01(environment.activity01);
        const float warmAmbientWarming =
            0.0015f * std::clamp((ambientC - kNeutralAmbientC) / 20.0f, 0.0f, 1.0f);
        const float homeostasisWarming =
            std::max(0.0f, kNormalCoreC - state.coreC) *
            0.0010f *
            (0.5f + 0.5f * shelter);
        const float warmingCPerSecond =
            heatWarming +
            activityWarming +
            warmAmbientWarming +
            homeostasisWarming;

        state.coreC = std::clamp(
            state.coreC + (warmingCPerSecond - coolingCPerSecond) * dt,
            kMinimumCoreC,
            kMaximumCoreC);
        state.wetness01 = wetness;
        state.stage = StageForCore(state.coreC);

        return { state, insulation, coolingCPerSecond, warmingCPerSecond };
    }

    [[nodiscard]] inline StepResult StepTimeJump(
        ClimateState state,
        const EnvironmentSample& environment,
        const ProtectionSample& protection,
        float totalSeconds,
        float substepSeconds = kMaximumStepSeconds) noexcept
    {
        state = SanitizeState(state);
        const float maxDuration = kMaximumTimeJumpHours * 3600.0f;
        float remaining = std::clamp(
            std::isfinite(totalSeconds) ? totalSeconds : 0.0f,
            0.0f,
            maxDuration);
        const float dt = std::clamp(
            std::isfinite(substepSeconds) && substepSeconds > 0.0f ? substepSeconds : kMaximumStepSeconds,
            0.1f,
            kMaximumStepSeconds);

        StepResult current{ state, EffectiveInsulation(protection), 0.0f, 0.0f };
        while (remaining > 0.0f)
        {
            const float stepDt = std::min(remaining, dt);
            current = Step(current.state, environment, protection, stepDt);
            remaining -= stepDt;
        }
        return current;
    }

    [[nodiscard]] inline float ElapsedGameSeconds(float gameDaysElapsed) noexcept
    {
        if (!std::isfinite(gameDaysElapsed) || gameDaysElapsed <= 0.0f)
            return 0.0f;
        return gameDaysElapsed * 86400.0f;
    }
}
