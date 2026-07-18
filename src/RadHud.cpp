#include "pch.h"
#include "RadHud.h"
#include "RadiationEngine.h"

// RE headers BEFORE PrismaUI_F4_API.h: the API header pulls in Windows.h, whose macros clobber
// engine struct layouts (e.g. TESFile) if the RE headers are parsed afterwards.
#include <RE/B/BSTEvent.h>
#include <RE/M/MenuOpenCloseEvent.h>
#include <RE/M/MessageBoxMenu.h>
#include <RE/P/PauseMenu.h>
#include <RE/U/UI.h>
// For the in-world HUD gate (Pip-Boy possession + current location).
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESObjectREFR.h>
#include <RE/T/TESObjectARMO.h>
#include <RE/B/BGSInventoryList.h>
#include <RE/B/BGSBipedObjectForm.h>
#include <RE/B/BIPED_OBJECT.h>
// Calendar year — fallback in-world signal when the Pip-Boy slot scan misses (alt-start / replacers).
#include <RE/C/Calendar.h>
#include <RE/T/TESGlobal.h>
// Vanilla-HUD corner message for the debug-menu fallback when PrismaUI is absent.
#include <RE/S/SendHUDMessage.h>

#include "PrismaUI_F4_API.h"
#include "PrismaHUD_F4_API.h"

#include <atomic>

#include <cstdio>

namespace
{
    // The view's HTML path relative to Data/PrismaUI_F4/views/.
    constexpr const char* kViewPath = "Interface/FalloutAnomaly/radhud.html";
    // Standalone toxicity widget, registered with PrismaHUD_F4 for positioning.
    constexpr const char* kToxicViewPath = "Interface/FalloutAnomaly/hud_toxic.html";
    constexpr const char* kToxicWidgetId = "rad_toxic";
    // Standalone gas-mask FILTER/MASK bar widget (split out of radhud.html for PrismaHUD positioning).
    constexpr const char* kFilterViewPath = "Interface/FalloutAnomaly/hud_filter.html";
    constexpr const char* kFilterWidgetId = "rad_filter";

    // PrismaHUD WidgetDesc default position/size are FRACTIONS of the 1920x1080 reference (value/ref),
    // NOT pixels -- the editor multiplies them by the live viewport. (The API header's "pixels" comment
    // was wrong and is corrected there now; passing raw pixels puts the editor drag box off-screen at
    // e.g. 30*1920, which is why the toxicity widget registered but never appeared usable in the editor.)
    constexpr PRISMA_HUD_API::WidgetDesc kToxicDesc{
        kToxicWidgetId, "Toxicity", 30.0f / 1920.0f, 882.0f / 1080.0f, 200.0f / 1920.0f, 44.0f / 1080.0f };
    constexpr PRISMA_HUD_API::WidgetDesc kFilterDesc{
        kFilterWidgetId, "Gas Mask Filter", 22.0f / 1920.0f, 1014.0f / 1080.0f, 240.0f / 1920.0f, 44.0f / 1080.0f };

    // HUD gate: latches once the player is in the world (cleared each load in
    // CreateView) so the inventory scan only runs during the intro.
    std::atomic<bool> g_inWorld{ false };

    // Vanilla, start-method-agnostic "intro is over" check: the player holds a
    // Pip-Boy (any armor filling the kPipboy biped slot - replacer-proof, no
    // FormID) and has a current location. True at vault exit in the vanilla
    // intro; true immediately for alternate starts that grant the Pip-Boy; false
    // through the whole pre-Pip-Boy prewar sequence (including character creation).
    bool IsPlayerInWorld()
    {
        if (g_inWorld.load()) return true;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->GetCurrentLocation()) return false;

        auto* inv = player->inventoryList;
        if (!inv) return false;

        bool hasPipboy = false;
        inv->ForEachStack(
            [](RE::BGSInventoryItem& a_item) -> bool {
                auto* obj = a_item.object;
                if (!obj || obj->GetFormType() != RE::ENUM_FORM_ID::kARMO) return false;
                auto* armo = static_cast<RE::TESObjectARMO*>(obj);
                return armo->FillsBipedSlot(static_cast<std::int32_t>(RE::BIPED_OBJECT::kPipboy));
            },
            [&hasPipboy](RE::BGSInventoryItem&, RE::BGSInventoryItem::Stack&) -> bool {
                hasPipboy = true;
                return false;  // found one - stop iterating
            });

        if (hasPipboy) { g_inWorld.store(true); return true; }

