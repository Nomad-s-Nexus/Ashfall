#include "RadEngine_Internal.h"
#include "SerializationPolicy.h"

#include <vector>

using namespace RadInternal;

// Engine core: lifecycle (Initialize + the 4Hz Update tick), input polling, ESL form
// resolution, co-save persistence, and new-game / post-load reset.
// Each subsystem lives in its own RadEngine_*.cpp; shared constants: RadEngine_Internal.h



namespace
{
    // Player-hit sink for the gas-mask CONDITION system: when the player is struck, the worn mask
    // takes durability damage. Registered once at kGameDataReady on the global hit-event source.
    class MaskHitSink : public RE::BSTEventSink<RE::TESHitEvent>
    {
    public:
        static MaskHitSink* GetSingleton() { static MaskHitSink s; return &s; }
        RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent& a_event,
                                              RE::BSTEventSource<RE::TESHitEvent>*) override
        {
            // target = who got hit. Only the player's mask takes damage here — IsPlayerRef() (via
            // commonlib_NonVR) is the authoritative check, no raw 0x14 FormID literal.
            if (a_event.target && a_event.target->IsPlayerRef())
                RadiationEngine::GetSingleton().OnPlayerHit();
            return RE::BSEventNotifyControl::kContinue;
        }
    };
}


bool RadiationEngine::Initialize()
{
    auto logger = Log();

    // Storm arrays (_stormWeathers/_stormIMADs) are sized by kStormTypeCount; the table loops use
    // kStormTypeTableSize. They must stay equal or a resolved storm type could be unindexable / OOB.
    static_assert(kStormTypeCount == kStormTypeTableSize,
                  "kStormTypeCount must match the kStormTypes table length");

    logger->info("========== RADIATION ENGINE INITIALIZATION ==========");

    try
    {
        logger->debug("[1] Getting RadiationConfig singleton...");
        _config = &RadiationConfig::GetSingleton();
        if (!_config)
        {
            logger->error("  ✗ Failed to get config singleton");
            return false;
        }
        logger->debug("  ✓ RadiationConfig singleton retrieved");

        // Determine data path
        auto dataPath = "Data/F4SE/Plugins/FalloutAnomaly";
        logger->debug("[2] Loading configuration from: {}", dataPath);

        if (!std::filesystem::exists(dataPath))
        {
            logger->error("  ✗ Config path does not exist: {}", dataPath);
            logger->error("  Create the folder and add thresholds.json.");
            return false;
        }
        logger->debug("  ✓ Config directory exists");

        if (!_config->LoadFromFile(dataPath))
        {
            logger->error("  ✗ Failed to load config from {}", dataPath);
            logger->error("  Check logs above for specific parsing errors");
            return false;
        }

        logger->info("  ✓ Configuration loaded successfully");
        logger->debug("    - Radiation stage thresholds loaded (thresholds.json)");

        _currentStageLevel = 0;
        _lastUpdateTime = std::chrono::high_resolution_clock::now();

        LoadFilterIni();   // gas-mask filter + condition tuning ([Filter] / [Condition] sections)

        // Register the player-hit sink once for the gas-mask condition system.
        static bool s_hitSinkRegistered = false;
        if (!s_hitSinkRegistered)
        {
            if (auto* src = RE::TESHitEvent::GetEventSource())
            {
                src->RegisterSink(MaskHitSink::GetSingleton());
                s_hitSinkRegistered = true;
                logger->info("[Condition] Player-hit sink registered (mask takes damage when struck).");
            }
        }

        logger->info("✓ RadiationEngine initialized successfully");
        logger->info("====================================================");
        return true;
    }
    catch (const std::exception& ex)
    {
        logger->error("✗ Exception during RadiationEngine initialization: {}", ex.what());
        logger->error("====================================================");
        return false;
    }
}


