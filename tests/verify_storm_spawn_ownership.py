from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"

actors = (SRC / "RadEngine_Storm_Actors.cpp").read_text(encoding="utf-8")
weather = (SRC / "RadEngine_Storm_Weather.cpp").read_text(encoding="utf-8")
weather_core = (SRC / "RadEngine_Weather.cpp").read_text(encoding="utf-8")
dome = (SRC / "RadEngine_Storm_Dome.cpp").read_text(encoding="utf-8")
storm = (SRC / "RadEngine_Storm.cpp").read_text(encoding="utf-8")
storm_update = (SRC / "RadEngine_Storm_Update.cpp").read_text(encoding="utf-8")
mask = (SRC / "RadEngine_Mask.cpp").read_text(encoding="utf-8")
debug = (SRC / "RadEngine_Debug.cpp").read_text(encoding="utf-8")
hooks = (SRC / "Hooks.cpp").read_text(encoding="utf-8")
dome_hooks = (SRC / "Hooks_DomeFollow.cpp").read_text(encoding="utf-8")
lifecycle_dispatch = (SRC / "RadLifecycleDispatch.cpp").read_text(encoding="utf-8")
main = (SRC / "main.cpp").read_text(encoding="utf-8")
radiation_engine = (SRC / "RadiationEngine.cpp").read_text(encoding="utf-8")
radiation_header = (SRC / "RadiationEngine.h").read_text(encoding="utf-8")
radhud = (SRC / "RadHud.cpp").read_text(encoding="utf-8")

ghoul_spawn = actors.split("void RadiationEngine::SpawnStormGhouls()", 1)[1].split(
    "void RadiationEngine::CheckGhoulPackCleanup()", 1
)[0]

for token in (
    "auto* cell = player->GetParentCell();",
    "!cell->IsExterior()",
    "!cell->IsAttached()",
    "cell->worldSpace != world",
):
    assert token in ghoul_spawn, f"storm ghoul spawn is missing ownership gate: {token}"

for token in (
    "std::size_t liveHunters = 0;",
    "std::size_t unresolvedHunters = 0;",
    "unresolved during streaming; deferring pack replacement",
    "ref->GetFormType() == RE::ENUM_FORM_ID::kACHR",
    "static_cast<RE::Actor*>(ref)",
    "actor && !actor->IsDead(false)",
    "Existing hunter pack still active",
    "Previous hunter pack defeated",
    "ClearStormGhouls();",
):
    assert token in ghoul_spawn, f"storm ghoul pack bound is missing: {token}"

for token in (
    "data.initiallyDisabled = true;",
    "data.clearStillLoadingFlag = true;",
    "actor->Enable(true);",
    "actor->ClampToGround();",
    "actor->GetPosition();",
    "actor->Get3D() != nullptr",
):
    assert token in ghoul_spawn, f"storm ghoul placement activation is missing: {token}"

assert "ref->As<RE::Actor>()" not in ghoul_spawn
assert "_stormGhoulRefs.erase" not in ghoul_spawn

pending_retry = actors.split("void RadiationEngine::RetryPendingStormGhouls()", 1)[1].split(
    "void RadiationEngine::RetryStormGhoulCleanup()", 1
)[0]
for token in (
    "_stormGhoulPendingRefs",
    "handle.get()",
    "actor->Enable(true);",
    "actor->ClampToGround();",
    "_stormGhoulRefs.push_back(handle);",
    "kStormGhoulPendingTimeoutTicks",
):
    assert token in pending_retry, f"pending storm ghoul retry is missing: {token}"
assert "stillPending.push_back(handle);" in pending_retry
assert "ref->Enable(true);" in pending_retry
assert "ref->SetDelete(true);" in pending_retry
assert "_stormGhoulCleanupRefs.insert" in pending_retry
assert "allowing future pack rolls" in pending_retry
assert "_stormGhoulPendingRefs.push_back(handle);" in ghoul_spawn
assert "else if (ref)" not in ghoul_spawn
assert "rejected" not in ghoul_spawn
assert "if (ref)\n                    ref->Enable(true);" in ghoul_spawn

