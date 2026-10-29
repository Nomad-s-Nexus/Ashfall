#pragma once

// Constraint: replaces the vanilla HC_* activate path, so no HC_* record is read or written.
namespace WaterBottleFill
{
    // Constraint: registration must run on the game-main thread and be retried after a failure.
    bool ActivateOnGameThread();
    bool Active() noexcept;
}