void RadiationEngine::Update()
{
    auto logger = Log();

    try
    {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player)
        {
            logger->debug("[Update] Player singleton not available (game not loaded)");
            return;
        }

        // Get vanilla radiation level via ActorValue singleton
        auto* actorValue = RE::ActorValue::GetSingleton();
        if (!actorValue || !actorValue->rads)
        {
            logger->debug("[Update] ActorValue singleton or rads value not available");
            return;
        }

        // Real play-clock for storm scheduling: the hook ticks at 4Hz only during active gameplay,
        // so each tick is ~0.25s of real play. Drives the timescale-independent storm schedule.
        _playSeconds += 0.25;

        // Lazy-load ESL records + force-start the FX quests (both latched), then run any debug
        // commands queued from the Ultralight JS thread — HERE, on the main thread, so their
        // engine calls (weather/IMAD/VM/spawns) are safe. This fixes the End Storm CTD.
        EnsureEslLoaded();
        // Deferred post-load storm-FX stop: OnPostLoad latches this because on the session's first
        // load the FX quest/IMADs were still unresolved there (see OnPostLoad). Runs once per load.
        if (_pendingLoadFXCleanup)
        {
            _pendingLoadFXCleanup = false;
            TriggerStormFX(false);
            ClearStormIMAD();
        }
        DrainDebugCommands();

        // Is a storm (ours or a vanilla radstorm) currently active? Drives both the shelter raycast
        // and the sheltered rad-block below.
        RE::Sky* sky = RE::Sky::GetSingleton();
        const bool vanillaRadstorm = sky && sky->currentWeather &&
            IsRadstormWeather(sky->currentWeather->GetFormID());
        const bool stormActive = (_stormPhase != StormPhase::Idle) || vanillaRadstorm;

        // Standard rain (vanilla or another mod's rad-rain) applies rads via weather-conditioned
        // magic effects that fire regardless of a roof — exactly like a radstorm. It does NOT set
        // stormActive (it is not one of our managed storms nor a radstorm FormID), so without this
        // the roof raycast never ran and the player gained rads under cover (e.g. the DC entryway).
        // Treat any rainy weather as "needs shelter evaluation" so the roof-fan runs and the
        // sheltered rad-refund below applies. Cheap: just a flag read off the current weather.
        const bool rainyWeather = sky && sky->currentWeather && IsRainyWeather(sky->currentWeather);

        // Rad-weather membership (FLST 0x802) — shared by the filter choke, the CHOKING health
        // damage, and the auto-equip below. Computed HERE (not later) because it can be broader
        // than stormActive (e.g. a forced storm sky preview, or FLST weathers outside a managed
        // phase) and the choking check depends on a FRESH roof raycast for those too.
        bool inRadWeather = false;
        if (_radWeatherList && sky && sky->currentWeather)
            inRadWeather = _radWeatherList->ContainsItem(sky->currentWeather);

        // Dust horizon build-up: during the WARNING phase we only fade the dust sky in on the horizon —
        // the storm hasn't HIT yet, so suppress the FLST-driven choke / CHOKING health / gas-mask
        // auto-equip. They resume at Active (build-up ends, _stormPhase == Active), preserving the
        // pre-Option-B gameplay timing (effects begin when the storm actually arrives).
        if (inRadWeather && _dustBuildupActive && _stormPhase == StormPhase::Warning)
            inRadWeather = false;

        const bool needsShelterEval = stormActive || rainyWeather || inRadWeather;

        // Roof/wall shelter raycast (throttled). Only matters during a storm, so we only pay for the
        // rays then; otherwise the structure check is off and the interior/Pulowski checks still apply.
        // Computed here so it's fresh for the rad-refund below and for ApplyStormExposure this tick.
        if (!needsShelterEval && !_debugShelter)
        {
            _playerEnclosed    = false;
            _enclosedCheckTick = 0;   // re-check immediately when the next storm/rain starts
        }
        else if (--_enclosedCheckTick <= 0)
        {
            // Run the roof-fan when a storm OR rad-rain is active, or when shelter-debug is on (so the
            // player can tune their base coverage with no storm running).
            _enclosedCheckTick = kEnclosedRecheckTicks;
            _playerEnclosed    = ComputeEnclosedByStructure();
        }

        // Hunter ghoul pack: persists past the storm, so check (throttled) whether the player has
        // left the area and the pack should despawn. Runs every tick regardless of storm/ghoul state.
        if (--_ghoulCleanupTick <= 0)
        {
            _ghoulCleanupTick = kGhoulCleanupTicks;
            CheckGhoulPackCleanup();
        }

        float particulateRads = player->GetActorValue(*actorValue->rads);
        float currentHP = actorValue->health ? player->GetActorValue(*actorValue->health) : 0.0f;

        // Decide whether to refund rad gain this tick:
        //  - Pulowski Preservation Protocol: while sealed in a POWERED shelter, block ALL rad gain
        //    from every source (weather, rad zones, eating/drinking irradiated items).
        //  - Full Power Armor suit: a sealed PA frame blocks all radiation (storms, rad zones,
        //    irradiated water/food). Vanilla rad-resist alone leaves a small bleed-through; a full
        //    suit should be a hard "no rads" shield, matching the in-game expectation.
        //  - Sheltered during ANY storm: storm radiation is applied by weather-conditioned magic
        //    effects (GetIsCurrentWeather), which fire regardless of a roof — so our ApplyStormExposure
        //    shelter check alone can't stop them, and the player kept gaining rads indoors. Refunding
        //    the per-tick gain whenever sheltered + a storm is active makes taking cover actually work
        //    for our managed storms AND vanilla radstorms. (Existing rads aren't healed — use RadAway.)
        // ALL THREE blockers are gated on needsShelterEval (a storm / rad-rain / rad weather is actually
        // up). The refund below is a plain rads DELTA — it cannot tell WHERE the rads came from — so an
        // ungated blocker refunds every rad from every source. Power armour and the powered shelter used
        // to be ungated, which made the player PERMANENTLY, TOTALLY rad-immune while suited: eating
        // radioactive food, drinking dirty water, swimming in the Glowing Sea, all refunded to zero,
        // forever. Gating on needsShelterEval keeps them doing their actual job (sealing you against
        // STORM radiation) while consumables and rad zones behave normally — vanilla PA already carries
        // a big rad-resist for those, and ApplyStormExposure separately honours rad-exposure resistance.
        const bool inPowerArmor = IsPlayerInPowerArmor();
        bool blockRadGain = needsShelterEval &&
            (IsPlayerInPoweredShelter() || inPowerArmor || IsPlayerSheltered());
        // Brief window after a new-game spawn-storm intercept: the radstorm can keep dosing rads for a
        // few seconds as its weather transitions out, so refund all rad gain until the window expires
        // (the one-time clear at intercept zeroes what already landed; this stops the trailing gain).
        if (_spawnRadBlockTicks > 0) { blockRadGain = true; --_spawnRadBlockTicks; }
        // Remove whatever rads were gained since the last tick (caps at the entry level; does not heal
        // existing rads). Done before the cure-delta check so it can't false-trigger RadAway.
        if (_prevRads >= 0.0f && particulateRads > _prevRads + 0.001f && blockRadGain) {
            const float gain = particulateRads - _prevRads;
            player->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *actorValue->rads, -gain);
            particulateRads = _prevRads;
        }

        // Detect RadAway / doctor treatment from the per-tick rads+HP delta — but SKIP the check
        // across a large game-time jump (sleep, wait, fast-travel): the 4Hz hook pauses during
        // those, so the first tick back would see a huge rads drop + HP gain and FALSELY fire the
        // doctor/chelation cure, wiping permanent tissue. Re-seed the baseline on a jump instead.
        const float nowDays         = GameDays();
        const float gameDaysElapsed = (_prevGameDays >= 0.0f) ? (nowDays - _prevGameDays) : 0.0f;
        const bool  timeJumped      = (_prevGameDays >= 0.0f) && (gameDaysElapsed > kCureGapDays);
        _prevGameDays = nowDays;
        // Chelation (inventory-based) runs every tick; the RadAway delta heal is skipped on a jump.
        CheckActiveCures(particulateRads, currentHP, /*deltaValid=*/!timeJumped);
        _prevRads = particulateRads;
        _prevHP   = currentHP;
        // Sleep/wait/fast-travel paused the hook: apply the elapsed game-time's tissue regression in
        // bulk (the per-tick UpdateTissueDamage below can only heal one tick's worth).
        if (timeJumped)
            ApplyRestTissueHeal(gameDaysElapsed * 24.0f, particulateRads);

        // (Health/AP/rads vitals are read+rendered by PrismaHUD_F4 directly; the Rad Mod no longer
        // computes or pushes them — it only owns the tissue "toxicity" widget below.)

        // PILLAR I — twofold radiation: combine particulate (vanilla Rads) with the
        // persistent tissue-damage pool to get the effective severity used for stages.
        float effectiveRads = UpdateTissueDamage(particulateRads);

        // Heartbeat status every ~8 seconds (32 ticks at 4Hz): full snapshot.
        static int updateCount = 0;
        if (++updateCount % 32 == 0)
        {
            const char* mode = _eslAvailable ? "ESL-perks" : "C++-fallback";
            logger->debug("[Status] particulate={:.1f} tissue={:.1f} effective={:.1f} stage={} mode={} perkIdx={}",
                particulateRads, _tissueDamage, effectiveRads, _currentStageLevel, mode, _appliedPerkIndex);
        }

        // Radstorm warning sound (plays once as a radstorm weather rolls in).
        CheckRadstormWarning();

        // Storm state machine — schedule/trigger/resolve rad storms (lethal + ambient).
        UpdateStorm();

        // Dust Storm fog roll-in — animates the dust wall in toward the player after Active-entry.
        UpdateDustFogRoll();

        // Dust Storm continuous camera dust — re-applies the blowing-dust art on a short cadence.
        UpdateDustMotes();

        // Start the dome's rotation/shader animation once its 3D attaches (native ACTI refs don't auto-start).
        UpdateDomeAnimation();

        // Ambient audio beds — per-storm bed (driven by current weather, so debug ForceWeather plays
        // it too) + a general exterior background bed. Runs every tick, independent of storm phase.
        UpdateAmbientAudio();

        // Vitals (health/AP/rads) are owned by PrismaHUD_F4 now; push only our unique tissue
        // ("toxicity") to the standalone widget that PrismaHUD positions in the unified HUD.
        RadHud::GetSingleton().PushToxic(_tissueDamage, _permanentTissueDamage);

        // Chronic tissue damage's actual consequences: wasting penalties + visceral symptoms.
        ApplyTissueEffects();

        // First-person gas-mask screen overlay + put-on/off fade. Debug override (mask:on/off/auto)
        // forces the OVERLAY for testing; the filter system uses the REAL worn state. PushMask only
        // sends on change (cheap per tick).
        const bool realMasked = IsWearingGasMask();
        const bool maskOn = _maskOverride == 1 ? true
                          : (_maskOverride == 0 ? false : realMasked);
        // (inRadWeather is computed up top with the shelter-eval flags — the roof raycast must be
        // fresh for any FLST rad weather now that choking depends on it.)

        // Worn-state transition -> mask foley. Follows the REAL worn state only, NOT the debug
        // override. _maskWasOn starts at -1 after each load so an already-worn mask syncs silently.
        const int maskNow = realMasked ? 1 : 0;
        if (_maskWasOn == -1)
            _maskWasOn = maskNow;
        else if (maskNow != _maskWasOn)
        {
            // Draw / withdraw foley riding the same transition (auto-equip included).
            {
                const bool female = IsPlayerFemale();
                RE::BSSoundHandle h{};
                Play2DSound(realMasked ? (female ? kSndMaskDrawF : kSndMaskDrawM)
                                       : (female ? kSndMaskOffF : kSndMaskOffM), h);
            }
            _maskWasOn = maskNow;
        }

        RadHud::GetSingleton().PushMask(maskOn);   // OVERLAY still follows the override (debug preview)

        // ---- Gas-mask FILTER (drain) + CONDITION (durability); a failed mask chokes in rad weather ----
        const bool filterSys = (_filterItem != nullptr);
        const bool condSys   = _conditionEnabled;
        if (filterSys || condSys)
        {
            // A filter is only CONSUMED when it is actually filtering bad air. Sheltered players, sealed
            // power armour and powered Pulowski shelters are protected (the exact same exemption the
            // CHOKING block below already uses), so the mask isn't doing any work — draining it there
            // punished correct play: the mod AUTO-EQUIPS the mask on rad-weather entry regardless of
            // cover, so a player who correctly took shelter for a ~4 min storm still burned ~80% of a
            // 300 s filter while safe indoors. Same gate on the failed-mask rad choke below, which
            // likewise ignored cover (its rads were then silently refunded by the sheltered rad-block,
            // so it only ever produced a per-tick rads flicker).
            const bool maskExposed = !IsPlayerSheltered() && !IsPlayerInPoweredShelter() &&
                                     !IsPlayerInPowerArmor();
            if (realMasked)
            {
                // filter drains while worn AND actually exposed to the air it's filtering
                if (filterSys && maskExposed && _filterCharge > 0.0f)
                    _filterCharge = std::max(0.0f, _filterCharge - (100.0f / _filterDurationSec) * 0.25f);

                // auto replace / repair when empty/broken
                if (filterSys && _filterCharge <= 0.0f && _filterAutoReplace && FilterItemCount() > 0)
                    ReplaceFilter(true);
                if (condSys && _maskCondition <= 0.0f && _conditionAutoRepair && RepairKitCount() > 0)
                    RepairMask(true);

                const bool filterDead = filterSys && _filterCharge <= 0.0f;
                const bool maskBroken = condSys && _maskCondition <= 0.0f;

                // a failed mask (dead filter OR broken) gives no protection -> breathe the bad air
                if ((filterDead || maskBroken) && maskExposed && inRadWeather && actorValue->rads)
                    player->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *actorValue->rads,
                                          _filterChokeRads * 0.25f);

                if (filterSys)
                {
                    if (filterDead) { if (_filterLowWarned < 2) { Notify("[Mask] Filter DEPLETED - replace it!", true); _filterLowWarned = 2; } }
                    else if (_filterCharge <= 20.0f && _filterLowWarned < 1) { Notify("[Mask] Filter running low.", true); _filterLowWarned = 1; }
                }
                if (condSys)
                {
                    if (maskBroken) { if (_conditionLowWarned < 2) { Notify("[Mask] Mask BROKEN - repair it!", true); _conditionLowWarned = 2; } }
                    else if (_maskCondition <= 25.0f && _conditionLowWarned < 1) { Notify("[Mask] Mask is damaged.", true); _conditionLowWarned = 1; }
                }
            }
            // The spare-filter count feeds only the HUD "xN filters" text, which is hidden while
            // unmasked. Refresh it at most ~every 8 ticks (2s) instead of walking the inventory each
            // tick; force a prompt refresh the next time a mask goes on.
            if (realMasked)
            {
                if (--_filterCountTick <= 0) { _filterSparesCache = FilterItemCount(); _filterCountTick = 8; }
            }
            else { _filterCountTick = 0; }
            RadHud::GetSingleton().PushFilter(filterSys ? _filterCharge : -1.0f,
                                              condSys ? _maskCondition : -1.0f,
                                              realMasked ? _filterSparesCache : 0, realMasked);
        }

        // ---- Choke-gate diagnostic (opt-in via the `shelter` debug command) ----
        // Dumps the live values behind every mask/choke gate once per second so we can see WHY a
        // feature is (not) firing in-game: the actual current-weather FormID and whether it's in the
        // RadWeatherList FLST, the worn-mask detection, and the shelter/PA exemptions. This is the
        // observability the log lacked when all four mask features read as "dead" (2026-07-06).
        if (_debugShelter && --_chokeDiagTick <= 0)
        {
            _chokeDiagTick = 4;  // ~1 Hz
            const std::uint32_t wid = (sky && sky->currentWeather) ? sky->currentWeather->GetFormID() : 0;
            logger->info("[ChokeDiag] weather=0x{:08X} inRadWx={} stormActive={} | realMasked={} maskOverride={} "
                         "| enclosed={} sheltered={} PA={} poweredShelter={} | filterCharge={:.0f} cond={:.0f} "
                         "chokingEnabled={} chokeTicks={} calmTicks={}",
                         wid, inRadWeather, stormActive, IsWearingGasMask(), _maskOverride,
                         _playerEnclosed, IsPlayerSheltered(), IsPlayerInPowerArmor(), IsPlayerInPoweredShelter(),
                         _filterCharge, _maskCondition, _chokingEnabled, _chokeTicks, _chokeCalmTicks);
        }

        // ---- CHOKING: unprotected breathing in rad weather damages HEALTH (GMW-style) ----
        // Full rate with no mask, reduced rate when masked but failed (dead filter / broken mask —
        // the rads choke above already handles the radiation half of that case). Sheltered players,
        // powered Pulowski shelters, and full power armor are exempt. One corner notify per episode
        // plus a throttled male/female choking one-shot.
        if (_chokingEnabled && inRadWeather && actorValue->health)
        {
            const bool exposed = !IsPlayerSheltered() && !IsPlayerInPoweredShelter() && !IsPlayerInPowerArmor();
            const bool filterDead = (_filterItem != nullptr) && _filterCharge <= 0.0f;
            const bool maskBroken = _conditionEnabled && _maskCondition <= 0.0f;
            const bool failedMask = realMasked && (filterDead || maskBroken);
            const bool choking    = exposed && (!realMasked || failedMask);

            if (choking)
            {
                _chokeCalmTicks = 0;
                if (_chokeTicks < 1000) ++_chokeTicks;

                const float rate = _chokeHealthPerSec * (realMasked ? _chokeFailedFactor : 1.0f);
                player->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *actorValue->health,
                                      -rate * 0.25f);

                // Grace: hold the notify/VO for the first ~2s so the rad-weather auto-equip can
                // mask the player without a spurious gasp on every storm entry.
                if (_chokeTicks >= 8)
                {
                    if (!_chokeNotified)
                    {
                        Notify(realMasked ? "[Mask] Your mask is FAILING - you're choking!"
                                          : "You're breathing contaminated air - mask up!", true);
                        _chokeNotified = true;
                    }
                    if (_chokeSounds && --_chokeSoundTick <= 0)
                    {
                        const bool female = IsPlayerFemale();
                        RE::BSSoundHandle h{};
                        Play2DSound(realMasked ? (female ? kSndChokeWithMaskF : kSndChokeWithMaskM)
                                               : (female ? kSndChokeNoMaskF : kSndChokeNoMaskM), h);
                        static std::mt19937 rng{ std::random_device{}() };
                        _chokeSoundTick = std::uniform_int_distribution<int>(24, 44)(rng);  // ~6-11 s
                    }
                }
            }
            else if ((_chokeTicks > 0 || _chokeNotified) && ++_chokeCalmTicks >= 40)
            {
                // Hysteresis: ~10s of sustained safety before the notify/VO re-arm, so flickering
                // in and out of cover at a roof edge doesn't spam the corner message or the gasp.
                _chokeTicks = 0; _chokeCalmTicks = 0;
                _chokeNotified = false; _chokeSoundTick = 0;
            }
        }
        else if ((_chokeTicks > 0 || _chokeNotified) && ++_chokeCalmTicks >= 40)
        {
            _chokeTicks = 0; _chokeCalmTicks = 0;
            _chokeNotified = false; _chokeSoundTick = 0;
        }

        // Auto-equip a gas mask while a rad weather is active (data-driven by the RadWeatherList FLST).
        // Acts on the weather transition only: dons a mask on ENTERING rad weather (if the player owns
        // one and isn't already masked) and removes ONLY what we auto-equipped on LEAVING — so manual
        // equip/unequip is respected. The equip flips the worn state, which the block above turns into
        // the put-on fade + overlay automatically.
        if (_radWeatherList)
        {
            const int inRadNow = inRadWeather ? 1 : 0;
            if (_wasInRadWeather == -1)
            {
                _wasInRadWeather = inRadNow;   // first eval after load: sync silently
            }
            else if (inRadNow != _wasInRadWeather)
            {
                if (inRadWeather)
                {
                    // Player: don an owned mask if not already masked.
                    if (!IsWearingGasMask())
                        if (auto* mask = FindInventoryMask())
                        {
                            SetMaskEquipped(mask, true);
                            _autoMaskEquipped = true;
                        }
                    // NPCs: mask up nearby high-process NPCs that own one.
                    EquipNearbyNpcMasks(true);
                    _npcMaskScanTick = kNpcMaskRescanTicks;
                }
                else
                {
                    if (_autoMaskEquipped)
                        if (auto* worn = GetWornMask())
                            SetMaskEquipped(worn, false);
                    _autoMaskEquipped = false;
                    EquipNearbyNpcMasks(false);   // unmask only the NPCs we masked
                }
                _wasInRadWeather = inRadNow;
            }
            else if (inRadWeather && --_npcMaskScanTick <= 0)
            {
                // Still in rad weather: re-scan so NPCs that loaded in mid-storm also mask up
                // (skips any already wearing a mask, so it is idempotent and cheap).
                _npcMaskScanTick = kNpcMaskRescanTicks;
                EquipNearbyNpcMasks(true);
            }
        }

        // Apply debuffs based on EFFECTIVE radiation (drives stage perks -> SPECIAL + Pipboy + IMAD).
        ApplyDebuffs(static_cast<uint32_t>(effectiveRads));

        // NOTE: Environmental radiation and health damage are now handled by the GAME/ESL,
        // not C++ per tick:
        //   - Environmental rads: vanilla radiation regions/hazards (Glowing Sea, water, etc.),
        //     tuned via Fallout4.esm overrides in RadiationOverhaul.esl.
        //   - Health loss: vanilla already reduces MAX HP as the Rads actor value rises.
        // This keeps the per-tick C++ work to just reading rads + swapping the stage perk
        // on stage changes. (Old ApplyEnvironmentalRadiation used non-existent location
        // keywords and did nothing; ApplyDamageTicking duplicated vanilla HP loss.)
    }
    catch (const std::exception& ex)
    {
        logger->error("[Update] Exception: {}", ex.what());
    }
}


