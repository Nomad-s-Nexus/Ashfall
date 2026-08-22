#include "pch.h"
#include "RadiationEngine.h"
#include "Hooks.h"
#include "RadHud.h"
#include <RE/U/UI.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/C/ControlMap.h>
#include <thread>
#include <atomic>
// Windows AFTER the RE/F4SE headers (REX/W32 hard-errors otherwise). For the Documents log path.
#include <ShlObj.h>
#include <KnownFolders.h>

namespace
{
    std::shared_ptr<spdlog::logger> logger;
    RadiationEngine* g_engine = nullptr;

    enum class GameVersion
    {
        Unknown,
        OG,
        NewGen
    };

    GameVersion g_gameVersion = GameVersion::Unknown;

    GameVersion DetectGameVersion(const F4SE::QueryInterface* a_f4se)
    {
        const auto ver = a_f4se->RuntimeVersion();

        // OG: 1.10.162 - 1.10.980
        if (ver >= F4SE::RUNTIME_1_10_162 && ver < F4SE::RUNTIME_1_10_980)
        {
            return GameVersion::OG;
        }

        // New Gen: 1.10.980+
        if (ver >= F4SE::RUNTIME_1_10_980)
        {
            return GameVersion::NewGen;
        }

        return GameVersion::Unknown;
    }

    std::string GameVersionString(GameVersion version)
    {
        switch (version)
        {
        case GameVersion::OG:
            return "Original Game (1.10.x)";
        case GameVersion::NewGen:
            return "Next Generation (1.10.980+)";
        case GameVersion::Unknown:
        default:
            return "UNKNOWN";
        }
    }

