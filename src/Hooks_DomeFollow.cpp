#include "pch.h"
#include "Hooks.h"
#include "RadiationEngine.h"
#include "RadLifecycleDispatch.h"
#include "RadCorrectnessPolicy.h"

#include <RE/P/PlayerCharacter.h>

#include <atomic>
#include <chrono>
#include <mutex>

namespace Hooks
{
    static std::atomic_bool s_domeFollowActive{ false };
    static std::atomic_bool s_stormImadFadeActive{ false };
    static std::atomic_bool s_domeFollowTaskQueued{ false };

    void SetDomeFollowActive(bool active) noexcept
    {
        s_domeFollowActive.store(active, std::memory_order_release);
    }

    void SetStormIMADFadeActive(bool active) noexcept
    {
        s_stormImadFadeActive.store(active, std::memory_order_release);
    }

    void QueueDomeFollow()
    {
        if (!s_domeFollowActive.load(std::memory_order_acquire) &&
            !s_stormImadFadeActive.load(std::memory_order_acquire)) return;

        const auto generation = RadLifecycleDispatch::ReadyPlayerUpdateGeneration();
        if (generation == 0) return;

        static constexpr auto kImadFadeInterval = std::chrono::milliseconds(33);
        static std::mutex throttleMutex;
        static std::chrono::steady_clock::time_point lastImadFade;
        static bool imadFadeInitialized = false;
        const auto now = std::chrono::steady_clock::now();
        const bool runImadFade = [&] {
            std::lock_guard<std::mutex> lock(throttleMutex);
            if (imadFadeInitialized && now - lastImadFade < kImadFadeInterval) return false;
            lastImadFade = now;
            imadFadeInitialized = true;
            return true;
        }();

        // Constraint: kFollowSkipEpsilon must equal kStormDomeFollowEpsilon (RadEngine_Internal.h); IMAD ticks keep DriveDomeAnimation running.
        static constexpr float kFollowSkipEpsilon = 64.0f;
        static std::mutex followMutex;
        static bool followPosValid = false;
        static RE::NiPoint3 lastFollowPos{};
        auto* player = RE::PlayerCharacter::GetSingleton();
        const RE::NiPoint3 playerPos = player ? player->GetPosition() : RE::NiPoint3{};
        const bool movedBeyondEpsilon = [&] {
            std::lock_guard<std::mutex> lock(followMutex);
            if (!followPosValid) return true;
            const float dx = playerPos.x - lastFollowPos.x;
            const float dy = playerPos.y - lastFollowPos.y;
            const float dz = playerPos.z - lastFollowPos.z;
            return RadCorrectnessPolicy::ShouldMoveDomeLayer((dx * dx) + (dy * dy) + (dz * dz),
                kFollowSkipEpsilon * kFollowSkipEpsilon);
        }();
        if (!runImadFade && !movedBeyondEpsilon)
            return;

        bool expected = false;
        if (!s_domeFollowTaskQueued.compare_exchange_strong(expected, true))
            return;

        bool queued = false;
        try
        {
            queued = QueueMainThreadTask([generation, runImadFade]() {
                try
                {
                    if (RadLifecycleDispatch::PlayerUpdatesReady(generation))
                    {
                        RadiationEngine::GetSingleton().UpdateStormDomeFollow();
                        if (runImadFade)
                            RadiationEngine::GetSingleton().UpdateStormIMADFade();
                    }
                }
                catch (...)
                {
                    s_domeFollowTaskQueued.store(false);
                    throw;
                }
                s_domeFollowTaskQueued.store(false);
            });
        }
        catch (...)
        {
            s_domeFollowTaskQueued.store(false);
            throw;
        }
        if (!queued)
        {
            s_domeFollowTaskQueued.store(false);
            return;
        }
        std::lock_guard<std::mutex> lock(followMutex);
        followPosValid = true;
        lastFollowPos = playerPos;
    }

}