void RadiationEngine::PollInput()
{
    // Runs EVERY frame (from the hook, before the 4Hz throttle) so quick taps are never missed.
    // DEBUG menu: Left Shift + F8, edge-detected from the reliable key-DOWN bit (0x8000).
    const bool dbgKeyDown = (GetAsyncKeyState(kDebugMenuKey) & 0x8000) != 0;
    const bool lshift     = (GetAsyncKeyState(VK_LSHIFT)     & 0x8000) != 0;
    if (dbgKeyDown && !_f8WasDown && lshift)
        RadHud::GetSingleton().ToggleDebugMenu();
    _f8WasDown = dbgKeyDown;

    // Apply a debug-menu toggle that arrived while the HUD view was still loading its DOM (the first
    // Shift+F8 during view init). No-op once nothing is pending. Runs on the verified main thread.
    RadHud::GetSingleton().PumpDeferredDebug();

    // Gas-mask filter replace keybind (INI [Filter] ReplaceKey; 0 = disabled).
    if (_filterReplaceKey != 0)
    {
        const bool rdown = (GetAsyncKeyState(_filterReplaceKey) & 0x8000) != 0;
        if (rdown && !_filterReplaceWasDown)
            ReplaceFilter(false);
        _filterReplaceWasDown = rdown;
    }
    else
    {
        _filterReplaceWasDown = false;
    }

    // Gas-mask repair keybind (INI [Condition] RepairKey; 0 = disabled).
    if (_conditionRepairKey != 0)
    {
        const bool rdown = (GetAsyncKeyState(_conditionRepairKey) & 0x8000) != 0;
        if (rdown && !_conditionRepairWasDown)
            RepairMask(false);
        _conditionRepairWasDown = rdown;
    }
    else
    {
        _conditionRepairWasDown = false;
    }
}


// ---------------------------------------------------------------------------
// PERSISTENCE — the storm play-clock + schedule survive save/load via the F4SE co-save, so the
// mod's own (35-90 min) storms accumulate play-time across sessions and actually reach their
// trigger, instead of the clock resetting to 0 on every launch.
// ---------------------------------------------------------------------------
namespace
{
    constexpr std::uint32_t kStormClockRecord  = 'SCLK';  // co-save record tag
    constexpr std::uint32_t kStormClockVersion = 1;
    constexpr std::uint32_t kFilterRecord      = 'FILT';  // co-save: gas-mask filter charge + condition
    constexpr std::uint32_t kFilterVersion     = 2;       // v2 adds mask condition after charge

    // Storm-clock record payload: three doubles (play, next, lastRadstorm) + one uint8 (scheduled).
    constexpr std::uint32_t kStormClockLen = sizeof(double) * 3 + sizeof(std::uint8_t);
    constexpr std::uint32_t kFilterV1Len = sizeof(float);
    constexpr std::uint32_t kFilterV2Len = sizeof(float) * 2;
}