    // Standard F4SE log location: Documents\My Games\Fallout4\F4SE. The old relative "Logs\" path
    // resolved against the game-dir CWD, which is write-protected on Program Files installs — the
    // sink then threw and ALL logging silently vanished into the null-sink fallback.
    std::filesystem::path ResolveLogDirectory()
    {
        wchar_t* docs = nullptr;
        if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &docs)) && docs)
        {
            std::filesystem::path dir = docs;
            ::CoTaskMemFree(docs);
            return dir / L"My Games" / L"Fallout4" / L"F4SE";
        }
        if (docs) ::CoTaskMemFree(docs);
        return {};
    }

    void InitializeLogger()
    {
        // A filesystem or file-open failure must never throw out of F4SEPlugin_Load (that would fail
        // the whole plugin). Fall back to the old game-dir Logs\ path, then to a no-op sink, so
        // `logger` is always valid and every later logger-> call is safe.
        try
        {
            std::filesystem::path logDir = ResolveLogDirectory();
            if (logDir.empty())
                logDir = "Logs";   // Documents unresolvable — old relative path as a fallback
            std::filesystem::create_directories(logDir);

            auto logPath = logDir / "FalloutAnomaly.log";
            auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string());
            logger = std::make_shared<spdlog::logger>("FalloutAnomaly", sink);
        }
        catch (const std::exception&)
        {
            try
            {
                std::filesystem::create_directories("Logs");
                auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("Logs\\FalloutAnomaly.log");
                logger = std::make_shared<spdlog::logger>("FalloutAnomaly", sink);
            }
            catch (const std::exception&)
            {
                logger = std::make_shared<spdlog::logger>("FalloutAnomaly",
                    std::make_shared<spdlog::sinks::null_sink_mt>());
            }
        }

        logger->set_level(spdlog::level::debug);
        logger->flush_on(spdlog::level::info);  // flush info+ so a hard crash keeps recent lines
        spdlog::register_logger(logger);

        logger->info("===========================================");
        logger->info("FALLOUT ANOMALY - RADIATION OVERHAUL");
        logger->info("Plugin Version: 1.0");
        logger->info("Build Date: {}", __DATE__);
        logger->info("===========================================");
    }

    // --- F4SE co-save callbacks: persist the storm play-clock + schedule across save/load ---
    // These only serialize the DLL's plain state and do not call game-engine mutation APIs.
    void SaveCallback(const F4SE::SerializationInterface* a_intfc)
    {
        RadiationEngine::GetSingleton().SaveState(a_intfc);
    }

    void LoadCallback(const F4SE::SerializationInterface* a_intfc)
    {
        RadiationEngine::GetSingleton().LoadState(a_intfc);
    }

    void RevertCallback(const F4SE::SerializationInterface* /*a_intfc*/)
    {
        RadiationEngine::GetSingleton().RevertState();
    }

    std::atomic<bool> g_radWaiting{ false };
    std::atomic<std::uint64_t> g_radWaitGeneration{ 0 };

    bool IsCurrentRadHudWait(std::uint64_t generation)
    {
        return g_radWaiting.load(std::memory_order_acquire) &&
               g_radWaitGeneration.load(std::memory_order_acquire) == generation;
    }

    void WaitThenCreateRadHud(std::uint64_t generation)
    {
        auto free = std::make_shared<std::atomic<bool>>(false);
        bool wasFree = false;
        auto freeSince = std::chrono::steady_clock::now();

        // Hard cap so the detached thread can never linger indefinitely. The generation token also
        // invalidates every queued probe/CreateView from an older load as soon as kPreLoadGame fires,
        // so a fast reload cannot revive an old detached waiter when the new load starts its own one.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);

        while (IsCurrentRadHudWait(generation)) {
            if (std::chrono::steady_clock::now() >= deadline) {
                if (g_radWaitGeneration.load(std::memory_order_acquire) == generation)
                    g_radWaiting.store(false, std::memory_order_release);
                if (logger) logger->warn("FalloutAnomaly: radhud wait thread timed out (10 min) — giving up this load.");
                break;
            }

            // Runtime probing proved F4SE AddTask is not a game-main-thread marshal on this setup.
            // Reset pessimistically before queuing: if the dispatcher is backlogged, a stale TRUE from
            // the previous probe must not keep advancing the 5-second world-ready hold timer.
            free->store(false, std::memory_order_release);
            if (!Hooks::QueueMainThreadTask([free, generation]() {
                    if (g_radWaitGeneration.load(std::memory_order_acquire) != generation)
                        return;
                    auto* ui = RE::UI::GetSingleton();
                    auto* cm = RE::ControlMap::GetSingleton();
                    auto* pc = RE::PlayerCharacter::GetSingleton();
                    const bool ctrl = ui && ui->menuMode == 0 && cm && !cm->ignoreKeyboardMouse && pc && pc->parentCell;
                    free->store(ctrl, std::memory_order_release);
                }))
            {
                if (logger) logger->error("FalloutAnomaly: failed to queue radhud world-ready probe on game-main thread.");
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (!IsCurrentRadHudWait(generation)) break;

            if (free->load(std::memory_order_acquire)) {
                if (!wasFree) {
                    wasFree = true;
                    freeSince = std::chrono::steady_clock::now();
                } else {
                    const auto held = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - freeSince).count();
                    if (held >= 5000) { // wait 5 seconds after loading screen
                        if (logger) logger->info("FalloutAnomaly: player is in world, creating radhud view");
                        if (!Hooks::QueueMainThreadTask([generation]() {
                                if (g_radWaitGeneration.load(std::memory_order_acquire) == generation)
                                    RadHud::GetSingleton().CreateView();
                            }))
                        {
                            if (logger) logger->error("FalloutAnomaly: failed to queue radhud CreateView on game-main thread.");
                        }
                        if (g_radWaitGeneration.load(std::memory_order_acquire) == generation)
                            g_radWaiting.store(false, std::memory_order_release);
                        break;
                    }
                }
            } else {
                wasFree = false;
            }
        }
    }

    // Spawn one waiter per load generation. kPreLoadGame invalidates the generation before the next
    // post-load/new-game event can start another waiter, so detached threads cannot cross load boundaries.
    void StartRadHudWaitIfIdle()
    {
        bool expected = false;
        if (g_radWaiting.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            const auto generation = g_radWaitGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
            std::thread(WaitThenCreateRadHud, generation).detach();
        }
    }

    void MessageHandler(F4SE::MessagingInterface::Message* a_msg)
    {
        if (!logger || !a_msg)
        {
            if (logger) logger->error("[MessageHandler] Received null message pointer");
            return;
        }

        // The F4SE message callback is not the game main thread on this runtime. It may coordinate
        // state, but any game-engine access below is queued through Hooks::QueueMainThreadTask.
        logger->debug("[MessageHandler] Processing message type: {} on thread {}",
            static_cast<int>(a_msg->type), ::GetCurrentThreadId());

        switch (a_msg->type)
        {
        case F4SE::MessagingInterface::kGameDataReady:
            logger->info("========== GAME DATA READY EVENT ==========");
            logger->info("Game Version: {}", GameVersionString(g_gameVersion));

            // Establish the verified handoff first. If that cannot be installed, fail closed: do not
            // initialize engine-facing systems or install the worker producer hook.
            if (!Hooks::InstallMainThreadDispatcher())
            {
                logger->critical("Failed to establish Fallout 4 main-thread dispatcher; radiation engine will remain inactive.");
                break;
            }

            if (!Hooks::QueueMainThreadTask([]() {
                    logger->info("Initializing Radiation Engine on verified game-main thread...");
                    try
                    {
                        if (RadiationEngine::GetSingleton().Initialize())
                        {
                            logger->info("✓ Radiation Engine initialized successfully");
                            logger->debug("  - Core mechanics loaded");
                            logger->debug("  - Configuration parsed");
                            logger->debug("  - State tracker created");

                            if (Hooks::InstallUpdateHook())
                            {
                                logger->info("✓ PlayerCharacter update producer installed");
                                logger->debug("  - Per-frame input and 4Hz engine ticks are marshaled to game-main thread");
                            }
                            else
                            {
                                logger->critical("✗ Update producer hook failed; radiation engine will not tick.");
                            }

                            // Acquire the optional PrismaUI HUD API from the same verified engine thread.
                            RadHud::GetSingleton().RequestAPI();
                        }
                        else
                        {
                            logger->error("✗ FAILED to initialize Radiation Engine");
                            logger->error("  - Check JSON configuration files");
                            logger->error("  - Verify data folder exists");
                            logger->error("  - See detailed logs above");
                        }
                    }
                    catch (const std::exception& ex)
                    {
                        logger->error("✗ EXCEPTION during main-thread initialization: {}", ex.what());
                    }
                    logger->info("===========================================");
                }))
            {
                logger->critical("Could not queue Radiation Engine initialization on game-main thread.");
            }
            break;

        case F4SE::MessagingInterface::kPostLoadGame:
            logger->info("========== POST LOAD GAME EVENT ==========");
            if (!Hooks::QueueMainThreadTask([]() {
                    auto& engine = RadiationEngine::GetSingleton();
                    engine.OnPostLoad();
                    engine.ResetAppliedPenalties();
                    StartRadHudWaitIfIdle();
                    logger->info("Game save loaded. Radiation Engine ACTIVE.");
                    logger->debug("  - Load-state engine mutation completed on game-main thread");
                    logger->info("=========================================");
                }))
            {
                logger->critical("Failed to queue post-load radiation reset on game-main thread.");
            }
            break;

        case F4SE::MessagingInterface::kPostPostLoad:
            logger->info("Post-post load event received");
            break;

        case F4SE::MessagingInterface::kNewGame:
            logger->info("New game started");
            if (!Hooks::QueueMainThreadTask([]() {
                    auto& engine = RadiationEngine::GetSingleton();
                    engine.OnPostLoad();   // reset per-load tracking
                    engine.OnNewGame();    // arm clear-weather-on-first-exterior
                    engine.ResetAppliedPenalties();
                    StartRadHudWaitIfIdle();
                    logger->debug("  - New-game radiation state reset on game-main thread");
                    logger->debug("  - Update loop: RUNNING");
                }))
            {
                logger->critical("Failed to queue new-game radiation reset on game-main thread.");
            }
            break;

        case F4SE::MessagingInterface::kPreLoadGame:
            // Invalidate the current detached waiter and every already-queued probe/CreateView from it
            // before the next load can start a fresh generation.
            g_radWaiting.store(false, std::memory_order_release);
            g_radWaitGeneration.fetch_add(1, std::memory_order_acq_rel);
            logger->info("Game loading...");
            break;

        default:
            logger->debug("Unhandled message type: {}", static_cast<int>(a_msg->type));
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Modern plugin declaration is retained because modern F4SE loaders inspect this
// export before F4SEPlugin_Load. Runtime support is still deliberately OG-only;
// F4SEPlugin_Load fails closed on NG before any hooks or messaging callbacks are installed.
// Legacy F4SEPlugin_Query is retained for OG F4SE.
// ---------------------------------------------------------------------------
F4SE_PLUGIN_VERSION = []() noexcept {
    F4SE::PluginVersionData data{};
    data.PluginVersion(REL::Version(1, 0, 0));
    data.PluginName("Fallout Anomaly - Radiation Overhaul");
    data.AuthorName("NomadsReach");
    data.UsesSigScanning(false);
    data.UsesAddressLibrary(true);
    data.UsesAddressLibraryNG(true);
    data.HasNoStructUse(false);
    data.IsLayoutDependent(true);
    data.IsLayoutDependentNG(true);
    data.CompatibleVersions({});
    return data;
}();

extern "C" __declspec(dllexport) bool F4SEAPI
F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
        auto* skse = reinterpret_cast<const F4SE::QueryInterface*>(a_f4se);
        if (!skse)
        {
            return false;
        }

        a_info->infoVersion = F4SE::PluginInfo::kVersion;
        a_info->name = "Fallout Anomaly - Radiation Overhaul";
        a_info->version = 1;

        if (skse->IsEditor())
        {
            return false;
        }

        // This build is OG-only. The PlayerCharacter::Update vtable slot and raw
        // native relocations have not been validated for NG, so reject it rather
        // than letting an unsupported runtime install the hook.
        g_gameVersion = DetectGameVersion(skse);
        return g_gameVersion == GameVersion::OG;
}

extern "C" __declspec(dllexport) bool F4SEAPI
F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
        F4SE::Init(a_f4se);

        // On NG, F4SEPlugin_Query is not called — detect version here so the same
        // OG-only guard applies regardless of which loader path F4SE used.
        if (g_gameVersion == GameVersion::Unknown)
        {
            g_gameVersion = DetectGameVersion(a_f4se);
        }

        InitializeLogger();

        if (g_gameVersion != GameVersion::OG)
        {
            logger->critical(
                "Unsupported Fallout 4 runtime: {}. This build is OG-only (tested on 1.10.163); "
                "refusing to install hooks on an unverified runtime.",
                GameVersionString(g_gameVersion));
            return false;
        }

        logger->info("F4SEPlugin_Load called");
        logger->info("Initializing plugin infrastructure...");
        logger->debug("  - Game Version: {}", GameVersionString(g_gameVersion));
        logger->debug("  - Logger: Initialized");
        logger->debug("  - Spdlog Level: DEBUG");

        auto messaging = F4SE::GetMessagingInterface();
        if (!messaging)
        {
            logger->error("✗ CRITICAL: Failed to get F4SE messaging interface");
            return false;
        }

        logger->debug("  - Messaging Interface: OK");

        if (!messaging->RegisterListener(MessageHandler))
        {
            logger->error("✗ CRITICAL: Failed to register message listener");
            logger->error("  - MessageHandler callback registration failed");
            return false;
        }

        logger->info("✓ Message listener registered successfully");
        logger->debug("  - Will receive: kGameDataReady, kPostLoadGame, kPostPostLoad, kNewGame, kPreLoadGame");

        // Register co-save serialization so the storm play-clock + schedule survive save/load.
        if (auto* serialization = F4SE::GetSerializationInterface())
        {
            serialization->SetUniqueID('FARO');   // Fallout Anomaly Radiation Overhaul
            serialization->SetRevertCallback(RevertCallback);
            serialization->SetSaveCallback(SaveCallback);
            serialization->SetLoadCallback(LoadCallback);
            logger->info("✓ Serialization registered — storm clock persists across saves.");
        }
        else
        {
            logger->warn("Serialization interface unavailable — storm clock will NOT persist.");
        }

        logger->info("✓ Plugin loaded successfully. Awaiting game events...");

        return true;
}
