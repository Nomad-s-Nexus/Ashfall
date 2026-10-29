#include "pch.h"
#include "RadHud.h"
#include <RE/D/DifficultyLevel.h>
#include <RE/P/PlayerCharacter.h>
#include "PrismaUI_F4_API.h"
#include "PrismaHUD_F4_API.h"

namespace
{
    bool SurvivalLayerActive()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        return !player || player->GetDifficultyLevel() != RE::DifficultyLevel::kTrueSurvival;
    }
}

void RadHud::PushNeeds(const RadNeedsPolicy::NeedsSnapshot& snapshot)
{
    if (!_api) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!_needsView || !api->IsValid(_needsView))
    {
        _needsReadyView.store(0);
        _needsWidgetRegistered = false;
        _needsPayload.clear();
        // Constraint: build views through the shared retry and only once the player is in the world.
        StartPrismaViewRetry();
        if (!PlayerInWorld()) return;
        _needsView = api->CreateView("Interface/FalloutAnomaly/hud_needs.html", [](PrismaView view) {
            RadHud::GetSingleton().PushTheme(view);
            RadHud::GetSingleton()._needsReadyView.store(view);
        });
        if (_needsView && api->IsValid(_needsView))
        {
            api->SetOrder(_needsView, 1'000'000);
            api->Hide(_needsView);
        }
    }
    if (!_needsView || !api->IsValid(_needsView) || _needsReadyView.load() != _needsView) return;
    if (IsCompanionMode() && !SurvivalLayerActive())
    {
        if (!_needsPayload.empty())
        {
            api->Hide(_needsView);
            _needsPayload.clear();
        }
        return;
    }
    if (_hudApi && !_needsWidgetRegistered)
    {
        auto* hud = static_cast<PRISMA_HUD_API::IVPrismaHUD1*>(_hudApi);
        hud->RegisterWidget(_needsView, CompanionPlaced({ "rad_needs", "Needs", 22.0f / 1920.0f,
            812.0f / 1080.0f, 176.0f / 1920.0f, 58.0f / 1080.0f }));
        hud->ApplyLayout(_needsView);
        _needsWidgetRegistered = true;
    }
    SyncLocalHudLayout();
    const auto percent = [](float value) {
        return static_cast<int>(std::lround(RadNeedsPolicy::Clamp01(value) * 100.0f));
    };
    const auto payload = json{
        { "hunger", percent(snapshot.pools.hunger01) },
        { "thirst", percent(snapshot.pools.thirst01) },
        { "sleepDebt", percent(snapshot.pools.sleepDebt01) },
        { "risk", percent(snapshot.risk01) },
        { "disease", snapshot.diseasePacked }
    }.dump();
    if (payload == _needsPayload) return;
    _needsPayload = payload;
    api->InteropCall(_needsView, "updateNeeds", payload.c_str());
    api->Show(_needsView);
}
void RadHud::PushSurvival(const RadNeedsPolicy::NeedsSnapshot& snapshot)
{
    // Constraint: the SURVIVAL tab reads the same snapshot the chips do, so the two can never disagree.
    if (!_api || !_debugOpen || _view == 0 || !_domReady) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!api->IsValid(_view)) return;
    const auto percent = [](float value) {
        return static_cast<int>(std::lround(RadNeedsPolicy::Clamp01(value) * 100.0f));
    };
    const auto payload = json{
        { "hunger", percent(snapshot.pools.hunger01) },
        { "thirst", percent(snapshot.pools.thirst01) },
        { "sleepDebt", percent(snapshot.pools.sleepDebt01) },
        { "risk", percent(snapshot.risk01) },
        { "disease", snapshot.diseasePacked }
    }.dump();
    if (payload == _survivalPayload) return;
    _survivalPayload = payload;
    api->InteropCall(_view, "updateSurvival", payload.c_str());
}