void RadiationEngine::EnsureEslLoaded()
{
    if (_eslChecked) return;
    _eslChecked = true;

    auto logger = Log();
    logger->info("========== ESL RESOLUTION ({}) ==========", kEslName);

    auto* dh = RE::TESDataHandler::GetSingleton();
    if (!dh)
    {
        logger->error("[ESL] TESDataHandler singleton is NULL — cannot resolve forms.");
        logger->warn("[ESL] -> Falling back to C++ SPECIAL penalties (no Pipboy display).");
        logger->info("==========================================");
        return;
    }
    logger->debug("[ESL] TESDataHandler @ 0x{:X}", reinterpret_cast<uintptr_t>(dh));

    // Report the plugin's load-order slot. By design (see gen_esl.py) the plugin
    // ships as a REGULAR full ESP — not ESL/light — so it loads LAST and wins
    // conflicts over the rest of the load order. LookupForm() below resolves forms
    // correctly regardless of slot type, so a full plugin is the expected case.
    // Check the light index first: an ESL plugin reports a real light index AND the
    // shared full index 0xFE, so testing the full index first would misclassify it.
    const auto lightIdx = dh->GetLoadedLightModIndex(kEslName);
    const auto fullIdx  = dh->GetLoadedModIndex(kEslName);
    if (lightIdx.has_value())
        logger->warn("[ESL] '{}' present as LIGHT/ESL plugin, light index 0x{:03X} (runtime FE{:03X}) — "
                     "it will load early and LOSE conflicts; ship it as a full ESP instead.",
            kEslName, lightIdx.value(), lightIdx.value());
    else if (fullIdx.has_value())
        logger->info("[ESL] '{}' present as FULL plugin, mod index 0x{:02X} (intended — loads last, wins conflicts).",
            kEslName, fullIdx.value());
    else
        logger->error("[ESL] '{}' is NOT in the load order — not active or not deployed to Data.", kEslName);

    int found = 0;
    for (int i = 0; i < kStageCount; ++i)
    {
        const uint32_t rawID = kPerkBaseFormID + i;
        RE::TESForm* form = dh->LookupForm(rawID, kEslName);

        if (!form)
        {
            logger->error("[ESL]   perk[{}] raw 0x{:06X}: LookupForm returned NULL", i, rawID);
            _stagePerks[i] = nullptr;
            continue;
        }

        if (form->GetFormType() != RE::ENUM_FORM_ID::kPERK)
        {
            logger->error("[ESL]   perk[{}] raw 0x{:06X}: resolved form is type {} (NOT a PERK)",
                i, rawID, static_cast<int>(form->GetFormType()));
            _stagePerks[i] = nullptr;
            continue;
        }

        _stagePerks[i] = static_cast<RE::BGSPerk*>(form);
        ++found;
        logger->debug("[ESL]   perk[{}] raw 0x{:06X} -> runtime 0x{:08X} \"{}\"",
            i, rawID, _stagePerks[i]->GetFormID(), PerkName(_stagePerks[i]));
    }

    // (The radiation-sickness screen tint is an HTML overlay now; the engine IMAD 0x830 is no
    // longer resolved or triggered — the record remains in the ESP, unused.)

    // Resolve the rad-weather FormList (optional). Drives the auto-mask-equip feature: when the
    // current weather is in this list, a worn mask is auto-donned. Data-driven so the rad-weather
    // set is editable in xEdit without a rebuild.
    {
        RE::TESForm* fl = dh->LookupForm(kRadWeatherListID, kEslName);
        _radWeatherList = (fl && fl->GetFormType() == RE::ENUM_FORM_ID::kFLST)
            ? static_cast<RE::BGSListForm*>(fl) : nullptr;
        logger->info("[ESL] Rad-weather FormList (0x{:03X}): {}", kRadWeatherListID,
            _radWeatherList ? "resolved (auto-mask-equip enabled)" : "NOT found (auto-equip disabled)");
    }

    // Resolve the gas-mask filter consumable (optional). Enables the filter system: the mask drains
    // this while worn, and replacing it consumes one from the player's inventory.
    {
        RE::TESForm* f = dh->LookupForm(kGasMaskFilterID, kEslName);
        _filterItem = (f && f->GetFormType() == RE::ENUM_FORM_ID::kMISC)
            ? static_cast<RE::TESBoundObject*>(f) : nullptr;
        logger->info("[ESL] Gas-mask filter (0x{:03X}): {}", kGasMaskFilterID,
            _filterItem ? "resolved (filter system enabled)" : "NOT found (filter system disabled)");
    }

    // Resolve the gas-mask repair kit consumable (optional). Repairs the mask's condition.
    {
        RE::TESForm* f = dh->LookupForm(kGasMaskRepairKitID, kEslName);
        _repairKitItem = (f && f->GetFormType() == RE::ENUM_FORM_ID::kMISC)
            ? static_cast<RE::TESBoundObject*>(f) : nullptr;
        logger->info("[ESL] Gas-mask repair kit (0x{:03X}): {}", kGasMaskRepairKitID,
            _repairKitItem ? "resolved" : "NOT found (repair disabled)");
    }

    // Resolve the persistent tissue-damage global (Pillar I). Seed our cached value
    // from it so accumulated tissue damage survives save/load.
    {
        RE::TESForm* gForm = dh->LookupForm(kTissueGlobalID, kEslName);
        _tissueGlobal = (gForm && gForm->GetFormType() == RE::ENUM_FORM_ID::kGLOB)
            ? static_cast<RE::TESGlobal*>(gForm) : nullptr;
        if (_tissueGlobal)
        {
            _tissueDamage = _tissueGlobal->GetValue();
            logger->info("[ESL] Tissue-damage global (0x{:03X}) resolved; loaded {:.1f}", kTissueGlobalID, _tissueDamage);
        }
        else
        {
            logger->warn("[ESL] Tissue-damage global (0x{:03X}) NOT found; tissue damage disabled.", kTissueGlobalID);
        }
    }

    // Permanent (scarred) tissue global — requires chelation or doctor to cure.
    {
        RE::TESForm* gForm = dh->LookupForm(kPermTissueGlobalID, kEslName);
        _permanentTissueGlobal = (gForm && gForm->GetFormType() == RE::ENUM_FORM_ID::kGLOB)
            ? static_cast<RE::TESGlobal*>(gForm) : nullptr;
        if (_permanentTissueGlobal)
        {
            _permanentTissueDamage = _permanentTissueGlobal->GetValue();
            logger->info("[ESL] Permanent-tissue global (0x{:03X}) resolved; loaded {:.1f}", kPermTissueGlobalID, _permanentTissueDamage);
        }
        else
        {
            logger->warn("[ESL] Permanent-tissue global (0x{:03X}) NOT found; chelation system disabled.", kPermTissueGlobalID);
        }
    }

    // Storm FX quest — Papyrus script drives lethal-storm camera/controller shake (stage 10/20).
    {
        RE::TESForm* qForm = dh->LookupForm(kStormFXQuestFormID, kEslName);
        _stormFXQuest = (qForm && qForm->GetFormType() == RE::ENUM_FORM_ID::kQUST)
            ? static_cast<RE::TESQuest*>(qForm) : nullptr;
        logger->info("[ESL] Storm FX quest (0x{:03X}): {}", kStormFXQuestFormID,
            _stormFXQuest ? "resolved (camera shake via Papyrus stage 10/20)" : "NOT found");
    }

    // NOTE: EnsureQuestRunning is currently a no-op (the VM Quest.Start() path crashed — see
    // its body). StartGameEnabled starts these quests on a NEW GAME automatically; on saves
    // made before the quests existed they stay dormant until we add a crash-safe start path.
    EnsureQuestRunning(_stormFXQuest);

    // Fire hazard object for Firestorm ring spawn (from base game — always present).
    // DN121FireHazard: a large fire hazard that spawns fire particles + light + damage.
    {
        RE::TESForm* hForm = dh->LookupForm(0x1A7C6A, "Fallout4.esm");
        _fireHazardForm = (hForm && hForm->GetFormType() == RE::ENUM_FORM_ID::kHAZD)
            ? static_cast<RE::BGSHazard*>(hForm) : nullptr;
        logger->info("[ESL] Fire hazard (DN121FireHazard 0x1A7C6A): {}",
            _fireHazardForm ? "resolved (firestorm ring spawn enabled)" : "NOT found");
    }

    // Safe firestorm detonation. This spawns our zero-damage, 360-degree cinematic nuke explosion;
    // the missile Projectile::Launch ABI remains out of scope until it can be tested independently.
    {
        RE::TESForm* xForm = dh->LookupForm(0x885, kEslName);
        _nukeExplosionForm = (xForm && xForm->GetFormType() == RE::ENUM_FORM_ID::kEXPL)
            ? static_cast<RE::BGSExplosion*>(xForm) : nullptr;
        logger->info("[ESL] Firestorm distant-strike form (0x885): {}.",
            _nukeExplosionForm ? "resolved" : "MISSING");
    }

    // Storm dome layers: our scriptless FARO_StormDome01..04 ACTI copies (0x805-0x808), meshes bundled
    // from Anomaly's CloudArena. They MUST be scriptless — placing the real KORs activator
    // (KORs_Anomaly_CloudArena01, which carries KORsCloudArenaActScript + defaultdisablehavokonload)
    // natively from the 4Hz worker-thread hook crashed the game in BSScript VM ObjectBindPolicy +
    // Havok behavior-graph binding (same worker-thread VM hazard that killed SetStage). The clouds are
    // lit by a separately-spawned scriptless arena LIGHT instead (see _stormDomeLight below) — no VM.
    {
        int resolved = 0;
        for (int k = 0; k < 4; ++k)
        {
            RE::TESForm* f = dh->LookupForm(0x805 + k, kEslName);
            _stormDomeForms[k] = (f && f->GetFormType() == RE::ENUM_FORM_ID::kACTI) ? f : nullptr;
            if (_stormDomeForms[k]) ++resolved;
        }
        // Arena lights for the effect-lit cloud shapes (scriptless LIGH). These were historically resolved
        // from "KORsAnomaly Stripped.esp" (ArenaLight01 0x025334, ArenaLight02 0x02626D), which made KORs a
        // hidden runtime requirement. The two LIGH records (plus their GodRays/LensFlare sub-records) are now
        // owned by RadiationOverhaul.esp itself — copied in with permission — so KORs is no longer required.
        // BOTH lights together make the glowing vortex (broad illumination + bright lens-flare eye).
        RE::TESForm* lf = dh->LookupForm(0x889, kEslName);   // FARO_StormDomeLight (was KORs ArenaLight01)
        _stormDomeLight = (lf && lf->GetFormType() == RE::ENUM_FORM_ID::kLIGH)
            ? static_cast<RE::TESBoundObject*>(lf) : nullptr;
        RE::TESForm* ff = dh->LookupForm(0x88A, kEslName);   // FARO_StormDomeFlare (was KORs ArenaLight02, lens flare)
        _stormDomeFlare = (ff && ff->GetFormType() == RE::ENUM_FORM_ID::kLIGH)
            ? static_cast<RE::TESBoundObject*>(ff) : nullptr;
        logger->info("[ESL] Storm dome layers (FARO_StormDome01..04): {}/4 resolved; arena light {}, lens flare {}.",
            resolved, _stormDomeLight ? "resolved" : "NOT found", _stormDomeFlare ? "resolved" : "NOT found");
    }

    // Storm weathers (forced sky per type) — optional; storms still run with just the
    // screen FX if a weather record is missing.
    for (int i = 0; i < kStormTypeCount && i < kStormTypeTableSize; ++i)
    {
        const uint32_t wid = kStormTypes[i].weatherID;
        RE::TESForm* wForm = dh->LookupForm(wid, kEslName);
        _stormWeathers[i] = (wForm && wForm->GetFormType() == RE::ENUM_FORM_ID::kWTHR)
            ? static_cast<RE::TESWeather*>(wForm) : nullptr;
        logger->info("[ESL] Storm weather '{}' (0x{:03X}): {}", kStormTypes[i].name, wid,
            _stormWeathers[i] ? "resolved" : "NOT found (runs without forced sky)");

        // Per-storm fog, written to the record (the engine re-reads fogData live each frame in
        // Sky::UpdateFog, so this fully controls the on-screen fog). Distance fog (near/far/power)
        // sets the visibility wall; height fog (mid/range/HighDensityScale) gives real ground-fog
        // depth instead of the flat "painted" tint the generated records shipped (all height fields
        // were 0). The Dust Storm gets the CommonwealthDusty full-height-wall profile; the roll-in
        // (UpdateDustFogRoll) animates it inward from clear at Active-entry.
        if (auto* w = _stormWeathers[i])
        {
            const StormType& s = kStormTypes[i];
            logger->info("[StormFog] '{}' was near={:.0f} far={:.0f} pow={:.2f} hRange={:.0f} hiDens={:.2f} -> near={:.0f} far={:.0f} pow={:.2f} hRange={:.0f} hiDens={:.2f}",
                s.name, w->fogData[0], w->fogData[1], w->fogData[4], w->fogData[9], w->fogData[12],
                s.fogNear, s.fogFar, s.fogPower, s.fogHeightRange, s.fogHiDensity);
            ApplyWeatherFog(w, s.fogNear, s.fogFar, s.fogPower,
                s.fogHeightMid, s.fogHeightRange, s.fogHiDensity);

            // Dust Storm: attach the vanilla camera-dust particle VisualEffect (the screen-filling
            // blowing dust that actually reads as "blinding" — fog alone is only a haze). This is the
            // weather's referenceEffect (record field "VisualEffect", TESWeather+0xFC0); vanilla
            // CommonwealthDusty uses this exact effect. Set at runtime (no ESP/master-order surgery) so
            // the engine attaches it when the dust sky goes current. FXCameraAttachExplosionDustVE
            // (Fallout4.esm 0x1EB2D6), flags AttachToCamera + InheritRotation.
            if (i == kDustStormIndex)
            {
                RE::TESForm* veForm = dh->LookupForm(0x1EB2D6, "Fallout4.esm");
                if (veForm && veForm->GetFormType() == RE::ENUM_FORM_ID::kRFCT)
                {
                    w->referenceEffect = static_cast<RE::BGSReferenceEffect*>(veForm);
                    logger->info("[StormFX] Dust Storm camera-dust VisualEffect attached (FXCameraAttachExplosionDustVE).");
                }
                else
                {
                    logger->warn("[StormFX] Dust Storm camera-dust VE (0x1EB2D6) not found (type={}).",
                        veForm ? static_cast<int>(veForm->GetFormType()) : -1);
                }

                // Heavy camera-dust ArtObject for the WALL-ARRIVAL BURST: a short (~4s) heavier dust
                // gust fired on the player (attach-to-camera) the moment the roll-in wall reaches full,
                // layered over the steady dust VE. Nuka World's DLC04CameraAttachDustHeavyAO; fall back
                // to the vanilla FXCameraAttachExplosionDust art if Nuka World isn't present.
                RE::TESForm* aoForm = dh->LookupForm(0x0156CB, "DLCNukaWorld.esm");
                if (!aoForm || aoForm->GetFormType() != RE::ENUM_FORM_ID::kARTO)
                    aoForm = dh->LookupForm(0x1EB2D5, "Fallout4.esm");   // FXCameraAttachExplosionDust
                _dustBurstArt = (aoForm && aoForm->GetFormType() == RE::ENUM_FORM_ID::kARTO)
                    ? static_cast<RE::BGSArtObject*>(aoForm) : nullptr;
                logger->info("[StormFX] Dust wall-arrival burst art: {}.",
                    _dustBurstArt ? "resolved" : "NOT found");

                // PURPLE-SKY FIX: the generated dust weather inherited the CommonwealthGSRadstorm base
                // template's per-time IMAGESPACES (0x224586-0x224593 — blue/purple rad-storm colour
                // grades). An imagespace tints the WHOLE scene, so it rendered purple regardless of the
                // brown sky colours, and disabling our IMAD never touched it. Repoint all 8 time-of-day
                // slots to vanilla CommonwealthDusty's brown dust grades (CW_Dusty*). imageSpace[] order
                // = {Sunrise, Day, Sunset, Night, EarlySunrise, LateSunrise, EarlySunset, LateSunset}.
                static constexpr std::uint32_t kDustyImageSpaces[8] = {
                    0x1F61A2,  // [0] Sunrise      CW_DustyDAWN
                    0x1F61A5,  // [1] Day          CW_DustyDAY
                    0x1F61A6,  // [2] Sunset       CW_DustyDUSK
                    0x1F61A9,  // [3] Night        CW_DustyNIGHT
                    0x1F61A3,  // [4] EarlySunrise CW_DustyDAWN_E
                    0x1F61A4,  // [5] LateSunrise  CW_DustyDAWN_L
                    0x1F61A7,  // [6] EarlySunset  CW_DustyDUSK_E
                    0x1F61A8,  // [7] LateSunset   CW_DustyDUSK_L
                };
                int isSet = 0;
                for (int k = 0; k < 8; ++k)
                {
                    RE::TESForm* isf = dh->LookupForm(kDustyImageSpaces[k], "Fallout4.esm");
                    if (isf && isf->GetFormType() == RE::ENUM_FORM_ID::kIMGS)
                    {
                        w->imageSpace[k] = static_cast<RE::TESImageSpace*>(isf);
                        ++isSet;
                    }
                }
                logger->info("[StormFX] Dust Storm imagespaces repointed to CommonwealthDusty brown grades: {}/8.", isSet);
            }

            // AURORA CLEAR — ALL STORMS (2026-07-12): every generated storm inherited the radstorm
            // template's AURORA, Sky\RadStormSkyEffect.nif (TESWeather+0xF78) — a full-sky-dome shimmer
            // effect that renders OVER the sky colours and doesn't fit any of these storms (fire/ash/
            // sulfur/acid/dust). On dust it read as a muddy dome (the "purple"); on the others it makes
            // the transition look less clean than dust. Clear it so each storm's sky is just its (correct)
            // colours + fog + IMAD. Dust proved this reads clean.
            if (const char* auroraPath = w->aurora.GetModel(); auroraPath && auroraPath[0] != '\0')
            {
                logger->info("[StormFX] '{}' aurora was '{}' -> cleared (cleaner sky, matches dust).",
                    s.name, auroraPath);
                w->aurora.SetModel("");
            }

            // ENGINE CRASH GUARD (confirmed via crash-2026-07-07-18-21-18): Sky::UpdateSound
            // (RVA 0x650917) does `rand % (ThunderFreq * ucThunderCount)` for ANY weather that
            // carries a Thunder-type sound. Our generated storm weathers ship ThunderFreq (weatherData
            // index 0xA) == 0, so that integer modulo divides by zero on the BSJobs audio worker thread
            // -> EXCEPTION_INT_DIVIDE_BY_ZERO the moment the storm's sky becomes current. The dust storm
            // hit it first; ash/sulfur/acid/firestorm carry the same bad byte and are equally at risk.
            // Clamp any zero to a high non-zero value: the divisor is now safe, and a high frequency
            // makes real thunder ~never fire (these are dust/ash/chem storms, not thunderstorms).
            constexpr int kThunderFreqIdx = static_cast<int>(RE::TESWeather::WeatherData::kThunderFreq);
            if (w->weatherData[kThunderFreqIdx] == 0)
            {
                w->weatherData[kThunderFreqIdx] = 100;
                logger->info("[StormFix] '{}' ThunderFreq was 0 (engine div-by-zero in Sky::UpdateSound) -> set 100.",
                    kStormTypes[i].name);
            }

            // TRANSITION SPEED (weatherData[kTransDelta], TESWeather+0xA6F). Decompile-verified in
            // Sky::UpdateWeather (RVA 0x64E530): currentWeatherPct ramps over a duration of
            // `weatherData[3]/255 * (transMax - transMin) + transMin` in-game hours — i.e. this byte is a
            // normalized 0-255 knob for how LONG our BeginSmoothWeather cross-fade takes. The ESL
            // generator left it at whatever it happened to be, so the five storms rolled in at random,
            // inconsistent speeds. Author it deterministically so every storm's sky rolls in at the same
            // deliberate pace, and so it matches the ~8 s dome/siren onset sequence. Small byte = fast.
            // (Exact transMin/transMax engine constants aren't named yet, so this is tuned by feel, not
            // converted from a target duration — but it is now CONSISTENT and controllable, which it
            // was not before.) See ReverseEngineering\Organized\1.10.163\README-Weather.md.
            constexpr int   kTransDeltaIdx = static_cast<int>(RE::TESWeather::WeatherData::kTransDelta);
            constexpr std::int8_t kStormTransDelta = 24;   // brisk but visibly gradual sky roll-in
            const std::int8_t wasTrans = w->weatherData[kTransDeltaIdx];
            w->weatherData[kTransDeltaIdx] = kStormTransDelta;
            logger->info("[StormFix] '{}' TransDelta {} -> {} (uniform, tunable sky roll-in speed).",
                kStormTypes[i].name, static_cast<int>(wasTrans), static_cast<int>(kStormTransDelta));

            // WIND (decompile-verified from Sky::UpdateWind, RVA 0x650190 — see README-Weather.md).
            // Four weatherData[] bytes drive the global shader wind, all normalized 0-255 (GetWeatherData
            // rule) and all weather-transition-blended like fog:
            //   [0]    kWindSpeed      -> BSShaderManager::fWindMagnitude (cloud scroll + shader wind)
            //   [0x13] kWindTurbulence -> fWindFrequency / fWindMinSpeed / fWindMaxSpeed (gust freq + variance)
            //   [0x11] kWindDir        -> fWindAngle (base direction)
            //   [0x12] kWindDirRange   -> direction jitter range
            // Our generated storm records left these unauthored, so the storms were DEAD CALM — clouds
            // didn't scroll and there was no shader wind, which reads badly for what are literally
            // wind-driven dust / ash / chem storms. Author them by theme: the dust storm HOWLS, the
            // others scale down. weatherData is int8_t but the engine reads each byte UNSIGNED, so store
            // the byte pattern of the 0-255 value. Indices are decompile-verified; the magnitudes are a
            // design knob (transMin/transMax aren't named, so tuned by feel — but consistent now).
            constexpr int kWindSpeedIdx = static_cast<int>(RE::TESWeather::WeatherData::kWindSpeed);
            constexpr int kWindTurbIdx  = static_cast<int>(RE::TESWeather::WeatherData::kWindTurbulence);
            constexpr int kWindDirIdx   = static_cast<int>(RE::TESWeather::WeatherData::kWindDir);
            constexpr int kWindDirRngIdx= static_cast<int>(RE::TESWeather::WeatherData::kWindDirRange);
            auto asByte = [](int v) { return static_cast<std::int8_t>(static_cast<std::uint8_t>(v)); };

            int windSpeed, windTurb;
            switch (kStormTypes[i].themeId)
            {
            case 5: windSpeed = 220; windTurb = 200; break;  // Dust Storm  — howling, gusty
            case 4: windSpeed = 200; windTurb = 180; break;  // Ash Fallout — blowing ash
            case 1: windSpeed = 200; windTurb = 175; break;  // Rad Firestorm
            case 2: windSpeed = 150; windTurb = 140; break;  // Toxic Sulfur Storm
            case 3: windSpeed = 135; windTurb = 130; break;  // Acid Rain   — driving rain
            default: windSpeed = 170; windTurb = 150; break;
            }
            w->weatherData[kWindSpeedIdx]  = asByte(windSpeed);
            w->weatherData[kWindTurbIdx]   = asByte(windTurb);
            w->weatherData[kWindDirIdx]    = asByte(64);   // base heading (deterministic, not garbage)
            w->weatherData[kWindDirRngIdx] = asByte(96);   // moderate direction jitter
            logger->info("[StormFix] '{}' wind authored: speed={} turbulence={} (was dead calm).",
                kStormTypes[i].name, windSpeed, windTurb);

            // CAMERA-EFFECT TIMING (decompile-verified from Sky::UpdateEffects, RVA 0x655280 — see
            // README-Weather.md). The weather's camera-attached VisualEffect (our Dust Storm's
            // referenceEffect = FXCameraAttachExplosionDustVE, the blowing-dust motes) does NOT spawn at
            // transition start: the engine waits until `currentWeatherPct >= weatherData[kBeginEffect]/255`
            // to instantiate it, and tears the outgoing one down at `>= weatherData[kEndEffect]/255`. Both
            // are per-record CK bytes our generator left unauthored — so the dust VE appeared at a random
            // point in the roll-in (or, if the byte was high, barely at all). Author them so the camera
            // dust rolls in EARLY with the storm and clears promptly when it ends. Harmless on storms
            // with no referenceEffect (nothing to gate). Indices are CommonLibF4-named + decompile-confirmed.
            constexpr int kBeginEffectIdx = static_cast<int>(RE::TESWeather::WeatherData::kBeginEffect);
            constexpr int kEndEffectIdx   = static_cast<int>(RE::TESWeather::WeatherData::kEndEffect);
            w->weatherData[kBeginEffectIdx] = asByte(40);   // ~0.16: dust motes appear early in the roll-in
            w->weatherData[kEndEffectIdx]   = asByte(100);  // ~0.39: outgoing dust tears down as the storm clears
            logger->info("[StormFix] '{}' camera-VE thresholds authored (begin=40 end=100).",
                kStormTypes[i].name);
        }
    }

    // Commonwealth clear weather (vanilla) — forced once on a new game's first exterior so the
    // player never spawns into a rad storm (vault exit / alt-start). Matches console `fw 0002b52a`.
    {
        RE::TESForm* f = dh->LookupForm(0x0002B52A, "Fallout4.esm");
        _clearWeather = (f && f->GetFormType() == RE::ENUM_FORM_ID::kWTHR)
            ? static_cast<RE::TESWeather*>(f) : nullptr;
        logger->info("[ESL] Commonwealth clear weather (0x0002B52A): {}",
            _clearWeather ? "resolved (new-game clear-weather safety)" : "NOT found");
    }

    // Gas Masks of the Wasteland dust/sand storms (+ vanilla CommonwealthDusty). Not radioactive, but
    // they look like a storm and the region can roll one at spawn. We resolve their runtime FormIDs so
    // the new-game grace / clear-on-entry safety treat them as "bad weather to suppress at startup".
    // Absent GMW, the lookups just fail and the set stays empty (no effect).
    {
        _dustWeathers.clear();
        struct DustW { std::uint32_t local; const char* plugin; const char* name; };
        static constexpr DustW kDustWeathers[] = {
            { 0x1F61A1, "Fallout4.esm",                   "CommonwealthDusty" },
            { 0x00638C, "Gas Masks of the Wasteland.esp", "DustyLite" },
            { 0x003D78, "Gas Masks of the Wasteland.esp", "DustyCity" },
            { 0x0035DE, "Gas Masks of the Wasteland.esp", "DustyCommonwealth" },
            { 0x00638A, "Gas Masks of the Wasteland.esp", "DustyHeavy" },
            { 0x014209, "Gas Masks of the Wasteland.esp", "DustyExtreme" },
            { 0x015142, "Gas Masks of the Wasteland.esp", "DustyExtremeCopy" },
        };
        for (const auto& d : kDustWeathers)
        {
            RE::TESForm* f = dh->LookupForm(d.local, d.plugin);
            if (f && f->GetFormType() == RE::ENUM_FORM_ID::kWTHR)
                _dustWeathers.insert(f->GetFormID());
        }
        logger->info("[ESL] Dust-storm weathers (GMW + vanilla): {} resolved (suppressed at new-game startup).",
            _dustWeathers.size());
    }

    // Next-storm global — used only as an "is the storm system installed" gate; the actual
    // schedule (next-storm time + the _playSeconds play-clock) persists across saves via the F4SE
    // co-save (SaveState/LoadState), so we must NOT re-roll it here. Doing so on every first tick
    // after a load is exactly what starved the mod's own storms (they never reached their trigger).
    {
        RE::TESForm* gForm = dh->LookupForm(kNextEmissionGlobalID, kEslName);
        _nextEmissionGlobal = (gForm && gForm->GetFormType() == RE::ENUM_FORM_ID::kGLOB)
            ? static_cast<RE::TESGlobal*>(gForm) : nullptr;
        logger->info("[ESL] Storm system gate global (0x{:03X}): {}", kNextEmissionGlobalID,
            _nextEmissionGlobal ? "resolved (storms enabled)" : "NOT found (storms disabled)");
    }

    // Console debug-trigger global (optional). set RadOverhaul_ForceStorm to 1-4 in-game.
    {
        RE::TESForm* gForm = dh->LookupForm(kForceStormGlobalID, kEslName);
        _forceStormGlobal = (gForm && gForm->GetFormType() == RE::ENUM_FORM_ID::kGLOB)
            ? static_cast<RE::TESGlobal*>(gForm) : nullptr;
        logger->info("[ESL] Force-storm debug global (0x{:03X}): {}", kForceStormGlobalID,
            _forceStormGlobal ? "resolved ('set RadOverhaul_ForceStorm to 1-5')" : "NOT found");
    }

    // Storm atmosphere IMADs (0x880-0x884), one per storm type, persistent color grading.
    for (int i = 0; i < kStormTypeCount; ++i)
    {
        const uint32_t imadId = kStormImadBase + static_cast<uint32_t>(i);
        RE::TESForm* f = dh->LookupForm(imadId, kEslName);
        _stormIMADs[i] = (f && f->GetFormType() == RE::ENUM_FORM_ID::kIMAD)
            ? static_cast<RE::TESImageSpaceModifier*>(f) : nullptr;
        logger->info("[ESL] Storm IMAD '{}' (0x{:03X}): {}", kStormTypes[i].name, imadId,
            _stormIMADs[i] ? "resolved" : "NOT found (runs without engine color grading)");
    }

    // Feral ghoul leveled lists from Fallout4.esm — spawned during lethal storms.
    {
        RE::TESForm* f = dh->LookupForm(0x0758AE, "Fallout4.esm");
        _stormGhoulForm = (f && f->GetFormType() == RE::ENUM_FORM_ID::kLVLN)
            ? static_cast<RE::TESBoundObject*>(f) : nullptr;
        logger->info("[ESL] Feral ghoul leveled list (LCharFeralGhoul 0x0758AE): {}",
            _stormGhoulForm ? "resolved" : "NOT found (no ghoul spawns)");
    }
    {
        RE::TESForm* f = dh->LookupForm(0x0D39EE, "Fallout4.esm");
        _stormGlowingOneForm = (f && f->GetFormType() == RE::ENUM_FORM_ID::kLVLN)
            ? static_cast<RE::TESBoundObject*>(f) : nullptr;
        logger->info("[ESL] Glowing One leveled list (LCharFeralGhoulGlowingOne 0x0D39EE): {}",
            _stormGlowingOneForm ? "resolved" : "NOT found (no glowing one spawns)");
    }
    // ActorTypeGhoul keyword — used to spare/heal ghoul NPCs during storms (they thrive in rads).
    {
        RE::TESForm* f = dh->LookupForm(kActorTypeGhoulKeyword, "Fallout4.esm");
        _ghoulKeyword = (f && f->GetFormType() == RE::ENUM_FORM_ID::kKYWD)
            ? static_cast<RE::BGSKeyword*>(f) : nullptr;
        logger->info("[ESL] ActorTypeGhoul keyword (0x{:06X}): {}", kActorTypeGhoulKeyword,
            _ghoulKeyword ? "resolved (ghoul NPCs healed, not harmed, by storms)" : "NOT found");
    }
    // ArmorTypePower keyword — marks PA armor pieces. A full PA suit fully blocks rads + storm HP loss.
    {
        RE::TESForm* f = dh->LookupForm(0x4D8A1, "Fallout4.esm");
        _armorTypePowerKeyword = (f && f->GetFormType() == RE::ENUM_FORM_ID::kKYWD)
            ? static_cast<RE::BGSKeyword*>(f) : nullptr;
        logger->info("[ESL] ArmorTypePower keyword (0x4D8A1): {}",
            _armorTypePowerKeyword ? "resolved (full PA suit fully blocks rads)" : "NOT found");
    }

    // Chelation compound (our ALCH item) — consuming it is the reliable cure for permanent tissue.
    {
        RE::TESForm* f = dh->LookupForm(0x860, kEslName);
        // Type-check before the cast (matches every other lookup here): chelation is our ALCH item,
        // and ALCH -> TESBoundObject is single-inheritance so the cast is valid once confirmed. This
        // guards GetInventoryObjectCount(_chelationItem) in CheckActiveCures against a mis-typed form
        // (e.g. an ESL regen that changed 0x860) rather than handing it a bad pointer.
        _chelationItem = (f && f->GetFormType() == RE::ENUM_FORM_ID::kALCH)
            ? static_cast<RE::TESBoundObject*>(f) : nullptr;
        logger->info("[ESL] Chelation compound (0x860): {}",
            _chelationItem ? "resolved (permanent-tissue cure)" : "NOT found");
    }

    // WorkshopNPC faction — settlers; spared from the lethal-storm peak burst.
    {
        RE::TESForm* f = dh->LookupForm(0x000337F3, "Fallout4.esm");
        _workshopNPCFaction = (f && f->GetFormType() == RE::ENUM_FORM_ID::kFACT)
            ? static_cast<RE::TESFaction*>(f) : nullptr;
        logger->info("[ESL] WorkshopNPC faction (0x000337F3): {}",
            _workshopNPCFaction ? "resolved (settlers spared from storm peak)" : "NOT found");
    }

    _eslAvailable = (found == kStageCount);
    if (_eslAvailable)
        logger->info("[ESL] SUCCESS: {}/{} stage perks resolved. Pipboy PERKS/STATUS path ACTIVE.",
            found, kStageCount);
    else
        logger->warn("[ESL] PARTIAL: {}/{} perks resolved. Using C++ SPECIAL fallback (no Pipboy display).",
            found, kStageCount);
    logger->info("==========================================");
}


