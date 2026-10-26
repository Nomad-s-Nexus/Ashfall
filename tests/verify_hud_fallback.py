import sys
from pathlib import Path


def effective_max(permanent, temporary):
    return max(0.0, float(permanent) + float(temporary))


def vital_percent(current, maximum):
    current = max(0.0, float(current))
    maximum = max(0.0, float(maximum))
    return min(100.0, current / maximum * 100.0) if maximum > 0.0 else (100.0 if current > 0.0 else 0.0)


def health_segments(current, maximum, rads, rads_max=1000.0):
    current = max(0.0, float(current))
    maximum = max(0.0, float(maximum))
    rads_max = max(1.0, float(rads_max))
    blocked = max(0.0, min(1.0, float(rads) / rads_max))
    available = 1.0 - blocked
    healthy = min(max(0.0, current / maximum), available) if maximum > 0.0 else (available if current > 0.0 else 0.0)
    missing = max(0.0, available - healthy)
    return tuple(round(x * 100.0, 5) for x in (healthy, missing, blocked))


def toxic_state(fallback, tissue, permanent):
    tissue = min(1000.0, max(0.0, float(tissue)))
    permanent = min(tissue, max(0.0, float(permanent)))
    return {"tissue": tissue, "permanent": permanent, "visible": bool(fallback) or tissue > 0.0}


def widget_fallback(api_available, view_valid, registration_called):
    return not (api_available and view_valid and registration_called)


def position_update(wanted, viewport, last_viewport, force=False):
    width, height = viewport
    if width <= 1 or height <= 1 or (viewport == last_viewport and not force):
        return None, last_viewport
    return (wanted[0] * width, wanted[1] * height, wanted[2], wanted[3]), viewport


def engine_active(core_initialized, update_producer_installed):
    return bool(core_initialized and update_producer_installed)


def hud_wait_should_release(create_dispatch_accepted):
    return bool(create_dispatch_accepted)


def check_behavior():
    health_max = effective_max(300, 50)
    ap_max = effective_max(100, 20)
    assert round(vital_percent(330, health_max), 5) == 94.28571
    assert round(vital_percent(110, ap_max), 5) == 91.66667
    assert vital_percent(0, 0) == 0.0
    assert vital_percent(10, -5) == 100.0
    assert vital_percent(360, health_max) == 100.0

    # Vanilla radiation blocks max-health capacity from the right side of the fallback bar.
    assert health_segments(70, 100, 300) == (70.0, 0.0, 30.0)
    assert health_segments(50, 100, 300) == (50.0, 20.0, 30.0)
    assert health_segments(100, 100, 0) == (100.0, 0.0, 0.0)

    assert widget_fallback(False, False, False) is True
    assert widget_fallback(True, False, False) is True
    assert widget_fallback(True, True, True) is False

    assert toxic_state(False, 0, 0)["visible"] is False
    assert toxic_state(True, 0, 0)["visible"] is True

    tissue = toxic_state(False, 400, 120)
    assert tissue == {"tissue": 400.0, "permanent": 120.0, "visible": True}
    assert toxic_state(False, -1, 50) == {"tissue": 0.0, "permanent": 0.0, "visible": False}

    wanted = (0.1, 0.2, 1.0, 0.0)
    first, last = position_update(wanted, (1920, 1080), (0, 0))
    assert first == (192.0, 216.0, 1.0, 0.0)
    assert position_update(wanted, (1920, 1080), last)[0] is None
    resized, last = position_update(wanted, (2560, 1440), last)
    assert resized == (256.0, 288.0, 1.0, 0.0)
    moved, _ = position_update((0.2, 0.3, 1.5, 90.0), (2560, 1440), last, force=True)
    assert moved == (512.0, 432.0, 1.5, 90.0)

    # A load/new-game mutation must never run on a partially activated engine.
    assert engine_active(False, False) is False
    assert engine_active(True, False) is False
    assert engine_active(False, True) is False
    assert engine_active(True, True) is True

    # The load waiter must survive a transient CreateView dispatch rejection.
    assert hud_wait_should_release(False) is False
    assert hud_wait_should_release(True) is True