        // Fallback: some setups never trip the Pip-Boy slot scan — alternate-start mods, Pip-Boy
        // replacers that change the biped slot, or holstered/hidden Pip-Boy frameworks. For those the
        // gate would stay false forever and the HUD vitals never populate (empty grey bars). If the
        // calendar reads a postwar year, the prewar intro is definitively over, so latch in-world
        // anyway. (Prewar Vault 111 / chargen is 2077, well under this threshold, so it stays hidden.)
        if (auto* cal = RE::Calendar::GetSingleton(); cal && cal->gameYear &&
            cal->gameYear->GetValue() > 2100.0f)
        {
            g_inWorld.store(true);
            return true;
        }
        return false;
    }

    // Static so the OnDomReady callback (a plain function pointer) can reach the
    // singleton's state. RadHud is a singleton so this is safe.
    std::atomic<bool>* g_domReadyFlag = nullptr;
    void*              g_apiForCb     = nullptr;

    // Same pattern for the toxicity widget view's OnDomReady callback (registers with PrismaHUD).
    std::atomic<bool>* g_toxicDomReadyFlag = nullptr;
    void*              g_hudApiForCb       = nullptr;

    // And for the filter/mask bar widget view's OnDomReady callback (also registers with PrismaHUD).
    std::atomic<bool>* g_filterDomReadyFlag = nullptr;

    // JS -> C++ bridge for the debug menu buttons. window.radDebug("storm:0") etc.
    void OnRadDebugCommand(const char* arg)
    {
        RadiationEngine::GetSingleton().HandleDebugCommand(arg ? arg : "");
    }

    // Closes the rad debug menu when a blocking modal opens. The debug menu focuses the PrismaUI
    // view (which captures input for its cursor); without this, the still-focused view eats the
    // cursor and the player is locked out of the modal:
    //   * PauseMenu      — pressing Esc opens the pause menu under the focused view (force-quit).
    //   * MessageBoxMenu — any popup (quest/mod/yes-no prompt) can't be answered -> soft-lock.
    class BlockingMenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
    {
    public:
        static BlockingMenuSink* GetSingleton() { static BlockingMenuSink s; return &s; }
        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event,
                                              RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
        {
            if (a_event.opening &&
                (a_event.menuName == RE::PauseMenu::MENU_NAME ||
                 a_event.menuName == RE::MessageBoxMenu::MENU_NAME))
                RadHud::GetSingleton().OnBlockingMenuOpened();
            return RE::BSEventNotifyControl::kContinue;
        }
    };
}

void RadHud::RequestAPI()
{
    auto logger = spdlog::get("FalloutAnomaly");
    _api = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI2>();
    if (_api)
        logger->info("[HUD] PrismaUI_F4 API acquired — radiation HUD enabled.");
    else
        logger->info("[HUD] PrismaUI_F4 not present — radiation HUD disabled (optional).");

    // Optional: PrismaHUD_F4 positions the toxicity widget in the unified HUD editor. If absent,
    // the toxic widget still renders at its default position (no-op fallbacks in its JS).
    _hudApi = PRISMA_HUD_API::RequestPluginAPI();
    logger->info("[HUD] PrismaHUD_F4 {} — toxicity widget {}.",
        _hudApi ? "API acquired" : "not present",
        _hudApi ? "will be positionable in the HUD editor" : "uses its default position");
}

