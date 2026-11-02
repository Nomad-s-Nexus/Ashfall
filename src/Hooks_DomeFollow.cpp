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
            s_domeFollowTaskQueued.store(false);
    }

}
