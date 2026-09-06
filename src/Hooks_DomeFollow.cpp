#include "pch.h"
#include "Hooks.h"
#include "RadiationEngine.h"
#include "RadLifecycleDispatch.h"

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

        static constexpr auto kDomeFollowInterval = std::chrono::milliseconds(33);
        static std::mutex throttleMutex;
        static std::chrono::steady_clock::time_point lastFollow;
        static bool throttleInitialized = false;
        const auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(throttleMutex);
            if (throttleInitialized && now - lastFollow < kDomeFollowInterval) return;
            lastFollow = now;
            throttleInitialized = true;
        }

        bool expected = false;
        if (!s_domeFollowTaskQueued.compare_exchange_strong(expected, true))
            return;

        bool queued = false;
        try
        {
            queued = QueueMainThreadTask([generation]() {
                try
                {
                    if (RadLifecycleDispatch::PlayerUpdatesReady(generation))
                    {
                        RadiationEngine::GetSingleton().UpdateStormDomeFollow();
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
            s_domeFollowTaskQueued.store(false);
    }

}