cleanup_retry = actors.split("void RadiationEngine::RetryStormGhoulCleanup()", 1)[1].split(
    "void RadiationEngine::RetireUnresolvedStormGhouls()", 1
)[0]
for token in (
    "_stormGhoulCleanupRefs",
    "ref->Disable();",
    "ref->SetDelete(true);",
    "stillPending.push_back(handle);",
):
    assert token in cleanup_retry, f"storm ghoul cleanup queue is missing: {token}"

live_retirement = actors.split("void RadiationEngine::RetireUnresolvedStormGhouls()", 1)[1].split(
    "void RadiationEngine::ResetStormGhoulStateForLoad()", 1
)[0]
for token in (
    "_stormGhoulRefs",
    "_stormGhoulUnresolvedTicksByHandle",
    "const auto handleValue = handle.get_handle();",
    "if (handle.get())",
    "++_stormGhoulUnresolvedTicksByHandle[handleValue]",
    "_stormGhoulUnresolvedTicksByHandle.erase(handleValue);",
    "kStormGhoulPendingTimeoutTicks",
    "_stormGhoulCleanupRefs.push_back(handle);",
    "pack replacement is no longer blocked",
):
    assert token in live_retirement, f"unresolved live ghoul retirement is missing: {token}"
assert "++_stormGhoulUnresolvedTicks[" not in live_retirement
assert "std::size_t unresolved" not in live_retirement

unresolved_ages = {}
retired = []
for _ in range(119):
    for handle in ("A", "B"):
        if handle == "A":
            unresolved_ages.pop(handle, None)
        else:
            unresolved_ages[handle] = unresolved_ages.get(handle, 0) + 1
            if unresolved_ages[handle] >= 120:
                retired.append(handle)
                unresolved_ages.pop(handle)
assert retired == []
assert unresolved_ages == {"B": 119}
unresolved_ages["B"] += 1
assert unresolved_ages["B"] == 120
assert "A" not in retired

load_reset = actors.split("void RadiationEngine::ResetStormGhoulStateForLoad()", 1)[1].split(
    "void RadiationEngine::SpawnStormGhouls()", 1
)[0]
for token in (
    "_stormGhoulRefs.clear();",
    "_stormGhoulPendingRefs.clear();",
    "_stormGhoulCleanupRefs.clear();",
    "_stormGhoulPendingTicks = 0;",
    "_stormGhoulUnresolvedTicksByHandle.clear();",
    "_ghoulPackWorldID = 0;",
):
    assert token in load_reset, f"load ghoul state reset is missing: {token}"

update_body = radiation_engine.split("void RadiationEngine::Update()", 1)[1].split(
    "void RadiationEngine::PollInput()", 1
)[0]
assert "RetryStormGhoulCleanup();" in update_body
assert "RetryPendingStormGhouls();" in update_body
assert "RetireUnresolvedStormGhouls();" in update_body
assert update_body.find("RetryStormGhoulCleanup();") < update_body.find("CheckGhoulPackCleanup();")
assert update_body.find("RetryPendingStormGhouls();") < update_body.find("CheckGhoulPackCleanup();")
assert update_body.find("RetireUnresolvedStormGhouls();") < update_body.find("CheckGhoulPackCleanup();")

revert_state = radiation_engine.split("void RadiationEngine::RevertState()", 1)[1].split(
    "void RadiationEngine::Notify", 1
)[0]
assert "ResetStormGhoulStateForLoad();" in revert_state

postload = radiation_engine.split("void RadiationEngine::OnPostLoad()", 1)[1]
assert "ResetStormGhoulStateForLoad();" in postload

clear_ghouls = actors.split("void RadiationEngine::ClearStormGhouls()", 1)[1].split(
    "void RadiationEngine::ApplyStormIMAD", 1
)[0]
for token in (
    "_stormGhoulCleanupRefs.push_back(handle);",
    "_stormGhoulPendingRefs.clear();",
    "_stormGhoulUnresolvedTicksByHandle.clear();",
    "moved to cleanup ownership",
):
    assert token in clear_ghouls, f"pending storm ghoul cleanup is missing: {token}"