// ---------------------------------------------------------------------------
// STORM SYSTEM — our self-contained versions of NAC's rad storms.
// Lethal blowouts (Ash Fallout, Firestorm) + ambient hazards (Sulfur, Acid Rain).
// ---------------------------------------------------------------------------

float RadiationEngine::GameDays() const
{
    // GameDaysPassed global is the reliable game-time source. (Calendar::rawDaysPassed
    // reads 0 in practice on this runtime, which broke the old scheduler.)
    auto* cal = RE::Calendar::GetSingleton();
    if (cal && cal->gameDaysPassed)
        return cal->gameDaysPassed->GetValue();
    return 0.0f;
}


void RadiationEngine::SaveState(const F4SE::SerializationInterface* a_intfc) const
{
    auto logger = Log();
    if (!a_intfc->OpenRecord(kStormClockRecord, kStormClockVersion))
    {
        logger->error("[Save] Could not open storm-clock record — schedule will not persist.");
        return;
    }
    a_intfc->WriteRecordData(_playSeconds);
    a_intfc->WriteRecordData(_nextStormPlaySec);
    a_intfc->WriteRecordData(_lastRadstormSec);
    const std::uint8_t scheduled = _stormScheduled ? 1 : 0;
    a_intfc->WriteRecordData(scheduled);
    logger->info("[Save] Storm clock persisted (play={:.0f}s, next={:.0f}s, scheduled={}).",
        _playSeconds, _nextStormPlaySec, _stormScheduled);

    // Gas-mask filter charge + mask condition (separate record so the storm-clock format is untouched).
    if (a_intfc->OpenRecord(kFilterRecord, kFilterVersion))
    {
        a_intfc->WriteRecordData(_filterCharge);
        a_intfc->WriteRecordData(_maskCondition);
    }
}


