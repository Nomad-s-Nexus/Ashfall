#pragma once

#include "ClimateProtectionRuntime.h"

#include <atomic>
#include <string>
#include <vector>

class RadHud
{
public:
    static RadHud& GetSingleton()
    {
        static RadHud instance;
        return instance;
    }

    void RequestAPI();
    void CreateView();

    void PushToxic(float tissue, float permanentTissue);
    void PushMask(bool on);
    void PushFilter(float charge, float condition, int spares, bool masked);
    void PushClimate(float coreC, float ambientC, float wetness01, float insulation01, float heat01, int stage);
    void PushArmor(float insulation01, float effectiveInsulation01, bool powerArmor,
                   const std::vector<ClimateProtectionRuntime::ArmorInfo>& armor);
    void PushGhoulSettings(bool enabled, int chancePercent);
    void OpenHudEditor();
    void HandleHudLayoutCommand(const std::string& command);
    void ResetClimateForSession() noexcept;

    bool ShowSpecialInfo(const std::vector<std::string>& lines);
    void PushStorm(int phase, int theme, float intensity, bool sheltered, bool lethal);

    void ToggleDebugMenu();
    void SetDebugMenu(bool open);
    void ResetDebugMenuForSession();
    void PumpDeferredDebug();
    void OnBlockingMenuOpened();

    [[nodiscard]] bool Available() const { return _api != nullptr; }
    [[nodiscard]] bool HudLayoutAvailable() const { return _hudApi != nullptr; }
    [[nodiscard]] bool ViewsReady() const { return _viewsReady.load(); }

private:
    RadHud() = default;
    ~RadHud() = default;

    void StartPrismaViewRetry();
    void EnsureClimateView();

    void* _api = nullptr;
    void* _threadApi = nullptr;
    std::atomic<bool> _viewCreateRetry{ false };
    std::atomic<bool> _viewsReady{ false };
    uint64_t _view = 0;
    std::atomic<bool> _domReady{ false };

    void* _hudApi = nullptr;
    uint64_t _toxicView = 0;
    std::atomic<bool> _toxicDomReady{ false };
    std::atomic<bool> _toxicWidgetRegistered{ false };

    uint64_t _filterView = 0;
    std::atomic<bool> _filterDomReady{ false };

    uint64_t _climateView = 0;
    std::atomic<bool> _climateDomReady{ false };
    std::atomic<bool> _climateWidgetRegistered{ false };
    int _climateCoreTenth = -100000;
    int _climateAmbientTenth = -100000;
    int _climateWetPercent = -1;
    int _climateInsulationPercent = -1;
    int _climateHeatPercent = -1;
    int _climateStage = -1;
    std::string _armorPayload;

    int _maskPushed = -1;

    bool _debugOpen = false;
    bool _debugTogglePending = false;
    bool _debugDesiredOpen = false;

    struct LocalLayout {
        float x;
        float y;
        float s;
        float r;
        bool hidden;
    };
    LocalLayout _toxicLayout{ 30.0f / 1920.0f, 882.0f / 1080.0f, 1.0f, 0.0f, false };
    LocalLayout _filterLayout{ 22.0f / 1920.0f, 1014.0f / 1080.0f, 1.0f, 0.0f, false };
    LocalLayout _climateLayout{ 22.0f / 1920.0f, 944.0f / 1080.0f, 1.0f, 0.0f, false };
    bool _climateFahrenheit = false;
    bool _localLayoutLoaded = false;
    bool _localLayoutApplied = false;

    void LoadLocalHudLayout();
    bool SaveLocalHudLayout();
    void ApplyLocalHudLayout(bool preview);
    std::string LocalHudLayoutJson() const;
};