assert "if (_stormGhoulRefs.empty() && _stormGhoulPendingRefs.empty()) return;" in clear_ghouls
assert "_stormGhoulClearPending" not in actors

ghoul_cleanup = actors.split("void RadiationEngine::CheckGhoulPackCleanup()", 1)[1].split(
    "void RadiationEngine::ClearStormGhouls()", 1
)[0]
assert "Player went indoors. Despawning the hunter pack." in ghoul_cleanup
assert "Player ownership unresolved during streaming; deferring pack cleanup." in ghoul_cleanup
unresolved = ghoul_cleanup.split(
    "if (!cell || !cell->IsExterior() || !cell->IsAttached() || !world || cell->worldSpace != world)", 1
)[1].split("const std::uint32_t wid", 1)[0]
assert "ClearStormGhouls();" not in unresolved
assert "return;" in unresolved

fire_ring = weather.split("void RadiationEngine::SpawnFireRing()", 1)[1].split(
    "bool RadiationEngine::SpawnDistantNuke()", 1
)[0]

for token in (
    "auto* cell = player->GetParentCell();",
    "!cell->IsExterior()",
    "!cell->IsAttached()",
    "cell->worldSpace != world",
):
    assert token in fire_ring, f"fire-ring spawn is missing ownership gate: {token}"

release = weather.split("void RadiationEngine::ReleaseStormWeather()", 1)[1].split(
    "void RadiationEngine::ForceClearAndSilence()", 1
)[0]
already_current = release.split("if (sky->currentWeather == target)", 1)[1].split("return;", 1)[0]
assert "_weatherHandoffSettleTicks = 0;" in already_current
assert "_weatherHandoffWaitTicks = 0;" in already_current

force_clear = weather.split("void RadiationEngine::ForceClearAndSilence()", 1)[1].split(
    "void RadiationEngine::SpawnFireRing()", 1
)[0]
assert "if (!sky || !_clearWeather) return;" not in force_clear
assert "if (!sky) return;" in force_clear
release_override = force_clear.find("sky->ReleaseWeatherOverride();")
clear_guard = force_clear.find("if (!_clearWeather) return;")
assert release_override != -1
assert clear_guard != -1
assert release_override < clear_guard

radstorm_warning = weather_core.split("void RadiationEngine::CheckRadstormWarning()", 1)[1].split(
    "void RadiationEngine::BeginSmoothWeather", 1
)[0]
load_shelter_reset = radstorm_warning.split("if (_maskWasOn == -1)", 1)[1].split(
    "// Cell-exit grace:", 1
)[0]
for token in (
    "_playerEnclosed = false;",
    "_enclosedCheckTick = 0;",
    "_prevSheltered = true;",
    "_cellExitGraceTicks = 0;",
    "_playerEnclosed = ComputeEnclosedByStructure();",
    "_enclosedCheckTick = kEnclosedRecheckTicks;",
):
    assert token in load_shelter_reset, f"post-load shelter cache reset is missing: {token}"
assert load_shelter_reset.find("_playerEnclosed = false;") < load_shelter_reset.find(
    "_playerEnclosed = ComputeEnclosedByStructure();"
)
assert "BeginSmoothWeather(revertTo, /*lockOverride=*/false);" in radstorm_warning

smooth_weather = weather_core.split("void RadiationEngine::BeginSmoothWeather", 1)[1].split(
    "void RadiationEngine::CaptureWeatherReturnTarget", 1
)[0]
same_current = smooth_weather.split("if (sky->currentWeather == a_target)", 1)[1].split("return;", 1)[0]
assert "if (a_lockOverride)" in same_current
assert "sky->overrideWeather = a_target;" in same_current
assert "_weatherForced = true;" in same_current
assert smooth_weather.find("_releaseWeatherOverrideWhenSettled && a_target != _weatherReturnTarget") < smooth_weather.find(
    "if (sky->currentWeather == a_target)"
)

