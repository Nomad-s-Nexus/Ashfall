#include "pch.h"
#include "RadHud.h"
#include "PrismaUI_F4_API.h"
#include "PrismaHUD_F4_API.h"

void RadHud::PushNeeds(const RadNeedsPolicy::NeedsState& state)
{
    if (!_api) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!_needsView || !api->IsValid(_needsView))
    {
        _needsReadyView.store(0);
        _needsWidgetRegistered = false;
        _needsPayload.clear();
        _needsView = api->CreateView("Interface/FalloutAnomaly/hud_needs.html", [](PrismaView view) {
            RadHud::GetSingleton()._needsReadyView.store(view);
        });
        if (_needsView && api->IsValid(_needsView))
        {
            api->SetOrder(_needsView, 1'000'000);
            api->Hide(_needsView);
        }
    }
    if (!_needsView || !api->IsValid(_needsView) || _needsReadyView.load() != _needsView) return;
    if (_hudApi && !_needsWidgetRegistered)
    {
        auto* hud = static_cast<PRISMA_HUD_API::IVPrismaHUD1*>(_hudApi);
        hud->RegisterWidget(_needsView, { "rad_needs", "Needs", 22.0f / 1920.0f,
            812.0f / 1080.0f, 176.0f / 1920.0f, 58.0f / 1080.0f });
        hud->ApplyLayout(_needsView);
        _needsWidgetRegistered = true;
    }
    SyncLocalHudLayout();
    const auto percent = [](float value) {
        return static_cast<int>(std::lround(RadNeedsPolicy::Clamp01(value) * 100.0f));
    };
    const auto payload = json{
        { "hunger", percent(state.hunger01) },
        { "thirst", percent(state.thirst01) },
        { "sleepDebt", percent(state.sleepDebt01) }
    }.dump();
    if (payload == _needsPayload) return;
    _needsPayload = payload;
    api->InteropCall(_needsView, "updateNeeds", payload.c_str());
    api->Show(_needsView);
}