void RadiationEngine::LoadState(const F4SE::SerializationInterface* a_intfc)
{
    auto logger = Log();
    std::uint32_t type = 0, version = 0, length = 0;

    // SaveState writes one SCLK record. Older co-saves can carry duplicates, but their ordering does not
    // prove which clock is current. Only one valid record, or repeated field-identical records, may restore
    // state. Conflicting valid records fail safe and schedule a fresh storm rather than arbitrarily moving
    // the monotonic play clock backward or postponing storms by an unbounded amount.
    std::vector<RadiationSerialization::StormClockState> clockCandidates;
    int    clockSeen = 0, clockRejected = 0;

    const auto readExact = [a_intfc]<class T>(T& a_value) {
        return a_intfc->ReadRecordData(a_value) == sizeof(T);
    };

    while (a_intfc->GetNextRecordInfo(type, version, length))
    {
        if (type == kFilterRecord)
        {
            float charge = 100.0f;
            float condition = 100.0f;
            const bool isV1 = version == 1 && length == kFilterV1Len;
            const bool isV2 = version == kFilterVersion && length == kFilterV2Len;
            const bool read = isV1 ? readExact(charge) :
                              isV2 ? readExact(charge) && readExact(condition) : false;
            if (!read || !std::isfinite(charge) || charge < 0.0f || charge > 100.0f ||
                (isV2 && (!std::isfinite(condition) || condition < 0.0f || condition > 100.0f)))
            {
                logger->warn("[Load] Filter record v{} len {} is invalid or truncated — keeping defaults.", version, length);
                continue;
            }
            _filterCharge = charge;
            if (isV2)
                _maskCondition = condition;
            logger->info("[Load] Gas-mask filter restored (charge {:.0f}%, condition {:.0f}%).",
                _filterCharge, _maskCondition);
            continue;
        }
        if (type != kStormClockRecord)
        {
            logger->warn("[Load] Unknown co-save record 0x{:08X} (len {}) — skipping.", type, length);
            continue;
        }
        ++clockSeen;
        if (version != kStormClockVersion)
        {
            ++clockRejected;
            logger->warn("[Load] Storm-clock record #{} v{} != v{} — ignoring.", clockSeen, version, kStormClockVersion);
            continue;   // F4SE re-syncs to the next record on the following GetNextRecordInfo.
        }
        if (length != kStormClockLen)
        {
            ++clockRejected;
            logger->warn("[Load] Storm-clock record #{} bad length {} (expected {}) — ignoring.",
                clockSeen, length, kStormClockLen);
            continue;
        }
        double p = 0.0, n = 0.0, l = 0.0;
        std::uint8_t s = 0;
        const bool read = readExact(p) && readExact(n) && readExact(l) && readExact(s);
        const RadiationSerialization::StormClockState candidate{ p, n, l, s != 0 };
        if (!read || (s != 0 && s != 1) || !RadiationSerialization::IsValidStormClock(candidate))
        {
            ++clockRejected;
            logger->warn("[Load] Storm-clock record #{} is invalid or truncated (play={}, next={}, last={}, sched={}) — ignoring.",
                clockSeen, p, n, l, static_cast<int>(s));
            continue;
        }
        clockCandidates.push_back(candidate);
    }

    if (clockCandidates.size() == 1 ||
        (clockCandidates.size() > 1 && RadiationSerialization::ClassifyStormClockCandidates(clockCandidates) ==
            RadiationSerialization::StormClockCandidateDisposition::kRestore))
    {
        const auto& clock = clockCandidates.front();
        _playSeconds      = clock.playSeconds;
        _nextStormPlaySec = clock.nextStormPlaySeconds;
        _lastRadstormSec  = clock.lastRadstormSeconds;
        _stormScheduled   = clock.scheduled;
        logger->info("[Load] Storm clock restored (play={:.0f}s, next={:.0f}s, scheduled={}){}.",
            _playSeconds, _nextStormPlaySec, _stormScheduled, clockCandidates.size() > 1 ? " [identical duplicates]" : "");
    }
    else if (!clockCandidates.empty())
    {
        logger->warn("[Load] {} conflicting valid storm-clock records — refusing to choose a clock; scheduling fresh.",
            clockCandidates.size());
        ScheduleNextStorm();
    }
    else if (clockSeen > 0)
    {
        logger->warn("[Load] {} storm-clock record(s) present but none valid ({} rejected) — scheduling fresh.",
            clockSeen, clockRejected);
        ScheduleNextStorm();
    }
    // No SCLK record at all: RevertState's fresh defaults (run before this callback) stand.
}


