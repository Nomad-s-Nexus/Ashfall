#pragma once

#include <algorithm>
#include <cmath>

namespace RadNeedsPolicy
{
    // Constraint: RadSurvival.esp owns these pools; this side only reads them for the HUD.
    struct NeedsState
    {
        float hunger01{ 0.0f };
        float thirst01{ 0.0f };
        float sleepDebt01{ 0.0f };
    };

    struct NeedsSnapshot
    {
        NeedsState pools{};
        float risk01{ 0.0f };
        int diseasePacked{ 0 };
    };

    inline constexpr int kDiseaseNone = 0;
    inline constexpr int kDiseaseMaxId = 2;
    inline constexpr int kDiseaseMaxStage = 3;
    inline constexpr int kDiseasePackedMax = kDiseaseMaxId * 10 + kDiseaseMaxStage;

    [[nodiscard]] inline float Clamp01(float value) noexcept
    {
        if (!std::isfinite(value))
            return 0.0f;
        return std::clamp(value, 0.0f, 1.0f);
    }

    [[nodiscard]] inline NeedsState Sanitize(NeedsState state) noexcept
    {
        state.hunger01 = Clamp01(state.hunger01);
        state.thirst01 = Clamp01(state.thirst01);
        state.sleepDebt01 = Clamp01(state.sleepDebt01);
        return state;
    }

    // Constraint: the disease GLOB packs DiseaseId * 10 + Stage, so an out-of-range read is discarded.
    [[nodiscard]] inline NeedsSnapshot Sanitize(NeedsSnapshot snapshot) noexcept
    {
        snapshot.pools = Sanitize(snapshot.pools);
        snapshot.risk01 = Clamp01(snapshot.risk01);
        if (snapshot.diseasePacked < kDiseaseNone || snapshot.diseasePacked > kDiseasePackedMax)
            snapshot.diseasePacked = kDiseaseNone;
        return snapshot;
    }
}
