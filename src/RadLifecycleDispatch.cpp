#include "pch.h"
#include "RadLifecycleDispatch.h"

#include "Hooks.h"
#include "RadCorrectnessPolicy.h"

#include <chrono>
#include <thread>

namespace RadLifecycleDispatch
{
    namespace
    {
        std::atomic<std::uint64_t> g_playerUpdateGeneration{ 0 };
        std::atomic<std::uint64_t> g_playerUpdateReadyGeneration{ 0 };

        bool TryQueue(const std::function<void()>& task)
        {
            try
            {
                return Hooks::QueueMainThreadTask(task);
            }
            catch (const std::exception& ex)
            {
                if (auto logger = spdlog::get("FalloutAnomaly"))
                    logger->error("[Lifecycle] Failed to queue game-main task: {}", ex.what());
            }
            catch (...)
            {
                if (auto logger = spdlog::get("FalloutAnomaly"))
                    logger->error("[Lifecycle] Failed to queue game-main task with an unknown exception.");
            }
            return false;
        }
    }

    void BlockPlayerUpdates() noexcept
    {
        g_playerUpdateGeneration.store(0, std::memory_order_release);
        // Constraint: a task queued when updates stop never runs, so its coalescing flag is cleared here.
        Hooks::ResetQueuedTaskFlags();
    }

    void SetPlayerUpdateGeneration(std::uint64_t generation) noexcept
    {
        g_playerUpdateGeneration.store(generation, std::memory_order_release);
    }

    void MarkPlayerUpdateGenerationReady(std::uint64_t generation) noexcept
    {
        g_playerUpdateReadyGeneration.store(generation, std::memory_order_release);
    }

    std::uint64_t ReadyPlayerUpdateGeneration() noexcept
    {
        const auto generation = g_playerUpdateGeneration.load(std::memory_order_acquire);
        return generation != 0 &&
            g_playerUpdateReadyGeneration.load(std::memory_order_acquire) == generation ? generation : 0;
    }

    bool PlayerUpdatesReady(std::uint64_t expectedGeneration) noexcept
    {
        return expectedGeneration != 0 &&
            g_playerUpdateGeneration.load(std::memory_order_acquire) == expectedGeneration &&
            g_playerUpdateReadyGeneration.load(std::memory_order_acquire) == expectedGeneration;
    }

    void QueueWithRetry(
        std::atomic<std::uint64_t>* generation,
        std::uint64_t expectedGeneration,
        std::string label,
        std::function<void()> task)
    {
        if (!generation || !task) return;
        if (TryQueue(task)) return;

        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->warn("[{}] lifecycle task deferred until the game-main dispatcher accepts work.", label);

        std::thread([generation, expectedGeneration, label = std::move(label), task = std::move(task)]() mutable {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
            while (RadCorrectnessPolicy::IsCurrentLoadGeneration(
                expectedGeneration, generation->load(std::memory_order_acquire)))
            {
                if (std::chrono::steady_clock::now() >= deadline) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (TryQueue(task)) return;
            }

            if (RadCorrectnessPolicy::IsCurrentLoadGeneration(
                    expectedGeneration, generation->load(std::memory_order_acquire)))
                if (auto logger = spdlog::get("FalloutAnomaly"))
                    logger->critical("[{}] lifecycle dispatch retry timed out; player-update producer remains blocked.", label);
        }).detach();
    }
}