void RadiationEngine::RevertState()
{
    // Runs on new game and before each load: clean defaults so a fresh game / a save with no
    // co-save data starts the clock at 0 rather than inheriting the previous game's state.
    _playSeconds      = 0.0;
    _nextStormPlaySec = 0.0;
    _lastRadstormSec  = -1.0e30;
    _stormScheduled   = false;
    _filterCharge     = 100.0f;   // fresh full filter for a new game / co-saveless load
    _filterLowWarned  = 0;
    _maskCondition    = 100.0f;
    _conditionLowWarned = 0;
    Log()->info("[Revert] Storm clock reset (new game / pre-load).");
}


void RadiationEngine::Notify(const char* message, bool warning) const
{
    // Vanilla HUD corner message — works with or without PrismaUI. throttle=true so a
    // repeated message doesn't spam the queue.
    RE::SendHUDMessage::ShowHUDMessage(message, "", true, warning);
}


void RadiationEngine::OnNewGame()
{
    // Arm the one-shot clear-weather safety; fired by UpdateStorm on the first exterior tick.
    _forceClearOnEntry = true;
}


void RadiationEngine::OnPostLoad()
{
    auto logger = Log();

    // Re-seed tissue fields from globals on every load so switching saves doesn't
    // carry the previous run's in-memory values into a fresh character's session.
    if (_tissueGlobal)
        _tissueDamage = _tissueGlobal->GetValue();
    if (_permanentTissueGlobal)
        _permanentTissueDamage = _permanentTissueGlobal->GetValue();

    // Reset cure-detection baselines so the first tick after a load never sees a cross-save
    // rads/HP delta or inventory-count change and false-fires a cure (wiping permanent tissue).
    // -1 sentinel: also skips the sheltered rad-gain refund on that first tick, which otherwise
    // read the entire saved rad pool as "gain since last tick" and erased it whenever the player
    // loaded in power armor / a powered shelter / sheltered rad weather.
    _prevRads = -1.0f;
    _prevHP   = -1.0f;
    _prevGameDays = -1.0f;
    _chelationCountInit = false;

    // Re-sync the gas-mask fade tracker so an already-worn mask on load syncs silently (no blink),
    // and cancel any pending fade-back from the previous session.
    _maskWasOn = -1;

    // Choking trackers are session-local: reset so a different save doesn't inherit the previous
    // one's grace/hysteresis state (e.g. skipping the 2s grace on its first exposure).
    _chokeTicks = 0;
    _chokeCalmTicks = 0;
    _chokeSoundTick = 0;
    _chokeNotified = false;

    // Re-sync the rad-weather auto-equip tracker so loading into rad weather syncs silently (no
    // auto-equip on load); we only act on weather transitions during play. Clear the NPC mask set
    // too — actor pointers/handles do not carry across loads, and the re-scan will repopulate it.
    _wasInRadWeather = -1;
    _autoMaskEquipped = false;
    _npcMaskScanTick = 0;
    _autoMaskedNpcs.clear();

    // The engine clears kTemporary AV modifiers on load, but our trackers persist in the DLL singleton.
    // Reset them so the radiation SPECIAL penalty re-applies fresh next tick (ApplyClampedSpecial) — a
    // stale non-zero tracker would otherwise match `desired` and silently skip re-applying the penalty.
    _appliedEndPenalty = 0;
    _appliedStrPenalty = 0;
    // Same for the tissue-wasting penalties: the engine drops kTemporary modifiers on load, so a stale
    // non-zero tracker would match `desired` and silently skip re-applying the penalty.
    _appliedCarryPen = 0.0f;
    _appliedHealPen  = 0.0f;
    _appliedSpeedPen = 0.0f;
    _appliedApPen    = 0.0f;
    _symptomTick     = 0;
    _symptomWarned   = 0;

    // Force-heal any SPECIAL stat that loaded in below the game floor of 1 (corruption / foreign
    // negative modifiers — the reported "-10"). Safe: only ever touches a genuinely broken stat.
    DispelNegativeSpecials();

    // NOTE: quest force-start is done in EnsureEslLoaded (after the quests resolve). OnPostLoad
    // runs BEFORE that lazy ESL load on first load, so the quests are still null here.

    // Storm: a forced weather override never survives a load, so abandon any ACTIVE storm event
    // (reset the phase to Idle, clear its FX/refs). We deliberately do NOT touch _stormScheduled /
    // _nextStormPlaySec on a normal mid-gap load — the schedule and the _playSeconds play-clock
    // persist via the F4SE co-save, so the next storm keeps its place across reloads instead of
    // being pushed a fresh 35-90 min into the future every load (which previously meant the mod's
    // storms never fired).
    //
    // EXCEPTION: if the persisted schedule is already PAST DUE (the save was made during/after the
    // storm it triggered), leaving it be would instantly fire a brand-new, differently-typed storm
    // on the very first Idle tick after EVERY reload of that save.
    //
    // This used to re-arm a deliberately SHORT 4-8 min window ("the abandoned storm returns soon").
    // That was the #1 cause of the "storms are back-to-back" reports: storms are lethal, so players
    // die -> reload -> the save is past-due -> a storm hits 4-8 min later -> they die -> reload...
    // a storm death-spiral. Any save made during/after a storm reproduces it on EVERY load.
    // Re-arm the FULL configured gap instead ([Storm] MinMinutes/MaxMinutes) so a reload can never
    // produce a short-gap storm.
    if (_stormScheduled && _playSeconds >= _nextStormPlaySec)
    {
        ScheduleNextStorm();
        logger->info("[Storm] Loaded a save with a past-due storm schedule — re-armed a full gap out "
                     "(was 4-8 min, which caused back-to-back storms on reload).");
    }
    ClearFireRefs();
    ClearStormGhouls();
    ClearStormIMAD();
    TriggerStormFX(false);  // clear shake left dangling by the load
    // Native dome refs are temporary (not force-persisted), so a load leaves stale handles in our
    // vector pointing at refs the engine already dropped — reset the tracker so the next storm starts
    // clean. (handle.get() on a dead ref returns null, so this is safe even if the refs are gone.)
    ClearStormDome();
    // On the session's FIRST load the quest/IMAD forms are still null (EnsureEslLoaded runs on the
    // first Update tick), so the two calls above no-op — latch a re-fire for after form resolution,
    // otherwise a save made mid-storm resumes its persisted Papyrus rumble loop with no stopper.
    _pendingLoadFXCleanup = true;
    if (_stormSoundHandle.IsValid()) _stormSoundHandle.FadeOutAndRelease(500);  // kill leftover storm audio
    if (_ambientBedHandle.IsValid()) _ambientBedHandle.FadeOutAndRelease(500);  // and the general bed
    _curStormBed = nullptr; _curAmbientBed = nullptr;  // re-derived from weather after load
    _stormPhase        = StormPhase::Idle;
    _shakeBurstStep    = -1;   // disarm shake (direct reset bypasses EnterStormPhase teardown); a save made
    _shakeRumbleTick   = -1;   // mid-lethal-storm otherwise leaves these armed and the NEXT storm rumbles
    _stormTicksLeft    = 0;
    _activeStormType   = -1;
    _forcedStormType   = -1;
    _forceStormNow     = false;
    _stormFXVisible    = false;
    _stormNukeFired    = false;
    _firestormAwaitingNuke = false;  // drop any mid-firestorm nuke-wait so a load can't strand Active
    _nukeAttempts      = 0;
    _nukeWaitTicks     = 0;
    _shakeBurstRetries = 0;
    _debugSkipToActive = false;
    _stormRollInTicks  = 0;   // clear any mid-firestorm-roll-in state so a stale value can't force the HUD theme
    _dustBuildupActive = false; // else a save loaded mid-dust-warning leaves this set and the NEXT storm's
    _stormPeakFired    = false; // Warning phase is wrongly suppressed by the inRadWeather build-up gate
    // NOTE: _stormScheduled / _nextStormPlaySec intentionally preserved (persisted schedule, see above).
    _stormExposedTicks = 0;
    _weatherForced     = false;
    _debugWeatherHold  = false;   // never let a debug weather hold survive a load
    RadHud::GetSingleton().PushStorm(static_cast<int>(StormPhase::Idle), 0, 0.0f, true, false);

    // Sleep / wait healing: measure elapsed game time since the last save loaded.
    auto* cal = RE::Calendar::GetSingleton();
    if (cal)
    {
        float currentTime = GameDays();
        if (_lastGameTime > 0.0f && currentTime > _lastGameTime)
        {
            float hoursPassed = (currentTime - _lastGameTime) * 24.0f;
            // Sanity bounds: ignore sub-30-min loads (fast travel, quick saves)
            // and implausible jumps >48 h that suggest a time-skip or first load.
            if (hoursPassed >= 0.5f && hoursPassed <= 48.0f)
            {
                // Same per-game-hour rate as live rest, gated on current rads.
                float rads = 0.0f;
                if (auto* p = RE::PlayerCharacter::GetSingleton())
                    if (auto* av = RE::ActorValue::GetSingleton(); av && av->rads)
                        rads = p->GetActorValue(*av->rads);
                ApplyRestTissueHeal(hoursPassed, rads);
            }
        }
        _lastGameTime = currentTime;
    }
    else
    {
        logger->warn("[Sleep] RE::Calendar unavailable; sleep healing skipped this load.");
    }
}