def check_source(html_path, source_path):
    html = Path(html_path).read_text(encoding="utf-8")
    for marker in ('HEALTH', 'AP', 'TISSUE', 'id="rads"', 'id="health"', 'id="stamina"', 'hud_status.css', "fallback=!!d.fallback", "var show=fallback||t>0"):
        if marker not in html:
            raise SystemExit(f"missing HUD fallback contract: {marker}")

    position_markers = (
        "var _want=null,_lastW=0,_lastH=0;",
        "if(W===_lastW&&H===_lastH)return;",
        "_lastW=W;_lastH=H;",
        "r.style.setProperty('--r',(_want.r||0)+'deg');",
        "var explicitSize=_want.w!==null&&_want.h!==null;",
        "typeof p.w==='number'?p.w:null",
        "typeof p.h==='number'?p.h:null",
        'new ResizeObserver(function(){flushPos();})',
    )
    for view in (Path(html_path), Path(html_path).with_name('hud_filter.html')):
        view_html = view.read_text(encoding="utf-8")
        for marker in position_markers:
            if marker not in view_html:
                raise SystemExit(f"missing PrismaHUD 2.1 position contract in {view.name}: {marker}")

    source = Path(source_path).read_text(encoding="utf-8")
    for marker in (
        'player.GetModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary, value)',
        'healthMax = EffectiveActorValueMax(*player, *values->health);',
        'apMax = EffectiveActorValueMax(*player, *values->actionPoints);',
        'rads = std::clamp(player->GetActorValue(*values->rads), 0.0f, 1000.0f);',
        r'\"rads\":%.1f,\"radsMax\":1000.0',
        'std::atomic<PRISMA_UI_API::IVPrismaUI2*> g_apiForCb',
        'std::atomic<PRISMA_HUD_API::IVPrismaHUD1*> g_hudApiForCb',
        'api->IsValid(_toxicView)',
        '_filterView == 0 || !api->IsValid(_filterView)',
        'CreateView();',
        'StartPrismaViewRetry();',
        'const bool fallback = !_toxicWidgetRegistered.load();',
        "constexpr int kHudViewOrder = 1'000'000;",
        'api->SetOrder(view, kHudViewOrder);',
        'setHudViewOrder(_toxicView);',
        'setHudViewOrder(_filterView);',
    ):
        if marker not in source:
            raise SystemExit(f"missing HUD fallback implementation: {marker}")
    if 'setHudViewOrder(_view);' in source:
        raise SystemExit('fullscreen radiation overlay must not use the PrismaHUD compositor order')

    # PrismaHUD must be available to a fast DOM-ready callback before asynchronous view creation starts.
    hud_api_idx = source.index('_hudApi = PRISMA_HUD_API::RequestPluginAPI();')
    retry_idx = source.index('StartPrismaViewRetry();')
    if hud_api_idx > retry_idx:
        raise SystemExit('PrismaHUD API must be resolved before asynchronous PrismaUI view creation')
    for marker in (
        'g_apiForCb.load(std::memory_order_acquire)',
        'g_hudApiForCb.load(std::memory_order_acquire)',
        'g_domReadyFlag.load(std::memory_order_acquire)',
        'g_toxicDomReadyFlag.load(std::memory_order_acquire)',
        'g_filterDomReadyFlag.load(std::memory_order_acquire)',
    ):
        if marker not in source:
            raise SystemExit(f'cross-thread HUD callback bridge must use atomic publication: {marker}')

    storm_source = '\n'.join(
        Path(source_path).with_name(name).read_text(encoding='utf-8')
        for name in (
            'RadEngine_Storm.cpp',
            'RadEngine_Storm_Weather.cpp',
            'RadEngine_Storm_Dome.cpp',
            'RadEngine_Storm_Actors.cpp',
            'RadEngine_Storm_FX.cpp',
            'RadEngine_Storm_Update.cpp',
        )
    )
    for marker in (
        'UpdateWeatherHandoff();',
        'CaptureWeatherReturnTarget();',
        'BeginSmoothWeather(target, /*lockOverride=*/true);',
        '_weatherReturnCaptured ? _weatherReturnTarget',
        'region control releases only after the blend is stable',
        'RadCorrectnessPolicy::DistantNukePlacement',
        'facingPlayerYaw',
        'sky->ForceWeather(_clearWeather, false);',
    ):
        if marker not in storm_source:
            raise SystemExit(f'missing smooth storm-weather return contract: {marker}')
    storm_weather_source = Path(source_path).with_name('RadEngine_Storm_Weather.cpp').read_text(encoding='utf-8')
    force_clear_start = storm_weather_source.index('void RadiationEngine::ForceClearAndSilence()')
    force_clear_body = storm_weather_source[force_clear_start:storm_weather_source.index('\n}\n', force_clear_start)]
    if 'ReleaseStormWeather();' in force_clear_body:
        raise SystemExit('ForceClearAndSilence must abort the handoff directly')

    weather_source = Path(source_path).with_name('RadEngine_Weather.cpp').read_text(encoding='utf-8')
    for marker in (
        'void RadiationEngine::UpdateWeatherHandoff()',
        'const bool resetSessionState = _forceClearOnEntry',
        '_weatherReturnTarget = nullptr;',
        '_weatherReturnCaptured = false;',
        '_releaseWeatherOverrideWhenSettled = false;',
        '_weatherHandoffSettleTicks = 0;',
        '_weatherReturnTarget',
        '_weatherHandoffSettleTicks < 8',
        'std::isfinite(sky->currentWeatherPct)',
        '_weatherHandoffWaitTicks > 240',
        'vanilla region weather control restored',
        'RadCorrectnessPolicy::ShouldReturnToClear',
    ):
        if marker not in weather_source:
            raise SystemExit(f'missing weather return handoff contract: {marker}')

    debug_source = Path(source_path).with_name('RadEngine_Debug.cpp').read_text(encoding='utf-8')
    for marker in (
        'CaptureWeatherReturnTarget();',
        '_releaseWeatherOverrideWhenSettled = false;',
        'BeginSmoothWeather(w);',
        'ForceWeather(static_cast<RE::TESWeather*>(f), true);',
    ):
        if marker not in debug_source:
            raise SystemExit(f'missing debug weather return contract: {marker}')

    lifecycle = Path(source_path).with_name('RadHudLifecycle.cpp').read_text(encoding='utf-8')
    for marker in (
        'RadHud::GetSingleton().ViewsReady()',
        'std::this_thread::sleep_for(std::chrono::milliseconds(500));',
    ):
        if marker not in lifecycle:
            raise SystemExit(f'missing PrismaUI startup retry contract: {marker}')

    storm_modules = '\n'.join(
        Path(source_path).with_name(name).read_text(encoding='utf-8')
        for name in ('RadEngine_Storm.cpp', 'RadEngine_Storm_Dome.cpp', 'RadEngine_Storm_FX.cpp',
                     'RadEngine_Storm_Update.cpp', 'RadEngine_Storm_Weather.cpp')
    )
    begin = storm_modules.split('void RadiationEngine::BeginStorm(int typeIndex)', 1)[1].split('\n}', 1)[0]
    assert 'dome-only lead' not in begin
    assert 'EnableCloudDome' not in storm_modules
    stage_diag = storm_modules.split('void RadiationEngine::DumpQuestStageTable(RE::TESQuest* quest)', 1)[1].split('void RadiationEngine::TriggerStormFX(bool on)', 1)[0]
    assert 'InitSearchTables' not in stage_diag
    active = storm_modules.split('case StormPhase::Active:', 1)[1].split('case StormPhase::', 1)[0]
    for marker in (
        '++_nukeAttempts;',
        '_stormNukeFired = SpawnDistantNuke();',
        '_firestormAwaitingNuke = false;',
    ):
        assert marker in active, f'missing bounded Firestorm retry contract: {marker}'
    assert '_stormNukeFired = true' not in active
    for symbol in ('SpawnStormDome', 'UpdateStormDomeLifecycle'):
        body = storm_modules.split('void RadiationEngine::' + symbol + '()', 1)[1].split('\n}', 1)[0]
        assert 'StormUsesDome(kStormTypes[_activeStormType].themeId)' in body
    papyrus_fx = storm_modules.split('void RadiationEngine::TriggerStormFX(bool on)', 1)[1].split('\n}', 1)[0]
    firestorm_fx = papyrus_fx.split('if (st.themeId == 1)', 1)[1].split('else if', 1)[0]
    assert '_stormFXQuest->SetStage(20);' in firestorm_fx
    assert 'return;' in firestorm_fx
    engine = Path(source_path).with_name('RadiationEngine.cpp').read_text(encoding='utf-8')
    assert 'filterSys && maskExposed && inRadWeather && _filterCharge > 0.0f' in engine
    assert 'RadHud::GetSingleton().PushAir(json{' in engine
    hooks = '\n'.join(
        Path(source_path).with_name(name).read_text(encoding='utf-8')
        for name in ('Hooks.cpp', 'Hooks_Input.cpp', 'Hooks_DomeFollow.cpp')
    )
    for marker in (
        'class RadInputHandler final : public RE::BSInputEventUser',
        'RE::MenuControls::GetSingleton()',
        'controls->RegisterHandler(RadInputHandler::GetSingleton())',
        'RE::BS_BUTTON_CODE::kF8',
        'GetAsyncKeyState(VK_SHIFT)',
        'DispatchToGameThread(RunPrismaThreadTask',
        'std::make_unique<std::function<void()>>(task)',
        'GetModuleHandleW(L"PrismaUI_F4.dll")',
        'PrismaUI module detected; V11 dispatcher not ready yet, startup work will be deferred.',
        'if (IsPrismaModuleLoaded())',
        'PrismaUI absent; installed Win32 fallback',
        'if (!s_mainThreadMsgHook && !GetPrismaThreadAPI())',
        's_domeFollowTaskQueued',
        's_domeFollowActive',
        's_stormImadFadeActive',
        'SetDomeFollowActive',
        'SetStormIMADFadeActive',
        'void QueueDomeFollow()',
        'RadiationEngine::GetSingleton().UpdateStormDomeFollow();',
        'RadiationEngine::GetSingleton().UpdateStormIMADFade();',
        'QueueDomeFollow();',
        'kDomeFollowInterval',
        'std::chrono::milliseconds(33)',
        's_domeFollowActive.load(std::memory_order_acquire)',
        's_stormImadFadeActive.load(std::memory_order_acquire)',
        'if (throttleInitialized && now - lastFollow < kDomeFollowInterval) return;',
        'kMaxQueuedMainTasks',
        's_mainTasks.size() >= kMaxQueuedMainTasks',
    ):
        if marker not in hooks:
            raise SystemExit(f'missing non-blocking game-thread dispatch contract: {marker}')
    dome_queue_idx = hooks.index('QueueDomeFollow();')
    dome_throttle_idx = hooks.index('if (elapsed < kUpdateInterval)', dome_queue_idx)
    if dome_queue_idx > dome_throttle_idx:
        raise SystemExit('storm dome follow must be queued before the 4 Hz engine throttle')
    dome_producer_idx = hooks.index('void QueueDomeFollow()')
    dome_active_gate_idx = hooks.index('s_domeFollowActive.load(std::memory_order_acquire)', dome_producer_idx)
    imad_active_gate_idx = hooks.index('s_stormImadFadeActive.load(std::memory_order_acquire)', dome_producer_idx)
    dome_gate_idx = hooks.index('if (throttleInitialized && now - lastFollow < kDomeFollowInterval) return;', dome_producer_idx)
    dome_dispatch_idx = hooks.index('queued = QueueMainThreadTask', dome_producer_idx)
    if max(dome_active_gate_idx, imad_active_gate_idx) > dome_gate_idx:
        raise SystemExit('storm visual active flags must be checked before cadence throttling')
    if dome_gate_idx > dome_dispatch_idx:
        raise SystemExit('storm dome follow cadence must be gated before main-thread dispatch')

    storm_source = '\n'.join(
        Path(source_path).with_name(name).read_text(encoding='utf-8')
        for name in (
            'RadEngine_Storm.cpp',
            'RadEngine_Storm_Weather.cpp',
            'RadEngine_Storm_Dome.cpp',
            'RadEngine_Storm_Actors.cpp',
            'RadEngine_Storm_FX.cpp',
            'RadEngine_Storm_Update.cpp',
        )
    )
    for marker in (
        'void RadiationEngine::UpdateStormDomeLifecycle()',
        'void RadiationEngine::UpdateStormDomeFollow()',
        'kStormDomeFollowDistanceThreshold',
        'needsMove',
        'ref->Get3D()',
        'playerWorld != _stormDomeWorld',
        'UpdateStormDomeLifecycle();',
        'ref->Update3DPosition(true);',
        'IsValidDomeOwner',
        'IsValidDomeReference',
        'cell->worldSpace == world',
    ):
        if marker not in storm_source:
            raise SystemExit(f'missing smooth dome follow contract: {marker}')
    for forbidden in ('ShouldFollowDomeOnTick', '_stormDomeFollowTick', 'Called once per player update'):
        if forbidden in storm_source:
            raise SystemExit(f'obsolete tick-throttled dome follow state remains: {forbidden}')

    engine_header = Path(source_path).with_name('RadiationEngine.h').read_text(encoding='utf-8')
    if 'void UpdateStormDomeFollow();' not in engine_header:
        raise SystemExit('RadiationEngine must expose per-frame dome follow')
    if 'void UpdateStormIMADFade();' not in engine_header:
        raise SystemExit('RadiationEngine must expose bounded IMAD fade updates')
    if '_stormDomeFollowTick' in engine_header:
        raise SystemExit('obsolete dome follow tick member must not return')

    for forbidden in (
        'WaitForPrismaDispatcher',
        'PrismaDispatcherProbe',
        'std::this_thread::sleep_for(std::chrono::milliseconds(50))',
    ):
        if forbidden in hooks:
            raise SystemExit(f'blocking Prisma startup dispatch must not return: {forbidden}')

    lifecycle_source = Path(source_path).with_name('RadLifecycle.cpp').read_text(encoding='utf-8')
    for marker in (
        'bool RadLifecycle::EnsureEngineActiveOnGameThread()',
        'RadiationEngine::GetSingleton().Initialize()',
        'Hooks::InstallUpdateHook()',
        'RadCorrectnessPolicy::IsEngineActive(',
        'Activation will be retried on the next safe lifecycle event',
        'hud.RequestAPI();',
        '!hud.Available() || !hud.HudLayoutAvailable()',
    ):
        if marker not in lifecycle_source:
            raise SystemExit(f'missing recoverable Rad lifecycle contract: {marker}')

    hud_header = Path(source_path).with_name('RadHud.h').read_text(encoding='utf-8')
    if 'bool HudLayoutAvailable() const' not in hud_header:
        raise SystemExit('Rad HUD must expose optional PrismaHUD provider readiness for lifecycle recovery')

    main_source = Path(source_path).with_name('main.cpp').read_text(encoding='utf-8')
    if '#include "RadLifecycle.h"' not in main_source:
        raise SystemExit('main.cpp must use the recoverable Rad lifecycle gate')

    post_start = main_source.index('case F4SE::MessagingInterface::kPostLoadGame:')
    post_end = main_source.index('case F4SE::MessagingInterface::kPostPostLoad:')
    post_block = main_source[post_start:post_end]
    ensure_idx = post_block.index('RadLifecycle::EnsureEngineActiveOnGameThread()')
    mutate_idx = post_block.index('engine.OnPostLoad();')
    active_idx = post_block.index('Game save loaded. Radiation Engine ACTIVE.')
    if not (ensure_idx < mutate_idx < active_idx):
        raise SystemExit('PostLoad must activate first, mutate second, and report ACTIVE last')
    if 'post-load mutations skipped' not in post_block:
        raise SystemExit('PostLoad must fail closed when activation is incomplete')

    new_start = main_source.index('case F4SE::MessagingInterface::kNewGame:')
    new_end = main_source.index('case F4SE::MessagingInterface::kPreLoadGame:')
    new_block = main_source[new_start:new_end]
    if new_block.index('RadLifecycle::EnsureEngineActiveOnGameThread()') > new_block.index('engine.OnPostLoad();'):
        raise SystemExit('NewGame must activate before mutating engine state')
    if 'new-game mutations skipped' not in new_block:
        raise SystemExit('NewGame must fail closed when activation is incomplete')

    game_data_start = main_source.index('case F4SE::MessagingInterface::kGameDataReady:')
    game_data_end = main_source.index('case F4SE::MessagingInterface::kPostLoadGame:')
    game_data_block = main_source[game_data_start:game_data_end]
    if 'GameDataReady activation deferred' not in game_data_block:
        raise SystemExit('GameDataReady must treat transient Prisma startup as deferred, not terminal')
    if 'RadiationEngine::GetSingleton().Initialize()' in game_data_block:
        raise SystemExit('GameDataReady must use the idempotent lifecycle activation helper')

    hud_wait_start = main_source.index('const bool createQueued = Hooks::QueueMainThreadTask')
    hud_wait_end = main_source.index('// The HUD waiter uses the same load generation', hud_wait_start)
    hud_wait_block = main_source[hud_wait_start:hud_wait_end]
    if 'if (!createQueued)' not in hud_wait_block or 'HUD retry worker will retry.' not in hud_wait_block:
        raise SystemExit('HUD waiter must detect rejected CreateView dispatches')
    reject_idx = hud_wait_block.index('if (!createQueued)')
    continue_idx = hud_wait_block.index('continue;', reject_idx)
    release_idx = hud_wait_block.index('g_radWaiting.store(false', reject_idx)
    if continue_idx > release_idx:
        raise SystemExit('HUD waiter must retry a rejected CreateView dispatch before releasing its latch')

    filter_html = Path(html_path).with_name('hud_filter.html').read_text(encoding='utf-8')
    for view_name, view_html in (("hud_toxic.html", html), ("hud_filter.html", filter_html)):
        if 'setInterval(function(){window.requestUpdate(\'\')},250);' in view_html:
            raise SystemExit(f'Rad HUD must not drive the radiation engine from {view_name}')
    if 'OnHudRequestUpdate' in source or 'BindGameThreadUIEvent' in source:
        raise SystemExit('Rad HUD must use the normal engine tick, not a second UI refresh path')
    headers = {
        'PrismaUI_F4_API.h': (
            'V9 = 136, V10 = 137, V11 = 138, V12 = 139',
            'class IVPrismaUI11',
            'DispatchToGameThread',
        ),
        'PrismaHUD_F4_API.h': (
            'enum class InterfaceVersion : uint8_t { V1, V2, V3, V4 };',
            'class IVPrismaHUD2',
            'class IVPrismaHUD3',
            'class IVPrismaHUD4',
        ),
    }
    for name, markers in headers.items():
        header = Path(source_path).with_name(name).read_text(encoding='utf-8')
        for marker in markers:
            if marker not in header:
                raise SystemExit(f'missing synchronized Prisma API contract in {name}: {marker}')


if len(sys.argv) != 3:
    raise SystemExit(f"usage: {Path(sys.argv[0]).name} HUD_TOXIC.html RADHUD.cpp")
check_behavior()
check_source(sys.argv[1], sys.argv[2])
print("HUD fallback behavior verified")