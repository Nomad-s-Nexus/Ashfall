import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
FAILURES = []


def check(name, condition, detail=""):
    status = "PASS" if condition else "FAIL"
    print(f"[{status}] {name}" + (f": {detail}" if detail and not condition else ""))
    if not condition:
        FAILURES.append(name)


def read(name):
    path = SRC / name
    return path.read_text(encoding="utf-8") if path.exists() else ""


def body(source, signature):
    if signature not in source:
        return ""
    start = source.index(signature)
    terminator = "\n}\n" if "RadiationEngine::" in signature else "\n    }\n"
    end = source.find(terminator, start)
    return source[start:end if end > 0 else len(source)]


def check_fallback_queue():
    hooks = read("Hooks.cpp")
    queue = body(hooks, "bool QueueMainThreadTask(")
    check("fallback queue declares 128-task bound",
          "static constexpr std::size_t kMaxQueuedMainTasks = 128;" in hooks)
    check("fallback queue rejects work at the bound",
          "if (s_mainTasks.size() >= kMaxQueuedMainTasks)" in queue and "return false;" in queue)


def check_dome_follow_producer():
    hooks = read("Hooks.cpp")
    follow = read("Hooks_StormVisuals.cpp")
    header = read("Hooks.h")
    producer = body(hooks, "void PlayerCharacter_Update_Hook(")
    input_poll = body(hooks, "static void QueueInputPoll()")
    check("dedicated storm visuals producer exists",
          "void QueueStormVisuals()" in follow and "void QueueStormVisuals();" in header)
    check("storm visuals producer is throttled near 30 Hz",
          "std::chrono::milliseconds(33)" in follow)
    check("storm visuals task is coalesced",
          "s_stormVisualsTaskQueued.compare_exchange_strong" in follow)
    check("storm visuals task is generation guarded",
          "RadLifecycleDispatch::PlayerUpdatesReady(generation)" in follow)
    check("player update produces storm visuals separately",
          "QueueStormVisuals();" in producer)
    check("input polling does not own storm visuals",
          "UpdateStormIMADFade" not in input_poll)


def check_generation_guards():
    hooks = read("Hooks.cpp")
    input_poll = body(hooks, "static void QueueInputPoll()")
    engine = body(hooks, "static void QueueEngineUpdate()")
    producer = body(hooks, "void PlayerCharacter_Update_Hook(")
    check("lifecycle dispatch module exists",
          (SRC / "RadLifecycleDispatch.cpp").exists() and (SRC / "RadLifecycleDispatch.h").exists())
    check("input task checks captured generation",
          "if (RadLifecycleDispatch::PlayerUpdatesReady(generation))" in input_poll)
    check("engine task checks captured generation",
          "if (RadLifecycleDispatch::PlayerUpdatesReady(generation))" in engine)
    guarded = engine.split("if (RadLifecycleDispatch::PlayerUpdatesReady(generation))", 1)
    check("climate update runs only inside the generation-guarded main-thread task",
          len(guarded) == 2 and "ClimateRuntime::GetSingleton().Update();" in guarded[1].split("catch", 1)[0])
    check("climate update is never called from the worker-thread hook",
          "ClimateRuntime" not in producer)
    check("player update producer requires a ready generation",
          "RadLifecycleDispatch::ReadyPlayerUpdateGeneration() == 0" in producer)
    main = read("main.cpp")
    for marker in ("RadLifecycleDispatch::BlockPlayerUpdates();",
                   "RadLifecycleDispatch::MarkPlayerUpdateGenerationReady("):
        check(f"lifecycle publishes generation: {marker}", marker in main)


