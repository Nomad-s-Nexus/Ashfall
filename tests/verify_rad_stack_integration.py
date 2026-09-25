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
    follow = read("Hooks_DomeFollow.cpp")
    header = read("Hooks.h")
    producer = body(hooks, "void PlayerCharacter_Update_Hook(")
    input_poll = body(hooks, "static void QueueInputPoll()")
    check("dedicated dome-follow producer exists",
          "void QueueDomeFollow()" in follow and "void QueueDomeFollow();" in header)
    check("dome-follow producer is throttled near 30 Hz",
          "std::chrono::milliseconds(33)" in follow)
    check("dome-follow task is coalesced",
          "s_domeFollowTaskQueued.compare_exchange_strong" in follow)
    check("dome-follow task is generation guarded",
          "RadLifecycleDispatch::PlayerUpdatesReady(generation)" in follow)
    check("player update produces dome follow separately",
          "QueueDomeFollow();" in producer)
    check("input polling does not own dome updates",
          "UpdateStormDome" not in input_poll)


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
namespace Hooks { bool QueueMainThreadTask(std::function<void()> task) { g_queue.push_back(std::move(task)); return true; } }
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
namespace Hooks { bool QueueMainThreadTask(std::function<void()> task); }
'''


def check_generation_behavior():
    source = SRC / "RadLifecycleDispatch.cpp"
    if not source.exists():
        check("generation gate behavior", False, "RadLifecycleDispatch.cpp missing")
        return
    compiler = shutil.which("g++") or shutil.which("clang++")
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
        exe = tmp / "harness"
        build = subprocess.run([compiler, "-std=c++20", "-pthread", "-I", str(tmp),
                                str(tmp / "harness.cpp"), str(tmp / "RadLifecycleDispatch.cpp"),
                                "-o", str(exe)], capture_output=True, text=True)
        if build.returncode != 0:
            check("generation gate behavior", False, build.stderr.strip()[:400])
            return
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        check("stale-generation callbacks cannot execute; current-generation callbacks execute",
              run.returncode == 0, run.stdout.strip())


def check_storm_layout():
    modules = ("RadEngine_Storm.cpp", "RadEngine_Storm_Weather.cpp", "RadEngine_Storm_Dome.cpp",
               "RadEngine_Storm_Actors.cpp", "RadEngine_Storm_FX.cpp", "RadEngine_Storm_Update.cpp")
    for name in modules:
        check(f"storm module present: {name}", (SRC / name).exists())
    lines = read("RadEngine_Storm.cpp").count("\n")
    check("storm coordinator stays under 500 lines", lines < 500, f"{lines} lines")
    storm = "\n".join(read(name) for name in modules)
    dome = read("RadEngine_Storm_Dome.cpp")
    for symbol in ("void RadiationEngine::SpawnStormDome()", "void RadiationEngine::UpdateStormDomeLifecycle()"):
        check(f"dome ownership gated by storm dome policy: {symbol}",
              "StormUsesDome(kStormTypes[_activeStormType].themeId)" in body(dome, symbol).replace("\n}", ""))
    check("Papyrus EnableCloudDome is never mutated", "EnableCloudDome" not in storm)
    fx = body(read("RadEngine_Storm_FX.cpp"), "void RadiationEngine::TriggerStormFX(bool on)")
    firestorm = fx.split("if (st.themeId == 1)", 1)
    check("Firestorm Papyrus route stops dome stages",
          len(firestorm) == 2 and "_stormFXQuest->SetStage(20);" in firestorm[1].split("else", 1)[0])
    check("dome follow keeps native ownership in UpdateStormDomeFollow",
          "void RadiationEngine::UpdateStormDomeFollow()" in dome)
    policy = read("RadCorrectnessPolicy.h")
    check("Firestorm dome policy is theme 1 only", "return theme != 1;" in policy)


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
