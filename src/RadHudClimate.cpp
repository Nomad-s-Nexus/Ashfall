#include "pch.h"
#include "RadHud.h"

#include <RE/P/PlayerCharacter.h>

#include "PrismaUI_F4_API.h"
#include "PrismaHUD_F4_API.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace
{
    constexpr const char* kClimateViewPath = "Interface/FalloutAnomaly/hud_climate.html";
    constexpr const char* kClimateWidgetId = "rad_climate";
    constexpr int kHudViewOrder = 1'000'000;
    constexpr PRISMA_HUD_API::WidgetDesc kClimateDesc{
        kClimateWidgetId,
        "Climate",
        22.0f / 1920.0f,
        854.0f / 1080.0f,
        360.0f / 1920.0f,
        98.0f / 1080.0f
    };

    int TempTenth(float value) noexcept
    {
        if (!std::isfinite(value))
            return 0;
        return static_cast<int>(std::lround(value * 10.0f));
    }

    int Percent(float value) noexcept
    {
        if (!std::isfinite(value))
            return 0;
        return static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) * 100.0f));
    }
}

void RadHud::ResetClimateForSession() noexcept
{
    _climatePayload.clear();
    _airPayload.clear();
    _toxicPayload.clear();
}

void RadHud::FinalizeClimateDomReady(uint64_t view)
{
    if (!_api)
        return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!api->IsValid(view))
        return;
    api->Show(view);
    PushTheme(view);
    if (_hudApi)
    {
        auto* hudApi = static_cast<PRISMA_HUD_API::IVPrismaHUD1*>(_hudApi);
        hudApi->RegisterWidget(view, CompanionPlaced(kClimateDesc));
        hudApi->ApplyLayout(view);
        _climateWidgetRegistered.store(true, std::memory_order_release);
    }
    _climateDomReady.store(true, std::memory_order_release);
}

void RadHud::EnsureClimateView()
{
    // Constraint: the climate view and its PrismaHUD registration never run inside the load settle window.
    if (!_api || !LoadSettled())
        return;

    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (_climateView != 0 && api->IsValid(_climateView))
    {
        api->SetOrder(_climateView, kHudViewOrder);
        // Constraint: an existing climate view joins the layout only after its DOM-ready finalize, which otherwise registers it.
        if (_hudApi && _climateDomReady.load(std::memory_order_acquire) &&
            !_climateWidgetRegistered.load(std::memory_order_acquire) && BridgeReady())
        {
            auto* hudApi = static_cast<PRISMA_HUD_API::IVPrismaHUD1*>(_hudApi);
            hudApi->RegisterWidget(_climateView, CompanionPlaced(kClimateDesc));
            hudApi->ApplyLayout(_climateView);
            _climateWidgetRegistered.store(true, std::memory_order_release);
        }
        return;
    }

    _climateDomReady.store(false, std::memory_order_release);
    _climateWidgetRegistered.store(false, std::memory_order_release);

    _domLive[kDomSlotClimate].store(0);
    _climateView = api->CreateView(kClimateViewPath, [](PrismaView view) {
        RadHud::NoteDomReady(RadHud::kDomSlotClimate, view);
    });
    _domLive[kDomSlotClimate].store(_climateView);

    if (_climateView != 0 && api->IsValid(_climateView))
    {
        api->SetOrder(_climateView, kHudViewOrder);
        if (!_climateDomReady.load(std::memory_order_acquire))
            api->Hide(_climateView);
        ResetClimateForSession();
        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->info("[HUD] Climate widget view created (handle {}).", _climateView);
    }
}

void RadHud::PushClimate(const RadClimatePolicy::ClimateState& state,
    const RadClimatePolicy::EnvironmentSample& environment, const RadClimatePolicy::StepResult& result)
{
    if (!_api) return;
    // Constraint: build views through the shared retry and only once the player is in the world.
    if (_climateView == 0 || !static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api)->IsValid(_climateView))
    {
        if (!PlayerInWorld()) return;
        StartPrismaViewRetry();
    }
    EnsureClimateView();
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!_climateView || !api->IsValid(_climateView) || !_climateDomReady) return;
    SyncLocalHudLayout();
    const auto rate = [](float value) { return std::round(value * 60000.0f) / 1000.0f; };
    json data{
        { "core", TempTenth(state.coreC) / 10.0 },
        { "ambient", TempTenth(environment.ambientC) / 10.0 },
        { "warmth", Percent(RadClimatePolicy::BodyWarmth01(state.coreC)) },
        { "wetness", Percent(state.wetness01) },
        { "insulation", Percent(result.effectiveInsulation01) },
        { "heat", Percent(environment.heat01) },
        { "stage", static_cast<int>(state.stage) },
        { "wind", TempTenth(RadClimatePolicy::VisibleWindMps(environment)) / 10.0 },
        { "shelter", Percent(environment.shelter01) },
        { "rainCover", Percent(environment.precipitationShelter01) },
        { "indoors", environment.indoors },
        { "cooling", rate(result.coolingCPerSecond) },
        { "warming", rate(result.warmingCPerSecond) }
    };
    // Constraint: invalid UTF-8 in a mod string must not throw out of the tick.
    const auto payload = data.dump(-1, ' ', false, json::error_handler_t::replace);
    if (payload == _climatePayload) return;
    _climatePayload = payload;
    api->InteropCall(_climateView, "updateClimate", payload.c_str());
    if (_debugOpen && _debugView && _debugDomReady && api->IsValid(_debugView))
        api->InteropCall(_debugView, "updateClimateInfo", payload.c_str());
    api->Show(_climateView);
}