begin_storm = storm.split("void RadiationEngine::BeginStorm(int typeIndex)", 1)[1].split(
    "void RadiationEngine::EnterWarningPhase()", 1
)[0]
assert "_stormRollInTicks  = 0;" in begin_storm
assert "_fleeTick          = 0;" in begin_storm
assert "const bool debugForced = _forcedStormType >= 0;" in begin_storm
assert "(debugForced && _forcedLeadTicks >= 0)" in begin_storm

new_game_clear = storm_update.split("if (_forceClearOnEntry)", 1)[1].split(
    "if (!_nextEmissionGlobal) return;", 1
)[0]
assert "ForceWeather(_clearWeather, true)" not in new_game_clear
assert "ForceWeather(_clearWeather, false)" in new_game_clear
startup_release = new_game_clear.find("sky->ReleaseWeatherOverride();")
startup_force = new_game_clear.find("ForceWeather(_clearWeather, false)")
assert startup_release != -1
assert startup_force != -1
assert startup_release < startup_force

dome_lifecycle = dome.split("void RadiationEngine::UpdateStormDomeLifecycle()", 1)[1].split(
    "void RadiationEngine::UpdateStormDomeFollow()", 1
)[0]
unresolved_owner = dome_lifecycle.split(
    "if (!IsValidDomeOwner(player, playerCell, playerWorld))", 1
)[1].split("if (!_stormDomeUp)", 1)[0]
assert "ClearStormDome();" not in unresolved_owner
assert "return;" in unresolved_owner

dome_follow = dome.split("void RadiationEngine::UpdateStormDomeFollow()", 1)[1].split(
    "void RadiationEngine::UpdateDustMotes()", 1
)[0]
dome_preflight, dome_move = dome_follow.split("if (!needsMove) return;", 1)
assert "if (!IsValidDomeReference(ref, _stormDomeWorld) || !ref->Get3D()) return;" in dome_preflight
assert "ref->Update3DPosition(true);" in dome_move
assert "IsValidDomeReference" not in dome_move
assert "ref->Get3D()" not in dome_move

warning_phase = storm_update.split("case StormPhase::Warning:", 1)[1].split(
    "case StormPhase::Active:", 1
)[0]
assert "kSndNukeImpact" not in warning_phase
already_current_storm = warning_phase.split("if (sky->currentWeather == w)", 1)[1].split(
    "                    else", 1
)[0]
assert "sky->overrideWeather = w;" in already_current_storm
assert already_current_storm.find("sky->overrideWeather = w;") < already_current_storm.find("_weatherForced = true;")
active_phase = storm_update.split("case StormPhase::Active:", 1)[1].split(
    "case StormPhase::Aftermath:", 1
)[0]
nuke_result = active_phase.split("_stormNukeFired = SpawnDistantNuke();", 1)[1].split(
    "if (_stormNukeFired ||", 1
)[0]
assert "if (_stormNukeFired)" in nuke_result
assert "Play2DSound(kSndNukeImpact, boom)" in nuke_result

roll_in = active_phase.split("if (_stormRollInTicks > 0)", 1)[1].split(
    "ApplyStormExposure();", 1
)[0]
wave_play = roll_in.find("Play2DSound(kSndBlastWave, wave)")
wave_guard = roll_in.rfind("if (_stormNukeFired)", 0, wave_play)
atmosphere = roll_in.find("ApplyStormIMAD(_activeStormType);")
assert wave_play != -1 and wave_guard != -1
assert atmosphere != -1 and atmosphere < wave_guard
assert "without nuke detonation" in roll_in

player_hit = mask.split("void RadiationEngine::OnPlayerHit()", 1)[1].split(
    "int RadiationEngine::RepairKitCount()", 1
)[0]
for token in (
    "RadLifecycleDispatch::ReadyPlayerUpdateGeneration()",
    "if (generation == 0) return;",
    "[generation]()",
    "RadLifecycleDispatch::PlayerUpdatesReady(generation)",
):
    assert token in player_hit, f"mask hit dispatch generation guard is missing: {token}"
