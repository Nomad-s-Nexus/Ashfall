#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace RadLifecycleDispatch
{
    void BlockPlayerUpdates() noexcept;
    void SetPlayerUpdateGeneration(std::uint64_t generation) noexcept;
    void MarkPlayerUpdateGenerationReady(std::uint64_t generation) noexcept;
    std::uint64_t ReadyPlayerUpdateGeneration() noexcept;
    bool PlayerUpdatesReady(std::uint64_t expectedGeneration) noexcept;

    void QueueWithRetry(
        std::atomic<std::uint64_t>* generation,
        std::uint64_t expectedGeneration,
        std::string label,
        std::function<void()> task);
}
