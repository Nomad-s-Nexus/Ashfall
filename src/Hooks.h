#pragma once

#include <functional>

namespace Hooks
{
    // Installs a thread-specific Win32 message hook on RE::Main::threadID. Work queued through
    // QueueMainThreadTask is executed only when that exact game-main thread pumps the message.
    bool InstallMainThreadDispatcher();

    // Installs the PlayerCharacter::Update producer hook. Call only after the engine is initialized
    // and the main-thread dispatcher is live.
    bool InstallUpdateHook();

    // Queue work for the verified game-main thread. Calls made from the main thread run inline.
    // Returns false if the dispatcher is unavailable or the wake-up message could not be posted.
    bool QueueMainThreadTask(std::function<void()> task);

    bool IsGameMainThread();
}
