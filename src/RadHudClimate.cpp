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
        944.0f / 1080.0f,
        250.0f / 1920.0f,
        58.0f / 1080.0f
    };

    std::atomic<std::atomic<bool>*> g_climateDomReadyFlag{ nullptr };
    std::atomic<std::atomic<bool>*> g_climateWidgetRegisteredFlag{ nullptr };
    std::atomic<PRISMA_UI_API::IVPrismaUI2*> g_climateApiForCb{ nullptr };
    std::atomic<PRISMA_HUD_API::IVPrismaHUD1*> g_climateHudApiForCb{ nullptr };

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
    _climateCoreTenth = -100000;
    _climateAmbientTenth = -100000;
    _climateWetPercent = -1;
    _climateInsulationPercent = -1;
    _climateHeatPercent = -1;
    _climateStage = -1;
}

void RadHud::EnsureClimateView()
{
    if (!_api)
        return;

    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (_climateView != 0 && api->IsValid(_climateView))
    {
        api->SetOrder(_climateView, kHudViewOrder);
        if (_hudApi && !_climateWidgetRegistered.load(std::memory_order_acquire))
        {
            auto* hudApi = static_cast<PRISMA_HUD_API::IVPrismaHUD1*>(_hudApi);
            hudApi->RegisterWidget(_climateView, kClimateDesc);
            hudApi->ApplyLayout(_climateView);
            _climateWidgetRegistered.store(true, std::memory_order_release);
        }
        return;
    }

    _climateDomReady.store(false, std::memory_order_release);
    _climateWidgetRegistered.store(false, std::memory_order_release);
    g_climateDomReadyFlag.store(&_climateDomReady, std::memory_order_release);
    g_climateWidgetRegisteredFlag.store(&_climateWidgetRegistered, std::memory_order_release);
    g_climateApiForCb.store(api, std::memory_order_release);
    g_climateHudApiForCb.store(static_cast<PRISMA_HUD_API::IVPrismaHUD1*>(_hudApi), std::memory_order_release);

    _climateView = api->CreateView(kClimateViewPath, [](PrismaView view) {
        auto* climateApi = g_climateApiForCb.load(std::memory_order_acquire);
        auto* hudApi = g_climateHudApiForCb.load(std::memory_order_acquire);
        if (climateApi)
            climateApi->Show(view);
        if (climateApi && hudApi && climateApi->IsValid(view))
        {
            hudApi->RegisterWidget(view, kClimateDesc);
            hudApi->ApplyLayout(view);
            if (auto* flag = g_climateWidgetRegisteredFlag.load(std::memory_order_acquire))
                flag->store(true, std::memory_order_release);
        }
        if (auto* flag = g_climateDomReadyFlag.load(std::memory_order_acquire))
            flag->store(true, std::memory_order_release);
    });

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

void RadHud::PushClimate(
    float coreC,
    float ambientC,
    float wetness01,
    float insulation01,
    float heat01,
    int stage)
{
    if (!_api)
        return;

    EnsureClimateView();
    if (!_hudApi && !_localLayoutApplied)
        ApplyLocalHudLayout(false);

    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (_climateView == 0 || !api->IsValid(_climateView) ||
        !_climateDomReady.load(std::memory_order_acquire))
        return;

    const int coreTenth = TempTenth(coreC);
    const int ambientTenth = TempTenth(ambientC);
    const int wetPercent = Percent(wetness01);
    const int insulationPercent = Percent(insulation01);
    const int heatPercent = Percent(heat01);
    const int stageValue = std::clamp(stage, 0, 5);

    if (coreTenth == _climateCoreTenth &&
        ambientTenth == _climateAmbientTenth &&
        wetPercent == _climateWetPercent &&
        insulationPercent == _climateInsulationPercent &&
        heatPercent == _climateHeatPercent &&
        stageValue == _climateStage)
        return;

    _climateCoreTenth = coreTenth;
    _climateAmbientTenth = ambientTenth;
    _climateWetPercent = wetPercent;
    _climateInsulationPercent = insulationPercent;
    _climateHeatPercent = heatPercent;
    _climateStage = stageValue;

    char payload[224];
    std::snprintf(payload, sizeof(payload),
        "{\"core\":%.1f,\"ambient\":%.1f,\"wetness\":%d,\"insulation\":%d,\"heat\":%d,\"stage\":%d}",
        static_cast<double>(coreTenth) / 10.0,
        static_cast<double>(ambientTenth) / 10.0,
        wetPercent,
        insulationPercent,
        heatPercent,
        stageValue);

    api->InteropCall(_climateView, "updateClimate", payload);
    api->Show(_climateView);
}