HARNESS = r'''
#include "RadLifecycleDispatch.h"
#include <cstdio>
#include <deque>
static std::deque<std::function<void()>> g_queue;
static int g_resetCalls = 0;
namespace Hooks { bool QueueMainThreadTask(std::function<void()> task) { g_queue.push_back(std::move(task)); return true; } void ResetQueuedTaskFlags() noexcept { ++g_resetCalls; } }
static int g_ran = 0;
static void Produce()
{
    const auto generation = RadLifecycleDispatch::ReadyPlayerUpdateGeneration();
    if (generation == 0) return;
    Hooks::QueueMainThreadTask([generation]() { if (RadLifecycleDispatch::PlayerUpdatesReady(generation)) ++g_ran; });
}
static void Drain() { while (!g_queue.empty()) { auto t = std::move(g_queue.front()); g_queue.pop_front(); t(); } }
int main()
{
    int failures = 0;
    RadLifecycleDispatch::SetPlayerUpdateGeneration(1);
    Produce();
    if (!g_queue.empty()) { std::puts("FAIL unready generation produced work"); ++failures; }
    RadLifecycleDispatch::MarkPlayerUpdateGenerationReady(1);
    Produce();
    RadLifecycleDispatch::BlockPlayerUpdates();
    if (g_resetCalls != 1) { std::puts("FAIL BlockPlayerUpdates did not reset the queued task flags"); ++failures; }
    RadLifecycleDispatch::SetPlayerUpdateGeneration(2);
    RadLifecycleDispatch::MarkPlayerUpdateGenerationReady(2);
    Drain();
    if (g_ran != 0) { std::puts("FAIL stale-generation callback executed"); ++failures; }
    Produce();
    Drain();
    if (g_ran != 1) { std::puts("FAIL current-generation callback did not execute"); ++failures; }
    RadLifecycleDispatch::BlockPlayerUpdates();
    if (RadLifecycleDispatch::PlayerUpdatesReady(2)) { std::puts("FAIL blocked generation reported ready"); ++failures; }
    std::atomic<std::uint64_t> load{ 7 };
    int retried = 0;
    RadLifecycleDispatch::QueueWithRetry(&load, 7, "test", [&retried]() { ++retried; });
    Drain();
    if (retried != 1) { std::puts("FAIL accepted lifecycle task did not execute"); ++failures; }
    return failures;
}
'''

STUB_PCH = r'''
#pragma once
#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <string>
namespace spdlog {
struct logger { template <class... A> void warn(A&&...) {} template <class... A> void error(A&&...) {} template <class... A> void critical(A&&...) {} };
inline std::shared_ptr<logger> get(const char*) { return nullptr; }
}
'''

STUB_HOOKS = r'''
#pragma once
#include <functional>
namespace Hooks { bool QueueMainThreadTask(std::function<void()> task); void ResetQueuedTaskFlags() noexcept; }
'''


def check_generation_behavior():
    source = SRC / "RadLifecycleDispatch.cpp"
    if not source.exists():
        check("generation gate behavior", False, "RadLifecycleDispatch.cpp missing")
        return
    compiler = shutil.which("cl") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        check("generation gate behavior", False, "no host C++ compiler")
        return
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        for name in ("RadLifecycleDispatch.cpp", "RadLifecycleDispatch.h", "RadCorrectnessPolicy.h"):
            shutil.copy(SRC / name, tmp / name)
        (tmp / "pch.h").write_text(STUB_PCH, encoding="utf-8")
        (tmp / "Hooks.h").write_text(STUB_HOOKS, encoding="utf-8")
        (tmp / "harness.cpp").write_text(HARNESS, encoding="utf-8")
        exe = tmp / ("harness.exe" if os.name == "nt" else "harness")
        flags = (["/nologo", "/std:c++20", "/EHsc", f"/Fe{exe}"]
                 if Path(compiler).stem.lower() == "cl"
                 else ["-std=c++20", "-pthread", "-o", str(exe)])
        build = subprocess.run([compiler, *flags, "harness.cpp", "RadLifecycleDispatch.cpp"],
                               cwd=tmp, capture_output=True, text=True)
        if build.returncode != 0:
            check("generation gate behavior", False, (build.stdout + build.stderr).strip()[:400])
            return
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        check("stale-generation callbacks cannot execute; current-generation callbacks execute",
              run.returncode == 0, run.stdout.strip())


