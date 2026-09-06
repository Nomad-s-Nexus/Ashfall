from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"

actors = (SRC / "RadEngine_Storm_Actors.cpp").read_text(encoding="utf-8")
weather = (SRC / "RadEngine_Storm_Weather.cpp").read_text(encoding="utf-8")
weather_core = (SRC / "RadEngine_Weather.cpp").read_text(encoding="utf-8")
dome = (SRC / "RadEngine_Storm_Dome.cpp").read_text(encoding="utf-8")
storm = (SRC / "RadEngine_Storm.cpp").read_text(encoding="utf-8")
storm_update = (SRC / "RadEngine_Storm_Update.cpp").read_text(encoding="utf-8")
hooks = (SRC / "Hooks.cpp").read_text(encoding="utf-8")
dome_hooks = (SRC / "Hooks_DomeFollow.cpp").read_text(encoding="utf-8")
lifecycle_dispatch = (SRC / "RadLifecycleDispatch.cpp").read_text(encoding="utf-8")
main = (SRC / "main.cpp").read_text(encoding="utf-8")

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
    "ref->GetFormType() == RE::ENUM_FORM_ID::kACHR",
    "static_cast<RE::Actor*>(ref)",
    "actor && !actor->IsDead(false)",
    "Existing hunter pack still active",
    "Previous hunter pack defeated",
    "ClearStormGhouls();",
):
    assert token in ghoul_spawn, f"storm ghoul pack bound is missing: {token}"

assert "ref->As<RE::Actor>()" not in ghoul_spawn
assert "_stormGhoulRefs.erase" not in ghoul_spawn

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

print("storm ownership, handoff, same-current weather ownership, new-game-clear, dome-streaming, lifecycle, queued-generation, nuke-audio, nuke-wave, roll-in-reset, dome-follow, and HUD guards verified")
