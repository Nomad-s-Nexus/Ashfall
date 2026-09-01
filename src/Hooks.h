#pragma once

#include <functional>

namespace Hooks
{
    // Configures verified game-main-thread dispatch. PrismaUI V11 is preferred when PrismaUI is
    // installed; its transient startup-not-ready state is non-blocking. The Win32 message hook is
    // installed only when PrismaUI is genuinely absent.
    bool InstallMainThreadDispatcher();

    // Installs the PlayerCharacter::Update producer hook. Call only after the engine is initialized
    // and a verified main-thread dispatcher is live.
    bool InstallUpdateHook();
    bool InstallInputHandler();

    // Queue work for the verified game-main thread. Calls made from the main thread run inline.
    // Returns false when PrismaUI is present but its dispatcher is not ready yet, or when the
    // no-PrismaUI Win32 fallback cannot accept/wake the task. Callers may safely retry later.
    bool QueueMainThreadTask(std::function<void()> task);

    bool IsGameMainThread();
}
