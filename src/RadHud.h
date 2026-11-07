#pragma once

#include "ClimateProtectionRuntime.h"
#include "NeedsPolicy.h"
#include "RadCorrectnessPolicy.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace PRISMA_HUD_API { struct WidgetDesc; }

// Constraint: in companion mode our widgets default beside Phosphor's HP/AP/RAD rings; a saved layout still wins.
bool CompanionWidgetDefault(const char* id, float& x, float& y);
PRISMA_HUD_API::WidgetDesc CompanionPlaced(const PRISMA_HUD_API::WidgetDesc& desc);

class RadHud
{
public:
    static RadHud& GetSingleton()
    {
        static RadHud instance;
        return instance;
    }

    void RequestAPI();
    bool StartPrismaViewRetry();
    void CreateView();

    void PushToxic(float tissue, float permanentTissue);
    void PushMask(bool on);
    void PushFilter(float charge, float condition, int spares, bool masked);
    void PushClimate(const RadClimatePolicy::ClimateState& state,
        const RadClimatePolicy::EnvironmentSample& environment, const RadClimatePolicy::StepResult& result);
    void PushAir(const std::string& payload);
    void PushNeeds(const RadNeedsPolicy::NeedsSnapshot& snapshot);
    void HideNeeds();
    void PushSurvival(const RadNeedsPolicy::NeedsSnapshot& snapshot);
    void PushArmor(float insulation01, float effectiveInsulation01, bool powerArmor,
                   const std::vector<ClimateProtectionRuntime::ArmorInfo>& armor);
    void PushGhoulSettings(bool enabled, float chance);
    void PushNeedsTuning(const std::string& json);
    void ResetClimateForSession() noexcept;
    void PushTheme(uint64_t view);

    bool IsCompanionMode();
    bool ShowSpecialInfo(const std::vector<std::string>& lines);
    void PushStorm(int phase, int theme, float intensity, bool sheltered, bool lethal);

    void ToggleDebugMenu();
    void SetDebugMenu(bool open);
    void RequestDebugClose() noexcept;
    [[nodiscard]] bool DebugMenuOpen() const noexcept { return _debugOpen; }
    void OpenHudLayoutEditor();
    void ApplyLocalHudLayout(const std::string& payload);
    void SyncLocalHudLayout();
    void SetLocalHudEditorPreview(bool on);
    void InvalidateLocalHudLayoutViews();
    void ResetDebugMenuForSession();
    void PumpDeferredDebug();
    // Constraint: the close refresh runs only from the 4 Hz tick, on the game thread, and never while the menu is open.
    void PumpDebugViewRefresh();
    void ReleaseStaleDebugFocus() noexcept;
    // Constraint: the 4 Hz tick re-sends the native debug state to the page only when it differs from the last delivered state.
    void SyncDebugPanelState() noexcept;
    void OnBlockingMenuOpened();
    // Constraint: read from the PlayerCharacter::Update worker, so pending state is atomic.
    [[nodiscard]] bool DebugWorkPending() const noexcept { return _debugCloseRequested.load() || _debugTogglePending.load(); }

    [[nodiscard]] bool PlayerInWorld() const;
    [[nodiscard]] bool ViewsExist() const;
    [[nodiscard]] bool Available() const { return _api != nullptr; }
    [[nodiscard]] bool HudLayoutAvailable() const { return _hudApi != nullptr; }
    [[nodiscard]] bool ViewsReady() const
    {
        return _viewsReady.load() && _domReady.load() && _toxicDomReady.load() && _filterDomReady.load();
    }

private:
    RadHud() = default;
    ~RadHud() = default;

    void EnsureClimateView();
    void PushFilterKeys(uint64_t view);

    void* _api = nullptr;       // PRISMA_UI_API::IVPrismaUI2*
    void* _threadApi = nullptr; // PRISMA_UI_API::IVPrismaUI11*
    std::atomic<bool> _viewCreateRetry{ false };
    std::atomic<std::uint64_t> _viewRetryNotBeforeMs{ 0 };
    std::atomic<bool> _viewsReady{ false };
    uint64_t _view = 0;
    std::atomic<bool> _domReady{ false };

    void* _hudApi = nullptr;    // PRISMA_HUD_API::IVPrismaHUD1* (optional; positions the toxic widget)
    void* _hudEditorApi = nullptr;
    uint64_t _toxicView = 0;    // standalone toxicity widget view registered with PrismaHUD
    std::atomic<bool> _toxicDomReady{ false };
    std::atomic<bool> _toxicWidgetRegistered{ false };

    uint64_t _filterView = 0;
    std::atomic<bool> _filterDomReady{ false };

    uint64_t _climateView = 0;
    std::atomic<bool> _climateDomReady{ false };
    std::atomic<bool> _climateWidgetRegistered{ false };
    std::string _climatePayload;
    std::string _airPayload;
    std::string _toxicPayload;
    std::string _armorPayload;
    std::string _filterMainPayload;   // last updateFilter sent to _view
    std::string _filterBarPayload;    // last updateFilter sent to _filterView
    std::string _stormPayload;        // last stormPhase sent to _view
    int _keysReplaceSent = -2;        // last VK sent in setKeys (-2 = never sent)
    int _keysRepairSent = -2;
    std::atomic<int> _companionMode{ -1 };
    uint64_t _needsView = 0;
    std::atomic<uint64_t> _needsReadyView{ 0 };
    bool _needsWidgetRegistered = false;
    std::string _needsPayload;
    std::string _survivalPayload;

    int _maskPushed = -1;

    bool _debugOpen = false;
    // Constraint: last SetDebugMenu value delivered to the page (1 open, 0 closed, -1 unknown for a new view); game thread only.
    int _debugPushedOpen = -1;
    void PushDebugPanelState(bool open) noexcept;
    std::atomic_bool _debugCloseRequested{ false };
    std::atomic_bool _debugTogglePending{ false };
    bool _debugDesiredOpen = false;

    std::uint64_t _debugNextFocusRetryMs = 0;
    std::uint64_t _debugFocusFailedSinceMs = 0;
    // Constraint: game thread only; armed by a close that followed an open, cleared by ResetDebugMenuForSession.
    RadCorrectnessPolicy::DebugViewRefreshState _debugRefresh{};
};
