#pragma once

namespace RadLifecycle
{
    // Ensures the core engine and PlayerCharacter update producer are both live.
    // Must be called on the verified Fallout 4 game-main thread. Safe to retry after
    // an earlier dispatch, initialization, or hook-install failure.
    bool EnsureEngineActiveOnGameThread();

    // True only after both core initialization and the update producer succeeded.
    bool IsEngineActive();
}
