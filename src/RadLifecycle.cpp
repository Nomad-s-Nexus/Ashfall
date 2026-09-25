#include "pch.h"
#include "RadLifecycle.h"

#include "Hooks.h"
#include "NeedsConsumptionTrace.h"
#include "RadCorrectnessPolicy.h"
#include "RadHud.h"
#include "RadMaskOwnership.h"
#include "RadiationEngine.h"

#include <atomic>

namespace
{
    std::atomic_bool g_coreInitialized{ false };
    std::atomic_bool g_updateProducerInstalled{ false };
}

bool RadLifecycle::IsEngineActive()
{
    return RadCorrectnessPolicy::IsEngineActive(
        g_coreInitialized.load(std::memory_order_acquire),
        g_updateProducerInstalled.load(std::memory_order_acquire));
}

bool RadLifecycle::EnsureEngineActiveOnGameThread()
{
    auto logger = spdlog::get("FalloutAnomaly");
    if (!Hooks::IsGameMainThread())
    {
        if (logger)
            logger->critical("[Lifecycle] Refusing Radiation Engine activation off the verified game-main thread.");
        return false;
    }

    try
    {
        if (!RadMaskOwnership::Activate())
        {
            RadMaskOwnership::Clear();
            if (logger)
                logger->warn("[Lifecycle] Auto-mask ownership event sink unavailable; restored ownership discarded.");
        }

        NeedsConsumptionTrace::ActivateOnGameThread();

        if (!g_coreInitialized.load(std::memory_order_acquire))
        {
            if (logger) logger->info("Initializing Radiation Engine on verified game-main thread...");
            if (!RadiationEngine::GetSingleton().Initialize())
            {
                if (logger)
                {
                    logger->error("FAILED to initialize Radiation Engine.");
                    logger->error("  - Check JSON configuration files");
                    logger->error("  - Verify data folder exists");
                    logger->error("  - Activation will be retried on the next safe lifecycle event");
                }
                return false;
            }

            g_coreInitialized.store(true, std::memory_order_release);
            if (logger)
            {
                logger->info("Radiation Engine initialized successfully");
                logger->debug("  - Core mechanics loaded");
                logger->debug("  - Configuration parsed");
                logger->debug("  - State tracker created");
            }
        }

        if (!g_updateProducerInstalled.load(std::memory_order_acquire))
        {
            if (!Hooks::InstallUpdateHook())
            {
                if (logger)
                    logger->critical("[Lifecycle] Update producer hook failed; Radiation Engine remains INACTIVE and will retry later.");
                return false;
            }

            g_updateProducerInstalled.store(true, std::memory_order_release);
            if (logger)
            {
                logger->info("PlayerCharacter update producer installed");
                logger->debug("  - Per-frame input and 4Hz engine ticks are marshaled to game-main thread");
            }
        }

        // HUD integration is optional and must never make the gameplay engine inactive. Retry when
        // PrismaUI itself is missing OR when PrismaUI is present but PrismaHUD was not yet available.
        // Once both providers are acquired, preserve those known-good pointers instead of needlessly
        // re-requesting them on every load.
        auto& hud = RadHud::GetSingleton();
        if (!hud.Available() || !hud.HudLayoutAvailable())
        {
            try
            {
                hud.RequestAPI();
            }
            catch (const std::exception& ex)
            {
                if (logger) logger->warn("[Lifecycle] Optional Rad HUD API request failed: {}", ex.what());
            }
            catch (...)
            {
                if (logger) logger->warn("[Lifecycle] Optional Rad HUD API request failed with an unknown exception.");
            }
        }

        return IsEngineActive();
    }
    catch (const std::exception& ex)
    {
        if (logger) logger->error("[Lifecycle] Exception while activating Radiation Engine: {}", ex.what());
    }
    catch (...)
    {
        if (logger) logger->error("[Lifecycle] Unknown exception while activating Radiation Engine.");
    }

    return false;
}