assert player_hit.find("RadLifecycleDispatch::PlayerUpdatesReady(generation)") < player_hit.find(
    "RadiationEngine::GetSingleton().OnPlayerHit();"
)
assert player_hit.rfind("RadLifecycleDispatch::PlayerUpdatesReady(generation)") < player_hit.find(
    "_maskCondition = std::max"
)

assert radhud.count("if (!a || !a->IsValid(v)) return;") == 3
for view_assignment, ready_flag in (
    ("_view = api->CreateView(kViewPath", "g_domReadyFlag.load"),
    ("_toxicView = api->CreateView(kToxicViewPath", "g_toxicDomReadyFlag.load"),
    ("_filterView = api->CreateView(kFilterViewPath", "g_filterDomReadyFlag.load"),
):
    callback = radhud.split(view_assignment, 1)[1].split("});", 1)[0]
    validity = callback.find("if (!a || !a->IsValid(v)) return;")
    ready = callback.find(ready_flag)
    assert validity != -1 and ready != -1 and validity < ready

producer = hooks.split("void PlayerCharacter_Update_Hook", 1)[1].split(
    "bool InstallUpdateHook", 1
)[0]
assert "RadLifecycleDispatch::ReadyPlayerUpdateGeneration() == 0" in producer
assert producer.find("RadLifecycleDispatch::ReadyPlayerUpdateGeneration()") < producer.find("QueueInputPoll();")

input_queue = hooks.split("static void QueueInputPoll()", 1)[1].split(
    "static void QueueEngineUpdate()", 1
)[0]
engine_queue = hooks.split("static void QueueEngineUpdate()", 1)[1].split(
    "void PlayerCharacter_Update_Hook", 1
)[0]
for body, action in (
    (input_queue, "RadiationEngine::GetSingleton().PollInput();"),
    (engine_queue, "RadiationEngine::GetSingleton().Update();"),
):
    assert "const auto generation = RadLifecycleDispatch::ReadyPlayerUpdateGeneration();" in body
    assert "[generation]()" in body
    assert "RadLifecycleDispatch::PlayerUpdatesReady(generation)" in body
    assert body.find("RadLifecycleDispatch::PlayerUpdatesReady(generation)") < body.find(action)

dome_queue = dome_hooks.split("void QueueDomeFollow()", 1)[1]
for token in (
    "const auto generation = RadLifecycleDispatch::ReadyPlayerUpdateGeneration();",
    "[generation]()",
    "RadLifecycleDispatch::PlayerUpdatesReady(generation)",
):
    assert token in dome_queue, f"dome producer generation guard is missing: {token}"
assert dome_queue.find("RadLifecycleDispatch::PlayerUpdatesReady(generation)") < dome_queue.find(
    "RadiationEngine::GetSingleton().UpdateStormDomeFollow();"
)

for token in (
    "g_playerUpdateGeneration{ 0 }",
    "g_playerUpdateReadyGeneration{ 0 }",
    "std::uint64_t ReadyPlayerUpdateGeneration() noexcept",
    "bool PlayerUpdatesReady(std::uint64_t expectedGeneration) noexcept",
    "g_playerUpdateGeneration.load(std::memory_order_acquire) == expectedGeneration",
    "g_playerUpdateReadyGeneration.load(std::memory_order_acquire) == expectedGeneration",
    "std::chrono::minutes(10)",
    "RadCorrectnessPolicy::IsCurrentLoadGeneration",
    "std::this_thread::sleep_for(std::chrono::milliseconds(100));",
    "TryQueue(task)",
):
    assert token in lifecycle_dispatch, f"lifecycle gate/retry is missing: {token}"

post_load = main.split("case F4SE::MessagingInterface::kPostLoadGame:", 1)[1].split(
    "case F4SE::MessagingInterface::kPostPostLoad:", 1
)[0]
new_game = main.split("case F4SE::MessagingInterface::kNewGame:", 1)[1].split(
    "case F4SE::MessagingInterface::kPreLoadGame:", 1
)[0]
pre_load = main.split("case F4SE::MessagingInterface::kPreLoadGame:", 1)[1].split("default:", 1)[0]

