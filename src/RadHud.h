#pragma once

#include "ClimateProtectionRuntime.h"
#include "NeedsPolicy.h"

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
    void OpenHudLayoutEditor();
    void ApplyLocalHudLayout(const std::string& payload);
    void SyncLocalHudLayout();
    void SetLocalHudEditorPreview(bool on);
    void InvalidateLocalHudLayoutViews();
    void ResetDebugMenuForSession();
    void PumpDeferredDebug();
    void OnBlockingMenuOpened();

    [[nodiscard]] bool PlayerInWorld() const;
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
    std::string _keysPayload;
    std::atomic<int> _companionMode{ -1 };
    uint64_t _needsView = 0;
    std::atomic<uint64_t> _needsReadyView{ 0 };
    bool _needsWidgetRegistered = false;
    std::string _needsPayload;
    std::string _survivalPayload;

    int _maskPushed = -1;

    bool _debugOpen = false;
    bool _debugTogglePending = false;
    bool _debugDesiredOpen = false;

    std::uint64_t _debugNextFocusRetryMs = 0;
};
