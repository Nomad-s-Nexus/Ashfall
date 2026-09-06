#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// Optional on-screen radiation HUD rendered via the PrismaUI_F4 framework.
// Shows two bars: particulate (vanilla Rads) and persistent tissue damage.
// Entirely optional — if PrismaUI_F4 is not installed, every call is a safe no-op.
class RadHud
{
public:
    static RadHud& GetSingleton()
    {
        static RadHud instance;
        return instance;
    }

    // kGameDataReady / recovery lifecycle: acquire optional Prisma UI/HUD APIs.
    void RequestAPI();
    bool StartPrismaViewRetry();

    // kPostLoadGame / kNewGame: (re)create the HUD view.
    void CreateView();

    void PushToxic(float tissue, float permanentTissue);

    // First-person gas-mask screen overlay: show/hide the fullscreen mask layer in the HUD view.
    // Sends only on state change (cheap to call every tick). No-op if PrismaUI is absent.
    void PushMask(bool on);

    // Gas-mask filter + condition readout: filter charge 0..100 (-1 = system off), mask condition
    // 0..100 (-1 = system off), spare-filter count, and whether the mask is worn (the widget shows
    // only while masked). No-op if PrismaUI is absent.
    void PushFilter(float charge, float condition, int spares, bool masked);

    // Show the SPECIAL-penalty diagnostic in a simple PrismaUI popup (the vanilla HUD corner truncates
    // long multi-line reports). Returns false if PrismaUI is unavailable (caller falls back to corner).
    bool ShowSpecialInfo(const std::vector<std::string>& lines);

    // Push storm state to the HUD overlay. phase = StormPhase enum value; theme selects
    // the overlay reskin (1 fire, 2 sulfur, 3 acid, 4 ash; 0 none); intensity 0..1;
    // sheltered = player indoors; lethal = blowout vs ambient hazard. No-op if absent.
    void PushStorm(int phase, int theme, float intensity, bool sheltered, bool lethal);

    // Debug menu (separate focusable PrismaUI view, toggled with Shift+F8).
    void ToggleDebugMenu();
    void SetDebugMenu(bool open);
    void OpenHudLayoutEditor();
    void ApplyLocalHudLayout(const std::string& payload);
    void SyncLocalHudLayout();
    void SetLocalHudEditorPreview(bool on);
    void InvalidateLocalHudLayoutViews();
    void ResetDebugMenuForSession();

    // Apply a debug-menu toggle that arrived before the view finished loading its DOM. Called every
    // frame from PollInput (verified game-main thread). No-op unless a toggle is pending AND the view
    // is now DOM-ready. This is why the first Shift+F8 pressed during view init is not lost.
    void PumpDeferredDebug();

    // Called by the menu-open sink when a blocking modal (pause/system menu OR a message box)
    // opens: closes the debug menu so its focused view doesn't capture the cursor and lock the
    // player out of that menu (soft-lock / force-quit). Safe no-op if the debug menu is closed.
    void OnBlockingMenuOpened();

    [[nodiscard]] bool Available() const { return _api != nullptr; }
    [[nodiscard]] bool HudLayoutAvailable() const { return _hudApi != nullptr; }
    [[nodiscard]] bool ViewsReady() const
    {
        return _viewsReady.load() && _domReady.load() && _toxicDomReady.load() && _filterDomReady.load();
    }

private:
    RadHud() = default;
    ~RadHud() = default;

    void* _api = nullptr;       // PRISMA_UI_API::IVPrismaUI2*
    void* _threadApi = nullptr; // PRISMA_UI_API::IVPrismaUI11*
    std::atomic<bool> _viewCreateRetry{ false };
    std::atomic<bool> _viewsReady{ false };
    uint64_t _view = 0;         // PrismaView handle (0 = invalid)
    // atomic: written on the Ultralight/JS thread (DOM-ready callback), read on the game thread
    // every 4Hz tick — a plain bool is a data race (benign on x86, still UB).
    std::atomic<bool> _domReady{ false };

    void* _hudApi = nullptr;    // PRISMA_HUD_API::IVPrismaHUD1* (optional; positions the toxic widget)
    void* _hudEditorApi = nullptr;
    uint64_t _toxicView = 0;    // standalone toxicity widget view registered with PrismaHUD
    std::atomic<bool> _toxicDomReady{ false };
    std::atomic<bool> _toxicWidgetRegistered{ false };

    // Standalone gas-mask FILTER/MASK bar widget view (hud_filter.html), split out of the fullscreen
    // radhud.html overlay so PrismaHUD_F4 can position it in the HUD editor. The visor + fog stay in
    // radhud.html; PushFilter drives both (this view for the bars, _view for the fog).
    uint64_t _filterView = 0;
    std::atomic<bool> _filterDomReady{ false };

    // Last gas-mask overlay state pushed to the view (-1 = none yet). Reset to -1 in CreateView so a
    // freshly (re)created view is re-synced on the next tick (the JS overlay starts hidden each load).
    int _maskPushed = -1;

    // Debug menu is baked into the single HUD view; focus toggles on open/close.
    bool _debugOpen = false;

    // Deferred debug-menu toggle: a Shift+F8 pressed before the view exists or while its DOM loads is
    // retained until PumpDeferredDebug can apply the desired state. Touched only on the game-main thread,
    // so no atomic is needed; the cross-thread readiness signal (_domReady) is already atomic.
    bool _debugTogglePending = false;   // a toggle is waiting for the view to become DOM-ready
    bool _debugDesiredOpen   = false;   // the state to apply once ready (last press wins)
    std::uint64_t _debugNextFocusRetryMs = 0;
};
