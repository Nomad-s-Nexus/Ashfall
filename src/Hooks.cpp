#include "pch.h"
#include "Hooks.h"
#include "RadiationEngine.h"

#include <RE/P/PlayerCharacter.h>

namespace Hooks
{
    // Store original Update function pointer
    static REL::Relocation<decltype(&RE::PlayerCharacter::Update)> original_Update;

    // Engine logic runs at a fixed cadence, NOT every rendered frame.
    // PlayerCharacter::Update is called once per frame (~60-120 Hz). Running the
    // radiation engine every frame was both performance-intensive and applied
    // rate-based effects (env rad, health drain) dozens of times too fast.
    //
    // Throttle uses WALL-CLOCK time, not the per-frame a_delta. In FO4 the engine
    // zeroes actor delta-time while a fullscreen menu (Pip-Boy / console / map) is
    // open, so a delta-based accumulator stops advancing and our radiation/storm
    // damage halts — letting the player cheese radstorms by sitting in the Pip-Boy.
    // Wall-clock keeps the 4Hz tick firing regardless of menu pause-state. Fallen
    // World does not pause time in menus, so this matches the player's experience.
    static constexpr float kUpdateInterval = 0.25f;  // seconds -> 4 Hz
    static std::chrono::high_resolution_clock::time_point s_lastTick;
    static bool s_lastTickInit = false;

    static int update_tick_count = 0;
    static std::chrono::high_resolution_clock::time_point last_log_time;

    // Hook function that wraps the original
    void PlayerCharacter_Update_Hook(RE::PlayerCharacter* a_this, float a_delta)
    {
        // Always call the original first so the game updates normally.
        original_Update(a_this, a_delta);

        try
        {
            // Only run radiation updates for the player. Use FormID identity, NOT pointer compare
            // (multiple inheritance in PlayerCharacter means the singleton pointer can differ from
            // a_this even when they refer to the same actor — bit Permadeath for the same reason).
            if (!a_this || a_this->GetFormID() != 0x14)
            {
                return;
            }

            // Poll hotkeys EVERY frame (not throttled) so a quick Shift+F8 tap or HUD-toggle press
            // is never missed between 4Hz ticks (the "have to press several times" bug).
            RadiationEngine::GetSingleton().PollInput();

            // Throttle off wall-clock — see header note. a_delta is unreliable in menus.
            const auto now = std::chrono::high_resolution_clock::now();
            if (!s_lastTickInit)
            {
                s_lastTick     = now;
                s_lastTickInit = true;
                return;
            }
            const float elapsed = std::chrono::duration<float>(now - s_lastTick).count();
            if (elapsed < kUpdateInterval)
            {
                return;
            }
            // Advance by exactly one interval so we don't burst-catch-up after a long stall
            // (alt-tab, loading screen). At most one engine tick per frame.
            s_lastTick = now;

            update_tick_count++;

            // Log hook activity at most every 30 seconds.
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_log_time).count() >= 30)
            {
                auto logger = spdlog::get("FalloutAnomaly");
                logger->debug("[Hook] Engine ticked {} times (4 Hz). Hook is ACTIVE.", update_tick_count);
                last_log_time = now;
            }

            // Then apply our radiation logic
            auto& engine = RadiationEngine::GetSingleton();
            engine.Update();
        }
        catch (const std::exception& ex)
        {
            auto logger = spdlog::get("FalloutAnomaly");
            logger->error("[Hook] Exception in PlayerCharacter_Update_Hook: {}", ex.what());
        }
    }

    void Install()
    {
        auto logger = spdlog::get("FalloutAnomaly");

        logger->info("[Hook] Installing PlayerCharacter::Update hook...");

        try
        {
            // Hook PlayerCharacter::Update vtable (index 0x0CF in Actor/PlayerCharacter vtable)
            static REL::Relocation<std::uintptr_t> PlayerCharacter_vtable(RE::VTABLE::PlayerCharacter[0]);

            logger->debug("  PlayerCharacter vtable: 0x{:X}", PlayerCharacter_vtable.address());
            logger->debug("  Hook index: 0x0CF");

            original_Update = PlayerCharacter_vtable.write_vfunc(0x0CF, &PlayerCharacter_Update_Hook);

            if (original_Update.address() != 0)
            {
                logger->info("✓ Hook installed successfully");
                logger->debug("  Original function address: 0x{:X}", original_Update.address());
                logger->debug("  Hook frequency: ~4Hz (every ~250ms)");
                logger->debug("  Hook status: ACTIVE and running");

                last_log_time = std::chrono::high_resolution_clock::now();
            }
            else
            {
                logger->error("✗ Failed to install PlayerCharacter::Update hook");
                logger->error("  Original function address was null");
            }
        }
        catch (const std::exception& ex)
        {
            logger->error("✗ Exception during hook installation: {}", ex.what());
        }
    }
}
