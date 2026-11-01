#include "pch.h"
#include "Hooks.h"
#include "RadiationEngine.h"
#include "RadLifecycleDispatch.h"

#include <atomic>
#include <chrono>
#include <mutex>

namespace Hooks
{
    static std::atomic_bool s_stormImadFadeActive{ false };
    static std::atomic_bool s_stormVisualsTaskQueued{ false };

    void SetStormIMADFadeActive(bool active) noexcept
    {
        s_stormImadFadeActive.store(active, std::memory_order_release);
    }

    void QueueStormVisuals()
    {
        if (!s_stormImadFadeActive.load(std::memory_order_acquire)) return;

        const auto generation = RadLifecycleDispatch::ReadyPlayerUpdateGeneration();
        if (generation == 0) return;

        static constexpr auto kStormVisualsInterval = std::chrono::milliseconds(33);
        static std::mutex throttleMutex;
        static std::chrono::steady_clock::time_point lastVisuals;
        static bool throttleInitialized = false;
        const auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(throttleMutex);
            if (throttleInitialized && now - lastVisuals < kStormVisualsInterval) return;
            lastVisuals = now;
            throttleInitialized = true;
        }

        bool expected = false;
        if (!s_stormVisualsTaskQueued.compare_exchange_strong(expected, true))
            return;

        bool queued = false;
        try
        {
            queued = QueueMainThreadTask([generation]() {
                try
                {
                    if (RadLifecycleDispatch::PlayerUpdatesReady(generation))
                        RadiationEngine::GetSingleton().UpdateStormIMADFade();
                }
                catch (...)
                {
                    s_stormVisualsTaskQueued.store(false);
                    throw;
                }
                s_stormVisualsTaskQueued.store(false);
            });
        }
        catch (...)
        {
            s_stormVisualsTaskQueued.store(false);
            throw;
        }
        if (!queued)
            s_stormVisualsTaskQueued.store(false);
    }

}