void RadHud::CreateView()
{
    auto logger = spdlog::get("FalloutAnomaly");
    if (!_api) return;

    // Re-evaluate the in-world gate for each loaded game / new game.
    g_inWorld.store(false);
    // Force a mask-overlay re-sync on the next tick: a freshly (re)created view starts with the
    // overlay hidden, so the cached state must not suppress the first push after a load.
    _maskPushed = -1;

    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);

    // Register the blocking-menu sink once so the pause menu / message boxes close the debug menu
    // instead of soft-locking the player (focused view would otherwise keep eating the cursor).
    static bool s_menuSinkRegistered = false;
    if (!s_menuSinkRegistered)
    {
        if (auto* ui = RE::UI::GetSingleton())
        {
            ui->RegisterSink(BlockingMenuSink::GetSingleton());
            s_menuSinkRegistered = true;
            logger->info("[HUD] Blocking-menu sink registered (pause menu + message boxes close the debug menu).");
        }
    }

    // Shared callback state (both views' OnDomReady callbacks are plain function pointers).
    g_apiForCb    = _api;
    g_hudApiForCb = _hudApi;

    // --- Main overlay view (radhud.html: gas-mask overlay, filter, storm banners, popup, debug) ---
    if (_view == 0 || !api->IsValid(_view))
    {
        _domReady = false;
        g_domReadyFlag = &_domReady;

        _view = api->CreateView(kViewPath, [](PrismaView v) {
            if (g_apiForCb) {
                auto* a = static_cast<PRISMA_UI_API::IVPrismaUI2*>(g_apiForCb);
                a->RegisterJSListener(v, "radDebug", OnRadDebugCommand);
                a->Show(v);
            }
            // Flag LAST: a Push seeing _domReady=true must find the listener already registered.
            if (g_domReadyFlag) g_domReadyFlag->store(true);
        });

        // Hide-until-DOM-ready. Guarded: if the DOM-ready callback already ran (synchronous or
        // very fast), its Show must win — an unconditional Hide here would blank the HUD all session.
        if (_view != 0) { logger->info("[HUD] Radiation overlay view created (handle {}).", _view); if (!_domReady.load()) api->Hide(_view); }
        else            logger->warn("[HUD] CreateView returned invalid handle.");
    }

    // --- Toxicity widget view (hud_toxic.html), registered with PrismaHUD_F4 for positioning ---
    if (_toxicView == 0 || !api->IsValid(_toxicView))
    {
        _toxicDomReady = false;
        g_toxicDomReadyFlag = &_toxicDomReady;

        _toxicView = api->CreateView(kToxicViewPath, [](PrismaView v) {
            if (g_apiForCb) static_cast<PRISMA_UI_API::IVPrismaUI2*>(g_apiForCb)->Show(v);
            if (g_hudApiForCb)
                static_cast<PRISMA_HUD_API::IVPrismaHUD1*>(g_hudApiForCb)->RegisterWidget(v, kToxicDesc);
            if (g_toxicDomReadyFlag) g_toxicDomReadyFlag->store(true);
        });

        // Same guarded hide-until-DOM-ready as the main view above.
        if (_toxicView != 0) { logger->info("[HUD] Toxicity widget view created (handle {}).", _toxicView); if (!_toxicDomReady.load()) api->Hide(_toxicView); }
    }
    else if (_hudApi)
    {
        // Existing toxic view on reload: re-register so PrismaHUD re-applies the saved layout.
        static_cast<PRISMA_HUD_API::IVPrismaHUD1*>(_hudApi)->RegisterWidget(_toxicView, kToxicDesc);
    }

    // --- Filter/mask bar widget view (hud_filter.html), registered with PrismaHUD_F4 for positioning ---
    if (_filterView == 0 || !api->IsValid(_filterView))
    {
        _filterDomReady = false;
        g_filterDomReadyFlag = &_filterDomReady;

        _filterView = api->CreateView(kFilterViewPath, [](PrismaView v) {
            if (g_apiForCb) static_cast<PRISMA_UI_API::IVPrismaUI2*>(g_apiForCb)->Show(v);
            if (g_hudApiForCb)
                static_cast<PRISMA_HUD_API::IVPrismaHUD1*>(g_hudApiForCb)->RegisterWidget(v, kFilterDesc);
            if (g_filterDomReadyFlag) g_filterDomReadyFlag->store(true);
        });

        // Same guarded hide-until-DOM-ready as the views above.
        if (_filterView != 0) { logger->info("[HUD] Filter widget view created (handle {}).", _filterView); if (!_filterDomReady.load()) api->Hide(_filterView); }
    }
    else if (_hudApi)
    {
        static_cast<PRISMA_HUD_API::IVPrismaHUD1*>(_hudApi)->RegisterWidget(_filterView, kFilterDesc);
    }
}

void RadHud::PushToxic(float tissue, float permanentTissue)
{
    if (!_api || _toxicView == 0 || !_toxicDomReady) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!api->IsValid(_toxicView)) return;
    if (!IsPlayerInWorld()) return;

    char json[96];
    std::snprintf(json, sizeof(json), "{\"tissue\":%.1f,\"permanentTissue\":%.1f}",
        tissue, permanentTissue);
    api->InteropCall(_toxicView, "updateToxic", json);
}

void RadHud::PushMask(bool on)
{
    if (!_api || _view == 0 || !_domReady) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!api->IsValid(_view)) return;

    // No mask overlay during the pre-Pip-Boy intro / character creation (matches the vitals gate).
    if (!IsPlayerInWorld()) return;

    const int state = on ? 1 : 0;
    if (state == _maskPushed) return;   // only send on change
    _maskPushed = state;
    api->InteropCall(_view, "setMask", on ? "{\"on\":true}" : "{\"on\":false}");
}