def check_storm_layout():
    modules = ("RadEngine_Storm.cpp", "RadEngine_Storm_Weather.cpp", "RadEngine_Storm_Effects.cpp",
               "RadEngine_Storm_Actors.cpp", "RadEngine_Storm_FX.cpp", "RadEngine_Storm_Update.cpp")
    for name in modules:
        check(f"storm module present: {name}", (SRC / name).exists())
    lines = read("RadEngine_Storm.cpp").count("\n")
    check("storm coordinator stays under 500 lines", lines < 500, f"{lines} lines")
    storm = "\n".join(read(name) for name in modules)
    dome_tokens = ("SpawnStormDome", "ClearStormDome", "UpdateStormDomeLifecycle", "UpdateStormDomeFollow",
                   "UpdateDomeAnimation", "FARO_StormDome", "kStormDome", "_stormDome")
    for token in dome_tokens:
        check(f"no native storm dome spawn path remains: {token}", token not in storm)
    check("Papyrus EnableCloudDome is never mutated", "EnableCloudDome" not in storm)
    fx = body(read("RadEngine_Storm_FX.cpp"), "void RadiationEngine::TriggerStormFX(bool on)")
    firestorm = fx.split("if (st.themeId == 1)", 1)
    check("Firestorm Papyrus route stops stages",
          len(firestorm) == 2 and "_stormFXQuest->SetStage(20);" in firestorm[1].split("else", 1)[0])
    policy = read("RadCorrectnessPolicy.h")
    check("ShouldReturnToClear policy survives the dome removal",
          "ShouldReturnToClear(bool managedStorm, bool vanillaRadstorm)" in policy)
    internal = read("RadEngine_Internal.h")
    weather_storm = read("RadEngine_Storm_Weather.cpp")
    esl = read("RadEngine_ESL.cpp")
    weather_core = read("RadEngine_Weather.cpp")
    rad_engine = read("RadiationEngine.cpp")
    audio = read("RadEngine_Audio.cpp")
    check("vanilla radstorm sky constants exist",
          "kStormRadSkyWeatherPrimary" in internal and "kStormRadSkyWeatherFallback" in internal)
    check("StormSkyWeather accessor exists",
          "RadiationEngine::StormSkyWeather(int typeIndex) const" in weather_storm)
    check("vanilla radstorm sky is resolved outside the mutation loop",
          "_stormSkyVanilla = primary ? primary : fallback;" in esl
          and esl.index("_stormSkyVanilla = primary") < esl.index("for (int i = 0; i < kStormTypeCount"))
    check("vanilla radstorm sky pointer is never mutated", "_stormSkyVanilla->" not in esl)
    check("CheckRadstormWarning returns during a managed storm",
          "if (_stormPhase != StormPhase::Idle) return;" in weather_core)
    check("ghoul gate spawns once per managed storm",
          "_stormPhase == StormPhase::Active))" in rad_engine)
    check("storm bed follows the active storm type under the vanilla sky",
          "RadCorrectnessPolicy::UsesVanillaRadstormSky(_activeStormType)" in audio)
    storm_update_src = read("RadEngine_Storm_Update.cpp")
    debug_src = read("RadEngine_Debug.cpp")
    check("Warning roll-in starts the vanilla radstorm sky for types 0 and 2",
          "sky roll-in armed at WARNING" in weather_storm + read("RadEngine_Storm.cpp")
          and "UsesVanillaRadstormSky(_activeStormType)" in read("RadEngine_Storm.cpp"))
    check("RollDiag trace logs phase, pct and weather IDs", "[RollDiag] {} phase={} pct={:.2f}" in storm_update_src)
    check("rad-weather gate holds during the vanilla Warning roll-in",
          "_stormPhase == StormPhase::Warning && _activeStormType >= 0" in rad_engine)
    check("DebugForceStorm releases only when a forced weather is in effect",
          "if (_weatherForced) ReleaseStormWeather();" in debug_src and "else ResetStormWeatherLatches();" in debug_src)


def check_ghoul_ownership():
    actors = read("RadEngine_Storm_Actors.cpp")
    check("ghoul spawn and cleanup live in actor module",
          "void RadiationEngine::SpawnStormGhouls()" in actors and "void RadiationEngine::ClearStormGhouls()" in actors)


def main():
    check_fallback_queue()
    check_dome_follow_producer()
    check_generation_guards()
    check_generation_behavior()
    check_storm_layout()
    check_ghoul_ownership()
    if FAILURES:
        print(f"{len(FAILURES)} Rad stack integration contract(s) failed")
        sys.exit(1)
    print("Rad stack integration contracts verified")


if __name__ == "__main__":
    main()