for section, generation_token in (
    (post_load, "g_radLoadGeneration.fetch_add"),
    (new_game, "RadCorrectnessPolicy::BeginNewGameLoad"),
    (pre_load, "g_radLoadGeneration.fetch_add"),
):
    block = section.find("RadLifecycleDispatch::BlockPlayerUpdates();")
    generation_change = section.find(generation_token)
    publish = section.find("RadLifecycleDispatch::SetPlayerUpdateGeneration(loadGeneration);")
    assert block != -1 and generation_change != -1 and publish != -1
    assert block < generation_change < publish

for section in (post_load, new_game):
    assert "RadLifecycleDispatch::QueueWithRetry(" in section
    assert "RadLifecycleDispatch::MarkPlayerUpdateGenerationReady(loadGeneration);" in section
    ready = section.find("RadLifecycleDispatch::MarkPlayerUpdateGenerationReady(loadGeneration);")
    reset = section.find("engine.ResetAppliedPenalties();")
    assert reset != -1 and reset < ready

reset_penalties = radiation_header.split("void ResetAppliedPenalties()", 1)[1].split(
    "void OnPostLoad();", 1
)[0]
assert "_radstormCueHandle.IsValid()" in reset_penalties
assert "_radstormCueHandle.FadeOutAndRelease(500);" in reset_penalties
assert reset_penalties.find("_radstormWarned = false;") < reset_penalties.find("_radstormCueHandle.FadeOutAndRelease(500);")

post_load_engine = radiation_engine.split("void RadiationEngine::OnPostLoad()", 1)[1]
assert "_forceClearOnEntry = false;" in post_load_engine
assert "_spawnRadBlockTicks = 0;" in post_load_engine
assert "_forcedStormType   = -1;" in post_load_engine
assert "_maskWasOn = -1;" in post_load_engine
assert "engine.OnNewGame();" not in post_load
post_reset = new_game.find("engine.OnPostLoad();")
new_game_arm = new_game.find("engine.OnNewGame();")
new_game_penalties = new_game.find("engine.ResetAppliedPenalties();")
assert post_reset != -1 and new_game_arm != -1 and new_game_penalties != -1
assert post_reset < new_game_arm < new_game_penalties

load_local_scrub = debug.split("void RadiationEngine::DrainDebugCommands()", 1)[1].split(
    "if (_pendingDebugCmds.empty()) return;", 1
)[0]
for token in (
    "if (_prevRads < 0.0f)",
    "_filterSparesCache = 0;",
    "_filterCountTick = 0;",
    "_enclosedCheckTick = 0;",
    "_weatherReturnTarget = nullptr;",
    "_weatherReturnCaptured = false;",
    "_releaseWeatherOverrideWhenSettled = false;",
    "_weatherHandoffSettleTicks = 0;",
):
    assert token in load_local_scrub, f"first post-load transient scrub is missing: {token}"

core_update = radiation_engine.split("void RadiationEngine::Update()", 1)[1].split(
    "void RadiationEngine::PollInput()", 1
)[0]
drain = core_update.find("DrainDebugCommands();")
warning = core_update.find("CheckRadstormWarning();")
storm_tick = core_update.find("UpdateStorm();")
assert drain != -1 and warning != -1 and storm_tick != -1
assert drain < warning < storm_tick

hud_wait = main.split("void WaitThenCreateRadHud", 1)[1].split(
    "void StartRadHudWaitIfIdle", 1
)[0]
for token in (
    "auto probeQueued = std::make_shared<std::atomic<bool>>(false);",
    "probeQueued->compare_exchange_strong",
    "[free, probeQueued, generation]",
    "probeQueued->store(false, std::memory_order_release);",
):
    assert token in hud_wait, f"HUD readiness probe is not coalesced: {token}"

print("storm ownership, handoff, same-current weather ownership, load-audio teardown, first-tick transient isolation, shelter-cache isolation, new-game load isolation, forced-lead isolation, new-game-clear, dome-streaming, lifecycle, queued-generation, mask-hit generation, hud-dom-ready validity, nuke-audio, nuke-wave, flee-reset, roll-in-reset, dome-follow, and HUD guards verified")
