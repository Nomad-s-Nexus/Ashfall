#include "pch.h"
#include "Hooks.h"
#include "RadiationEngine.h"

#include <RE/M/Main.h>
#include <RE/P/PlayerCharacter.h>

#include <atomic>
#include <deque>
#include <mutex>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace Hooks
{
    // PlayerCharacter::Update is a producer only. Runtime probing established that this vfunc executes
    // on a worker thread in this Fallout 4 runtime, so engine-facing radiation work must never run
    // directly from it.
    static REL::Relocation<decltype(&RE::PlayerCharacter::Update)> original_Update;
    static bool s_updateHookInstalled = false;

    // Verified game-main-thread dispatcher. RE::Main owns the game window and records the thread ID that
    // owns the main loop. A thread-specific WH_GETMESSAGE hook executes in that exact thread whenever its
    // Win32 queue is pumped. We post a private registered message to wake it and drain our C++ task queue.
    // This deliberately does NOT use F4SE AddTask: runtime probes showed AddTask callbacks execute on a
    // third thread on this setup and therefore are not a main-thread marshal.
    // Atomic: written on the F4SE-messaging thread in InstallMainThreadDispatcher and read from the
    // worker-thread producer path (IsGameMainThread, QueueMainThreadTask's availability gate) and the
    // detached RadHud waiter. The availability gate is itself one of these reads, so a plain-int race
    // here would be a race on the safety check. seq_cst gives the gate a happens-before on the install
    // stores (hook is published last, so a reader that sees it also sees threadId/wakeMessage).
    static std::atomic<DWORD> s_gameMainThreadId{ 0 };
    static std::atomic<HHOOK> s_mainThreadMsgHook{ nullptr };
    static std::atomic<UINT>  s_mainThreadWakeMessage{ 0 };
    static std::mutex s_mainTaskMutex;
    static std::deque<std::function<void()>> s_mainTasks;

    // Coalesce producer traffic. PlayerCharacter::Update can run 60-120+ times/s; there is no reason to
    // enqueue another input poll or 4 Hz engine tick while one of the same kind is already waiting.
    static std::atomic_bool s_inputTaskQueued{ false };
    static std::atomic_bool s_updateTaskQueued{ false };

    // PlayerCharacter::Update is a worker job and may migrate between worker threads across frames.
    // Protect every piece of throttle/log cadence state explicitly rather than relying on one stable
    // worker identity. The critical section is tiny and only entered for the player update.
    using TickClock = std::chrono::steady_clock;
    static constexpr float kUpdateInterval = 0.25f;  // seconds -> 4 Hz
    static std::mutex s_tickStateMutex;
    static TickClock::time_point s_lastTick;
    static TickClock::time_point s_lastLogTime;
    static bool s_lastTickInit = false;
    static int s_updateTickCount = 0;

    bool IsGameMainThread()
    {
        return s_gameMainThreadId != 0 && ::GetCurrentThreadId() == s_gameMainThreadId;
    }

    static void DrainMainThreadTasks()
    {
        // The hook is targeted at RE::Main::threadID, but keep an executable invariant rather than
        // trusting setup comments. If this ever fires on another thread, do not execute engine work.
        if (!IsGameMainThread())
        {
            if (auto logger = spdlog::get("FalloutAnomaly"))
                logger->critical("[MainDispatch] wake hook ran on thread {} but game main thread is {}; refusing queued engine work.",
                    ::GetCurrentThreadId(), s_gameMainThreadId.load());
            return;
        }

        std::deque<std::function<void()>> tasks;
        {
            std::lock_guard<std::mutex> lock(s_mainTaskMutex);
            tasks.swap(s_mainTasks);
        }

        for (auto& task : tasks)
        {
            if (!task) continue;
            try
            {
                task();
            }
            catch (const std::exception& ex)
            {
                if (auto logger = spdlog::get("FalloutAnomaly"))
                    logger->error("[MainDispatch] queued task threw: {}", ex.what());
            }
            catch (...)
            {
                if (auto logger = spdlog::get("FalloutAnomaly"))
                    logger->error("[MainDispatch] queued task threw an unknown exception.");
            }
        }
    }

    static LRESULT CALLBACK MainThreadGetMessageHook(int code, WPARAM wParam, LPARAM lParam)
    {
        if (code >= 0 && lParam && s_mainThreadWakeMessage != 0)
        {
            auto* msg = reinterpret_cast<MSG*>(lParam);
            if (msg->message == s_mainThreadWakeMessage)
            {
                DrainMainThreadTasks();

                // The registered message is ours only. Once removed from the queue, turn it into a
                // WM_NULL so Fallout's normal message dispatch never has to know about it.
                if (wParam == PM_REMOVE)
                {
                    msg->message = WM_NULL;
                    msg->wParam = 0;
                    msg->lParam = 0;
                }
            }
        }
        return ::CallNextHookEx(s_mainThreadMsgHook, code, wParam, lParam);
    }

    bool InstallMainThreadDispatcher()
    {
        auto logger = spdlog::get("FalloutAnomaly");
        if (s_mainThreadMsgHook && s_gameMainThreadId != 0)
            return true;

        auto* main = RE::Main::GetSingleton();
        if (!main || main->threadID == 0)
        {
            if (logger) logger->critical("[MainDispatch] RE::Main/threadID unavailable; cannot establish safe engine execution thread.");
            return false;
        }

        s_gameMainThreadId = static_cast<DWORD>(main->threadID);
        s_mainThreadWakeMessage = ::RegisterWindowMessageW(L"FARO_RadOverhaul_MainThreadDispatch_v1");
        if (s_mainThreadWakeMessage == 0)
        {
            if (logger) logger->critical("[MainDispatch] RegisterWindowMessage failed (GetLastError={}).", ::GetLastError());
            s_gameMainThreadId = 0;
            return false;
        }

        // Thread belongs to this process, so hMod must be null. WH_GETMESSAGE invokes our hook in the
        // context of the target thread when it calls GetMessage/PeekMessage.
        s_mainThreadMsgHook = ::SetWindowsHookExW(
            WH_GETMESSAGE, &MainThreadGetMessageHook, nullptr, s_gameMainThreadId);
        if (!s_mainThreadMsgHook)
        {
            if (logger) logger->critical("[MainDispatch] SetWindowsHookEx(WH_GETMESSAGE) failed for thread {} (GetLastError={}).",
                s_gameMainThreadId.load(), ::GetLastError());
            s_gameMainThreadId = 0;
            s_mainThreadWakeMessage = 0;
            return false;
        }

        if (logger)
            logger->info("[MainDispatch] Installed on Fallout 4 game-main thread {} (current setup thread {}).",
                s_gameMainThreadId.load(), ::GetCurrentThreadId());
        return true;
    }

    bool QueueMainThreadTask(std::function<void()> task)
    {
        if (!task) return false;

        if (IsGameMainThread())
        {
            // Keep inline execution behavior consistent with queued execution: task exceptions are
            // contained inside the dispatcher boundary rather than unexpectedly escaping only when a
            // caller happens to already be on the main thread.
            try
            {
                task();
                return true;
            }
            catch (const std::exception& ex)
            {
                if (auto logger = spdlog::get("FalloutAnomaly"))
                    logger->error("[MainDispatch] inline task threw: {}", ex.what());
            }
            catch (...)
            {
                if (auto logger = spdlog::get("FalloutAnomaly"))
                    logger->error("[MainDispatch] inline task threw an unknown exception.");
            }
            return false;
        }

        if (!s_mainThreadMsgHook || s_gameMainThreadId == 0 || s_mainThreadWakeMessage == 0)
            return false;

        std::lock_guard<std::mutex> lock(s_mainTaskMutex);
        s_mainTasks.emplace_back(std::move(task));
        if (!::PostThreadMessageW(s_gameMainThreadId, s_mainThreadWakeMessage, 0, 0))
        {
            s_mainTasks.pop_back();
            if (auto logger = spdlog::get("FalloutAnomaly"))
                logger->error("[MainDispatch] PostThreadMessage failed (GetLastError={}).", ::GetLastError());
            return false;
        }
        return true;
    }

    static void QueueInputPoll()
    {
        bool expected = false;
        if (!s_inputTaskQueued.compare_exchange_strong(expected, true))
            return;

        bool queued = false;
        try
        {
            queued = QueueMainThreadTask([]() {
                try
                {
                    RadiationEngine::GetSingleton().PollInput();
                }
                catch (...)
                {
                    s_inputTaskQueued.store(false);
                    throw;
                }
                s_inputTaskQueued.store(false);
            });
        }
        catch (...)
        {
            // QueueMainThreadTask can throw while enqueuing (e.g. bad_alloc growing the deque) before the
            // task body's reset ever runs. Clear the flag here or this producer is stranded forever.
            s_inputTaskQueued.store(false);
            throw;
        }
        if (!queued)
            s_inputTaskQueued.store(false);
    }

    static void QueueEngineUpdate()
    {
        bool expected = false;
        if (!s_updateTaskQueued.compare_exchange_strong(expected, true))
            return;

        bool queued = false;
        try
        {
            queued = QueueMainThreadTask([]() {
                try
                {
                    RadiationEngine::GetSingleton().Update();
                }
                catch (...)
                {
                    s_updateTaskQueued.store(false);
                    throw;
                }
                s_updateTaskQueued.store(false);
            });
        }
        catch (...)
        {
            // See QueueInputPoll: a throw out of QueueMainThreadTask itself must not strand the flag,
            // or 4 Hz engine ticks stop permanently.
            s_updateTaskQueued.store(false);
            throw;
        }
        if (!queued)
            s_updateTaskQueued.store(false);
    }

    // Worker-thread hook: call the original and produce main-thread work only.
    void PlayerCharacter_Update_Hook(RE::PlayerCharacter* a_this, float a_delta)
    {
        original_Update(a_this, a_delta);

        try
        {
            // Only react to the player. Use FormID identity rather than pointer equality because
            // PlayerCharacter's multiple inheritance can produce different adjusted pointers.
            if (!a_this || a_this->GetFormID() != 0x14)
                return;

            // Preserve per-frame key responsiveness without touching engine state on this worker.
            QueueInputPoll();

            // Wall-clock throttle: actor delta-time can be zero in fullscreen menus. At most one engine
            // update is produced every 250 ms. The mutex makes this safe even if Fallout migrates the
            // PlayerCharacter update job between worker threads across frames.
            const auto now = TickClock::now();
            {
                std::lock_guard<std::mutex> lock(s_tickStateMutex);
                if (!s_lastTickInit)
                {
                    s_lastTick = now;
                    s_lastTickInit = true;
                    return;
                }

                const float elapsed = std::chrono::duration<float>(now - s_lastTick).count();
                if (elapsed < kUpdateInterval)
                    return;
                s_lastTick = now;

                ++s_updateTickCount;
                if (std::chrono::duration_cast<std::chrono::seconds>(now - s_lastLogTime).count() >= 30)
                {
                    if (auto logger = spdlog::get("FalloutAnomaly"))
                        logger->debug("[Hook] Produced {} 4 Hz engine ticks; execution is marshaled to game-main thread {}.",
                            s_updateTickCount, s_gameMainThreadId.load());
                    s_lastLogTime = now;
                }
            }

            QueueEngineUpdate();
        }
        catch (const std::exception& ex)
        {
            if (auto logger = spdlog::get("FalloutAnomaly"))
                logger->error("[Hook] Exception in PlayerCharacter_Update_Hook producer: {}", ex.what());
        }
    }

    bool InstallUpdateHook()
    {
        auto logger = spdlog::get("FalloutAnomaly");
        if (!IsGameMainThread())
        {
            if (logger) logger->critical("[Hook] Refusing update-hook install off the verified game-main thread.");
            return false;
        }
        if (!s_mainThreadMsgHook)
        {
            if (logger) logger->critical("[Hook] Refusing update-hook install without main-thread dispatcher.");
            return false;
        }
        if (s_updateHookInstalled)
        {
            if (logger) logger->debug("[Hook] PlayerCharacter::Update producer already installed; skipping duplicate install.");
            return true;
        }

        if (logger) logger->info("[Hook] Installing PlayerCharacter::Update producer hook...");
        try
        {
            static REL::Relocation<std::uintptr_t> PlayerCharacter_vtable(RE::VTABLE::PlayerCharacter[0]);
            original_Update = PlayerCharacter_vtable.write_vfunc(0x0CF, &PlayerCharacter_Update_Hook);

            if (original_Update.address() == 0)
            {
                if (logger) logger->error("[Hook] PlayerCharacter::Update original function was null.");
                return false;
            }

            {
                std::lock_guard<std::mutex> lock(s_tickStateMutex);
                s_lastTickInit = false;
                s_updateTickCount = 0;
                s_lastLogTime = TickClock::now();
            }
            s_updateHookInstalled = true;

            if (logger)
            {
                logger->info("[Hook] Producer hook installed successfully.");
                logger->debug("  Original function address: 0x{:X}", original_Update.address());
                logger->debug("  Producer cadence: per-frame input + ~4 Hz engine tick");
                logger->debug("  Engine execution thread: verified game-main thread {}", s_gameMainThreadId.load());
            }
            return true;
        }
        catch (const std::exception& ex)
        {
            if (logger) logger->error("[Hook] Exception during update-hook installation: {}", ex.what());
            return false;
        }
    }
}