void RadHud::PushFilter(float charge, float condition, int spares, bool masked)
{
    if (!_api) return;
    if (!IsPlayerInWorld()) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);

    char json[160];
    std::snprintf(json, sizeof(json),
        "{\"charge\":%.0f,\"condition\":%.0f,\"spares\":%d,\"masked\":%s}",
        charge, condition, spares, masked ? "true" : "false");

    // Fullscreen visor fog lives in radhud.html (_view); the positionable FILTER/MASK bar widget is
    // the standalone hud_filter.html (_filterView). Both consume the same updateFilter payload; drive
    // each independently so one not being DOM-ready doesn't starve the other.
    if (_view != 0 && _domReady && api->IsValid(_view))
        api->InteropCall(_view, "updateFilter", json);
    if (_filterView != 0 && _filterDomReady && api->IsValid(_filterView))
        api->InteropCall(_filterView, "updateFilter", json);
}

bool RadHud::ShowSpecialInfo(const std::vector<std::string>& lines)
{
    if (!_api || _view == 0 || !_domReady) return false;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!api->IsValid(_view)) return false;

    // nlohmann::json handles escaping + the array build.
    json j;
    j["lines"] = lines;
    api->InteropCall(_view, "showSpecialInfo", j.dump().c_str());
    return true;
}

void RadHud::PushStorm(int phase, int theme, float intensity, bool sheltered, bool lethal)
{
    if (!_api || _view == 0 || !_domReady) return;

    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!api->IsValid(_view)) return;

    char json[128];
    std::snprintf(json, sizeof(json),
        "{\"phase\":%d,\"theme\":%d,\"intensity\":%.3f,\"sheltered\":%s,\"lethal\":%s}",
        phase, theme, intensity, sheltered ? "true" : "false", lethal ? "true" : "false");
    api->InteropCall(_view, "stormPhase", json);
}

void RadHud::ToggleDebugMenu()
{
    // Fallback feedback: if the PrismaUI framework or the debug view isn't available, the menu can't
    // open and the hotkey would otherwise look dead. Tell the player via the vanilla HUD corner so
    // Shift+F8 always produces a visible response. (SPECIAL corruption is still auto-repaired on every
    // load by DispelNegativeSpecials, so the player isn't stranded without the menu.)
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!api || _view == 0 || !_domReady || !api->IsValid(_view))
    {
        RE::SendHUDMessage::ShowHUDMessage(
            "[Radiation] Debug menu requires the PrismaUI HUD (not loaded). SPECIAL stats are "
            "auto-repaired on each load; use RadAway/sleep to recover.", "", true, true);
        spdlog::get("FalloutAnomaly")->warn("[RadHud] ToggleDebugMenu: PrismaUI view unavailable.");
        return;
    }

    SetDebugMenu(!_debugOpen);
}

void RadHud::OnBlockingMenuOpened()
{
    // Runs on the main thread (menu event). Unfocus the debug menu so the pause menu / message box
    // becomes interactable instead of input staying captured by the focused view.
    if (_debugOpen)
        SetDebugMenu(false);
}

void RadHud::SetDebugMenu(bool open)
{
    if (!_api) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);

    _debugOpen = open;

    if (_view != 0 && _domReady && api->IsValid(_view))
    {
        // Toggle the debug + HUD-layout panel (both baked into radhud.html), then focus the
        // HUD view so its buttons AND the HUD drag receive mouse input. Unfocus on close so
        // the overlay returns to passthrough and the game regains mouse/keyboard. Never Hide
        // here — the view must keep rendering the rad bars while the menu is shut.
        api->InteropCall(_view, "SetDebugMenu", open ? "1" : "0");
        if (open)
        {
            api->Show(_view);
            // disableFocusMenu=false (default): the game FocusMenu provides the mouse cursor. This
            // menu opens during normal gameplay (no pause menu underneath), so it's the only cursor
            // source — without it the cursor is clipped/stuck and the buttons are unreachable. The
            // engine keeps ticking (not paused) so the debug-command queue still drains. Closing is
            // done with Shift+F8 (F9 is the game's Quick Load and a passive poll can't consume it;
            // F11 is the user's chat overlay; F10 is reserved by Windows and reads unreliably) or the
            // CLOSE button; Unfocus then closes the FocusMenu so the Pip-Boy opens normally again.
            api->Focus(_view);
        }
        else
        {
            api->Unfocus(_view);
        }
    }

    spdlog::get("FalloutAnomaly")->info("[HUD] Debug menu {}.", open ? "opened" : "closed");
}
