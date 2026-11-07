#include "pch.h"
#include "RadHud.h"
#include "RadiationEngine.h"
#include "Hooks.h"
#include "ClimateRuntime.h"
#include "RadNeedsTuning.h"

// RE headers BEFORE PrismaUI_F4_API.h: the API header pulls in Windows.h, whose macros clobber
// engine struct layouts (e.g. TESFile) if the RE headers are parsed afterwards.
#include <RE/A/ActorValue.h>
#include <RE/A/ACTOR_VALUE_MODIFIER.h>
#include <RE/B/BSTEvent.h>
#include <RE/M/MenuOpenCloseEvent.h>
#include <RE/M/MessageBoxMenu.h>
#include <RE/P/PauseMenu.h>
#include <RE/U/UI.h>
// For the in-world HUD gate (Pip-Boy possession + current location).
#include <RE/P/PlayerCharacter.h>
#include <RE/P/PowerArmor.h>
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

#include <algorithm>
#include <atomic>
#include <cmath>

#include <cstdio>

namespace
{
    // The view's HTML path relative to Data/PrismaUI_F4/views/.
    constexpr const char* kViewPath = "Interface/FalloutAnomaly/radhud.html";
    constexpr const char* kDebugViewPath = "Interface/FalloutAnomaly/rad_debug.html";
    // Standalone toxicity widget, registered with PrismaHUD_F4 for positioning.
    constexpr const char* kToxicViewPath = "Interface/FalloutAnomaly/hud_toxic.html";
    constexpr const char* kToxicWidgetId = "rad_toxic";
    // Standalone gas-mask FILTER/MASK bar widget (split out of radhud.html for PrismaHUD positioning).
    constexpr const char* kFilterViewPath = "Interface/FalloutAnomaly/hud_filter.html";
    constexpr const char* kFilterWidgetId = "rad_filter";
    constexpr int kHudViewOrder = 1'000'000;

    // PrismaHUD WidgetDesc default position/size are FRACTIONS of the 1920x1080 reference (value/ref),
    // NOT pixels -- the editor multiplies them by the live viewport. (The API header's "pixels" comment
    // was wrong and is corrected there now; passing raw pixels puts the editor drag box off-screen at
    // e.g. 30*1920, which is why the toxicity widget registered but never appeared usable in the editor.)
    constexpr PRISMA_HUD_API::WidgetDesc kToxicDesc{
        kToxicWidgetId, "Toxicity", 22.0f / 1920.0f, 638.0f / 1080.0f, 360.0f / 1920.0f, 98.0f / 1080.0f };
    constexpr PRISMA_HUD_API::WidgetDesc kFilterDesc{
        kFilterWidgetId, "Gas Mask Filter", 22.0f / 1920.0f, 962.0f / 1080.0f, 360.0f / 1920.0f, 98.0f / 1080.0f };

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

    float EffectiveActorValueMax(const RE::PlayerCharacter& player, const RE::ActorValueInfo& value)
    {
        return std::max(0.0f,
            player.GetPermanentActorValue(value) +
            player.GetModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary, value));
    }

    // JS -> C++ bridge for the debug menu buttons. window.radDebug("storm:0") etc.
    void OnRadDebugCommand(const char* arg)
    {
        // Constraint: close bypasses the engine command queue, which only drains inside Update(); the menu must close even when Update() is idle.
        if (arg && std::string_view(arg) == "close")
        {
            RadHud::GetSingleton().RequestDebugClose();
            return;
        }
        RadiationEngine::GetSingleton().HandleDebugCommand(arg ? arg : "");
    }

    void OnRadHudLayoutCommand(const char* arg)
    {
        std::string command = "hud:layout:";
        command += arg ? arg : "";
        RadiationEngine::GetSingleton().HandleDebugCommand(command.c_str());
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

    // Preserve a known-good provider pointer while retrying the optional companion API later.
    if (!_api)
        _api = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI2>();
    if (!_api)
    {
        _hudApi = nullptr;
        logger->info("[HUD] PrismaUI_F4 not present — radiation HUD disabled (optional).");
        return;
    }
    logger->info("[HUD] PrismaUI_F4 API acquired — radiation HUD enabled.");

    // Resolve PrismaHUD before asynchronous PrismaUI view creation begins. A fast DOM-ready callback
    // can otherwise run before _hudApi is populated and permanently miss initial widget registration
    // until a later CreateView pass. Preserve an already-acquired pointer across later lifecycle calls.
    if (!_hudApi)
        _hudApi = PRISMA_HUD_API::RequestPluginAPI();
    if (!_hudEditorApi)
        _hudEditorApi = PRISMA_HUD_API::RequestEditorAPI();
    logger->info("[HUD] PrismaHUD_F4 {} — {}.",
        _hudApi ? "API acquired" : "not present",
        _hudApi ? "toxicity widget will be positionable in the HUD editor"
                : "standalone health, stamina, and tissue HUD enabled");

    // Constraint: view creation is deferred until the world exists (main.cpp's load wait starts it).
    if (PlayerInWorld())
        StartPrismaViewRetry();
}

bool RadHud::PlayerInWorld() const
{
    // Constraint: view creation is deferred until the world exists, so no widget is built on a menu.
    return IsPlayerInWorld();
}

bool RadHud::ViewsExist() const
{
    if (!_api) return false;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    return _view != 0 && api->IsValid(_view) &&
           _toxicView != 0 && api->IsValid(_toxicView) &&
           _filterView != 0 && api->IsValid(_filterView);
}

void RadHud::CreateView()
{
    auto logger = spdlog::get("FalloutAnomaly");
    // Constraint: no view is created inside the load settle window; the retry worker and the tick re-attempt it afterwards.
    if (!_api || !LoadSettled()) return;
    _viewsReady.store(false);

    // Re-evaluate the in-world gate for each loaded game / new game.
    g_inWorld.store(false);
    // Force a mask-overlay re-sync on the next tick: a freshly (re)created view starts with the
    // overlay hidden, so the cached state must not suppress the first push after a load.
    _maskPushed = -1;
    // Constraint: a (re)created view holds no payload yet, so every last-sent cache is forgotten here.
    _toxicPayload.clear();
    _climatePayload.clear();
    _filterMainPayload.clear();
    _filterBarPayload.clear();
    _stormPayload.clear();
    _keysReplaceSent = -2;
    _keysRepairSent = -2;

    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);

    const auto setHudViewOrder = [api](PrismaView view) {
        if (view != 0 && api->IsValid(view))
            api->SetOrder(view, kHudViewOrder);
    };
    setHudViewOrder(_toxicView);
    setHudViewOrder(_filterView);

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

    _toxicWidgetRegistered.store(_hudApi != nullptr);

    if (_view == 0 || !api->IsValid(_view))
    {
        _domReady = false;

        _domLive[kDomSlotMain].store(0);
        _view = api->CreateView(kViewPath, [](PrismaView v) { RadHud::NoteDomReady(kDomSlotMain, v); });
        _domLive[kDomSlotMain].store(_view);

        // Hide-until-DOM-ready. Guarded: if the DOM-ready callback already ran (synchronous or
        // very fast), its Show must win — an unconditional Hide here would blank the HUD all session.
        if (_view != 0) { logger->info("[HUD] Radiation overlay view created (handle {}).", _view); if (!_domReady.load()) api->Hide(_view); }
        else            logger->warn("[HUD] CreateView returned invalid handle.");
    }

    // --- Toxicity widget view (hud_toxic.html), registered with PrismaHUD_F4 for positioning ---
    if (_toxicView == 0 || !api->IsValid(_toxicView))
    {
        _toxicDomReady = false;

        _domLive[kDomSlotToxic].store(0);
        _toxicView = api->CreateView(kToxicViewPath, [](PrismaView v) { RadHud::NoteDomReady(kDomSlotToxic, v); });
        _domLive[kDomSlotToxic].store(_toxicView);

        // Same guarded hide-until-DOM-ready as the main view above.
        if (_toxicView != 0) { setHudViewOrder(_toxicView); logger->info("[HUD] Toxicity widget view created (handle {}).", _toxicView); if (!_toxicDomReady.load()) api->Hide(_toxicView); }
    }
    else if (_hudApi && api->IsValid(_toxicView))
    {
        // Constraint: an existing toxic view is re-registered by the pump, never from the view-creation path.
        _widgetReapplyPending.store(true);
    }

    // --- Filter/mask bar widget view (hud_filter.html), registered with PrismaHUD_F4 for positioning ---
    if (_filterView == 0 || !api->IsValid(_filterView))
    {
        _airPayload.clear();
        _filterDomReady = false;

        _domLive[kDomSlotFilter].store(0);
        _filterView = api->CreateView(kFilterViewPath, [](PrismaView v) { RadHud::NoteDomReady(kDomSlotFilter, v); });
        _domLive[kDomSlotFilter].store(_filterView);

        // Same guarded hide-until-DOM-ready as the views above.
        if (_filterView != 0) { setHudViewOrder(_filterView); logger->info("[HUD] Filter widget view created (handle {}).", _filterView); if (!_filterDomReady.load()) api->Hide(_filterView); }
    }
    else if (_hudApi)
    {
        // Constraint: an existing filter view is re-registered by the pump, never from the view-creation path.
        _widgetReapplyPending.store(true);
    }

    EnsureClimateView();

    _viewsReady.store(_view != 0 && api->IsValid(_view) &&
                      _toxicView != 0 && api->IsValid(_toxicView) &&
                      _filterView != 0 && api->IsValid(_filterView));
    SyncLocalHudLayout();
}

// Constraint: the finalizers below run on the game thread from PumpDomReady, after the DOM-ready callback has only published the handle.
void RadHud::FinalizeMainDomReady(uint64_t view)
{
    if (!_api) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!api->IsValid(view)) return;
    api->Show(view);
    PushTheme(view);
    // Constraint: set _domReady last so a Push that sees it finds the view shown and themed.
    _domReady.store(true, std::memory_order_release);
}

void RadHud::FinalizeDebugDomReady(uint64_t view)
{
    if (!_api) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!api->IsValid(view)) return;
    api->RegisterJSListener(view, "radDebug", OnRadDebugCommand);
    // Constraint: DOM-ready pushes the closed state so the page's debug panel matches native _debugOpen (false here).
    api->InteropCall(view, "SetDebugMenu", "0");
    PushTheme(view);
    // Constraint: set _debugDomReady last so a Push that sees it finds the listener registered.
    _debugDomReady.store(true, std::memory_order_release);
    SyncLocalHudLayout();
}

void RadHud::FinalizeToxicDomReady(uint64_t view)
{
    if (!_api) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!api->IsValid(view)) return;
    api->Show(view);
    PushTheme(view);
    if (_hudApi)
    {
        static_cast<PRISMA_HUD_API::IVPrismaHUD1*>(_hudApi)->RegisterWidget(view, CompanionPlaced(kToxicDesc));
        _toxicWidgetRegistered.store(true);
    }
    _toxicDomReady.store(true, std::memory_order_release);
}

void RadHud::FinalizeFilterDomReady(uint64_t view)
{
    if (!_api) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!api->IsValid(view)) return;
    api->Show(view);
    PushTheme(view);
    if (_hudApi)
        static_cast<PRISMA_HUD_API::IVPrismaHUD1*>(_hudApi)->RegisterWidget(view, CompanionPlaced(kFilterDesc));
    _filterDomReady.store(true, std::memory_order_release);
}

void RadHud::ReapplyWidgetRegistrations()
{
    if (!_api || !_hudApi) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    auto* hud = static_cast<PRISMA_HUD_API::IVPrismaHUD1*>(_hudApi);
    // Constraint: a re-registration re-applies the saved PrismaHUD layout to an existing widget view.
    if (_toxicView != 0 && _toxicDomReady.load() && api->IsValid(_toxicView))
    {
        hud->RegisterWidget(_toxicView, CompanionPlaced(kToxicDesc));
        _toxicWidgetRegistered.store(true);
    }
    if (_filterView != 0 && _filterDomReady.load() && api->IsValid(_filterView))
        hud->RegisterWidget(_filterView, CompanionPlaced(kFilterDesc));
}

void RadHud::PushToxic(float tissue, float permanentTissue)
{
    if (!_api || !LoadSettled()) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (_view == 0 || !api->IsValid(_view) ||
        _toxicView == 0 || !api->IsValid(_toxicView) ||
        _filterView == 0 || !api->IsValid(_filterView))
    {
        // Constraint: view creation starts only once the player is in the world.
        if (IsPlayerInWorld())
        {
            StartPrismaViewRetry();
            if (!_threadApi)
                CreateView();
        }
        return;
    }
    SyncLocalHudLayout();
    if (!IsPlayerInWorld()) return;
    if (_toxicView == 0 || !_toxicDomReady || !api->IsValid(_toxicView)) return;

    const bool companion = IsCompanionMode();
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* values = RE::ActorValue::GetSingleton();
    float health = 0.0f, healthMax = 0.0f, rads = 0.0f, ap = 0.0f, apMax = 0.0f;
    const bool fallback = !companion && !_toxicWidgetRegistered.load();
    if (!companion && player && values)
    {
        if (values->health)
        {
            health = player->GetActorValue(*values->health);
            healthMax = EffectiveActorValueMax(*player, *values->health);
            if (healthMax <= 0.0f && health > 0.0f) healthMax = health;
        }
        if (values->rads)
            rads = std::clamp(player->GetActorValue(*values->rads), 0.0f, 1000.0f);
        if (values->actionPoints)
        {
            ap = player->GetActorValue(*values->actionPoints);
            apMax = EffectiveActorValueMax(*player, *values->actionPoints);
            if (apMax <= 0.0f && ap > 0.0f) apMax = ap;
        }
    }

    char json[256];
    std::snprintf(json, sizeof(json),
        "{\"tissue\":%.1f,\"permanentTissue\":%.1f,\"fallback\":%s,"
        "\"health\":%.1f,\"healthMax\":%.1f,\"rads\":%.1f,\"radsMax\":1000.0,"
        "\"ap\":%.1f,\"apMax\":%.1f}",
        tissue, permanentTissue, fallback ? "true" : "false", health, healthMax, rads, ap, apMax);
    // Constraint: send only on change; the same payload feeds the debug menu's vitals view.
    if (_toxicPayload == json) return;
    _toxicPayload = json;
    api->InteropCall(_toxicView, "updateToxic", json);
    if (_debugOpen && _debugView && _debugDomReady && api->IsValid(_debugView))
        api->InteropCall(_debugView, "updateVitalsInfo", json);
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
    // Constraint: each view tracks its own last-sent payload so a late DOM-ready view still gets data.
    if (_view != 0 && _domReady && api->IsValid(_view) && _filterMainPayload != json)
    {
        _filterMainPayload = json;
        api->InteropCall(_view, "updateFilter", json);
    }
    if (_filterView != 0 && _filterDomReady && api->IsValid(_filterView))
    {
        if (_filterBarPayload != json)
        {
            _filterBarPayload = json;
            api->InteropCall(_filterView, "updateFilter", json);
        }
        PushFilterKeys(_filterView);
    }
}

void RadHud::PushAir(const std::string& payload)
{
    if (!_api || !IsPlayerInWorld()) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!_filterView || !_filterDomReady || !api->IsValid(_filterView)) return;
    if (payload == _airPayload) return;
    _airPayload = payload;
    api->InteropCall(_filterView, "updateAir", payload.c_str());
    if (_debugOpen && _debugView && _debugDomReady && api->IsValid(_debugView))
        api->InteropCall(_debugView, "updateAirInfo", payload.c_str());
}

void RadHud::PushGhoulSettings(bool enabled, float chance)
{
    if (!_api || _debugView == 0 || !_debugDomReady) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!api->IsValid(_debugView)) return;

    char json[96];
    std::snprintf(json, sizeof(json), "{\"enabled\":%s,\"chance\":%.0f}",
        enabled ? "true" : "false", chance * 100.0f);
    api->InteropCall(_debugView, "setGhoulSettings", json);
}

void RadHud::PushNeedsTuning(const std::string& json)
{
    if (!_api || _debugView == 0 || !_debugDomReady) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!api->IsValid(_debugView)) return;
    api->InteropCall(_debugView, "setNeedsTuning", json.c_str());
}

bool RadHud::ShowSpecialInfo(const std::vector<std::string>& lines)
{
    if (!_api || _debugView == 0 || !_debugDomReady) return false;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!api->IsValid(_debugView)) return false;

    // nlohmann::json handles escaping + the array build.
    json j;
    j["lines"] = lines;
    // Constraint: a mod-supplied string can carry invalid UTF-8; replace it instead of throwing.
    api->InteropCall(_debugView, "showSpecialInfo", j.dump(-1, ' ', false, json::error_handler_t::replace).c_str());
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
    if (_stormPayload == json) return;
    _stormPayload = json;
    api->InteropCall(_view, "stormPhase", json);
}

void RadHud::PushArmor(
    float insulation01,
    float effectiveInsulation01,
    bool powerArmor,
    const std::vector<ClimateProtectionRuntime::ArmorInfo>& armor)
{
    if (!_api || !_debugOpen || _debugView == 0 || !_debugDomReady)
        return;

    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!api->IsValid(_debugView))
        return;

    const auto percent = [](float value) {
        if (!std::isfinite(value)) return 0;
        return static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) * 100.0f));
    };

    json payload{
        { "insulation", percent(insulation01) },
        { "effectiveInsulation", percent(effectiveInsulation01) },
        { "powerArmor", powerArmor },
        { "inPowerArmor", RE::PowerArmor::PlayerInPowerArmor() },
        { "pieces", json::array() }
    };
    for (const auto& piece : armor)
    {
        payload["pieces"].push_back({
            { "formID", piece.formID },
            { "name", RadCorrectnessPolicy::SanitizeUiText(piece.name) },
            { "warmth", percent(piece.warmth01) },
            { "insulation", percent(piece.insulation01) },
            { "override", piece.overridden },
            { "head", piece.head },
            { "torso", piece.torso },
            { "leftArm", piece.leftArm },
            { "rightArm", piece.rightArm },
            { "leftLeg", piece.leftLeg },
            { "rightLeg", piece.rightLeg }
        });
    }

    const std::string serialized = payload.dump(-1, ' ', false, json::error_handler_t::replace);
    if (serialized == _armorPayload)
        return;
    _armorPayload = serialized;
    api->InteropCall(_debugView, "setArmorInfo", _armorPayload.c_str());
}

void RadHud::ResetDebugMenuForSession()
{
    g_inWorld.store(false);
    RadiationEngine::GetSingleton().ResetDebugKeyBaseline();
    _debugOpen = false;
    _debugCloseRequested.store(false);
    _debugTogglePending = false;
    _debugDesiredOpen = false;
    _debugNextFocusRetryMs = 0;
    SetLocalHudEditorPreview(false);
    SetDebugMenu(false);
    HideNeeds();
}

void RadHud::HideNeeds()
{
    _needsPayload.clear();
    if (_api && _needsView)
    {
        auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
        if (api->IsValid(_needsView)) api->Hide(_needsView);
    }
}

void RadHud::ToggleDebugMenu()
{
    auto logger = spdlog::get("FalloutAnomaly");
    static ULONGLONG lastToggle = 0; const auto now = ::GetTickCount64(); if (now - lastToggle < 100) return; lastToggle = now;

    // Constraint: only a missing PrismaUI (_api == nullptr) reaches the fallback message.
    if (!_api)
    {
        RE::SendHUDMessage::ShowHUDMessage(
            "[Radiation] Debug menu requires the PrismaUI HUD (not installed). SPECIAL stats are "
            "auto-repaired on each load; use RadAway/sleep to recover.", "", true, true);
        if (logger) logger->warn("[RadHud] ToggleDebugMenu: PrismaUI framework not present.");
        return;
    }

    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);

    if (_debugView != 0 && api->IsValid(_debugView) && _debugDomReady.load())
    {
        if (_debugTogglePending && _debugDesiredOpen)
        {
            _debugTogglePending = false;
            _debugDesiredOpen = false;
            _debugNextFocusRetryMs = 0;
            SetDebugMenu(false);
            if (logger) logger->info("[RadHud] Pending debug-menu open cancelled.");
            return;
        }
        _debugTogglePending = false;
        _debugNextFocusRetryMs = 0;
        SetDebugMenu(!_debugOpen);
        return;
    }

    // Constraint: a press made before the view is ready is deferred to PumpDeferredDebug, never dropped; only PumpDeferredDebug creates the view.
    const bool nextDesiredOpen = _debugTogglePending ? !_debugDesiredOpen : !_debugOpen;
    if (_debugTogglePending && !nextDesiredOpen)
    {
        _debugTogglePending = false;
        _debugDesiredOpen = false;
        _debugNextFocusRetryMs = 0;
        SetDebugMenu(false);
        if (logger) logger->info("[RadHud] Pending debug-menu open cancelled before view readiness.");
        return;
    }
    _debugDesiredOpen   = nextDesiredOpen;
    _debugTogglePending = true;
    if (logger)
        logger->info("[RadHud] Debug-menu toggle deferred until view ready (view={}, domReady={}); desired={}.",
            _debugView, _debugDomReady.load(), _debugDesiredOpen ? "open" : "closed");
}

void RadHud::RequestDebugClose() noexcept
{
    _debugCloseRequested.store(true);
    // Constraint: the close also runs as a game-thread task now, so focus/Escape release does not wait on PollInput.
    try
    {
        Hooks::QueueMainThreadTask([]() { RadHud::GetSingleton().PumpDeferredDebug(); });
    }
    catch (...)
    {
    }
}

void RadHud::PumpDeferredDebug()
{
    // Constraint: game-main thread only; it reads _debugDomReady, which only FinalizeDebugDomReady sets true.
    if (_debugCloseRequested.exchange(false))
    {
        _debugTogglePending = false;
        _debugDesiredOpen = false;
        _debugNextFocusRetryMs = 0;
        SetDebugMenu(false);
        return;
    }
    if (!_debugTogglePending) return;
    if (!_api) { _debugTogglePending = false; return; }   // framework went away — drop the request

    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (_debugDesiredOpen) EnsureDebugView();
    if (_debugView == 0 || !api->IsValid(_debugView) || !_debugDomReady.load())
        return;                                           // still not ready — keep the request pending

    const auto now = ::GetTickCount64();
    if (_debugDesiredOpen && now < _debugNextFocusRetryMs)
        return;

    SetDebugMenu(_debugDesiredOpen);
    if (_debugOpen == _debugDesiredOpen)
    {
        _debugTogglePending = false;
        _debugNextFocusRetryMs = 0;
        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->info("[RadHud] Applied deferred debug-menu toggle: {}.", _debugOpen ? "opened" : "closed");
    }
}

void RadHud::OnBlockingMenuOpened()
{
    _debugTogglePending = false;
    _debugDesiredOpen = false;
    _debugNextFocusRetryMs = 0;
    if (_debugOpen || _debugView != 0)
        SetDebugMenu(false);
}

namespace
{
    // Constraint: Escape reaches the view's JS only while the view owns it (PrismaUI V9); without V9 Escape stays with the game.
    void SetDebugViewOwnsEscape(uint64_t view, bool owns)
    {
        if (auto* escape = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI9>())
            escape->SetViewOwnsEscape(view, owns);
    }

    // Constraint: the debug view is kPanel only while its menu is open; an always-on kPanel makes IsAnyPanelVisible() true for other plugins.
    constexpr ULONGLONG kDebugFocusRetryMs = 150;
    constexpr ULONGLONG kDebugFocusGiveUpMs = 2000;

    void SetDebugViewRole(uint64_t view, PRISMA_UI_API::ViewRole role)
    {
        if (auto* roles = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI10>())
            roles->SetViewRole(view, role);
    }

    // Constraint: FocusOverlay is tried only after a plain Focus is rejected; a missing V10 interface simply returns false.
    bool FocusDebugOverlayFallback(uint64_t view)
    {
        auto* overlay = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI10>();
        return overlay != nullptr && overlay->FocusOverlay(view, false, false);
    }
}

void RadHud::EnsureDebugView()
{
    if (!_api) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (_debugView != 0 && api->IsValid(_debugView)) return;
    // Constraint: the debug view is created only for a user open, after the load settle and with the player in the world.
    if (!LoadSettled() || !PlayerInWorld()) return;

    _debugDomReady = false;
    _debugPushedOpen = -1;
    // Constraint: a new debug page holds no payload, so every debug-only cache is forgotten here.
    _armorPayload.clear();
    _survivalPayload.clear();
    _domLive[kDomSlotDebug].store(0);
    _debugView = api->CreateView(kDebugViewPath, [](PrismaView v) { RadHud::NoteDomReady(kDomSlotDebug, v); });
    _domLive[kDomSlotDebug].store(_debugView);
    if (_debugView == 0)
    {
        _debugTogglePending = false;
        _debugDesiredOpen = false;
        return;
    }
    if (!_debugDomReady.load()) api->Hide(_debugView);
}

void RadHud::DestroyDebugView()
{
    // Constraint: restores frozen debug time before the early return, so every close route runs it.
    RadiationEngine::GetSingleton().ResumeDebugTime();
    if (_debugView == 0) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    // Constraint: the built-in layout editor lives in the debug view, so it is closed before the view is destroyed.
    SetLocalHudEditorPreview(false);
    api->Unfocus(_debugView);
    SetDebugViewOwnsEscape(_debugView, false);
    SetDebugViewRole(_debugView, PRISMA_UI_API::ViewRole::kWidget);
    _domLive[kDomSlotDebug].store(0);
    api->Destroy(_debugView);
    _debugView = 0;
    _debugDomReady = false;
    _debugPushedOpen = -1;
    InvalidateLocalHudLayoutViews();
}

void RadHud::ReleaseStaleDebugFocus() noexcept
{
    // Constraint: the 4 Hz engine tick is the backstop; it only releases focus the debug view holds while the menu is closed.
    if (_debugOpen || _api == nullptr || _debugView == 0) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!api->IsValid(_debugView) || !api->HasFocus(_debugView)) return;
    api->Unfocus(_debugView);
    SetDebugViewOwnsEscape(_debugView, false);
}

void RadHud::PushDebugPanelState(bool open) noexcept
{
    // Constraint: every native debug-state change reaches the page through this helper, so the page's panel cannot drift from _debugOpen.
    if (_api == nullptr || _debugView == 0 || !_debugDomReady.load()) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!api->IsValid(_debugView)) return;
    api->InteropCall(_debugView, "SetDebugMenu", open ? "1" : "0");
    _debugPushedOpen = open ? 1 : 0;
}

void RadHud::SyncDebugPanelState() noexcept
{
    // Constraint: an unchanged state costs one integer compare, so the 4 Hz tick never re-sends the same state.
    if (_debugPushedOpen == (_debugOpen ? 1 : 0)) return;
    PushDebugPanelState(_debugOpen);
}

void RadHud::SetDebugMenu(bool open)
{
    if (!_api) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);

    if (!open)
    {
        _debugOpen = false;
        _debugTogglePending = false;
        _debugDesiredOpen = false;
        _debugNextFocusRetryMs = 0;
        _debugFocusFailedSinceMs = 0;
        PushDebugPanelState(false);
        DestroyDebugView();
    }
    else
    {
        // Constraint: without a DOM-ready view there is no panel to show, so an open request is dropped rather than latched.
        if (_debugView == 0 || !_debugDomReady.load() || !api->IsValid(_debugView)) return;
        if (!_climatePayload.empty()) api->InteropCall(_debugView, "updateClimateInfo", _climatePayload.c_str());
        if (!_airPayload.empty()) api->InteropCall(_debugView, "updateAirInfo", _airPayload.c_str());
        if (!_toxicPayload.empty()) api->InteropCall(_debugView, "updateVitalsInfo", _toxicPayload.c_str());
        auto& climate = ClimateRuntime::GetSingleton();
        PushArmor(climate.LastInsulation01(), climate.LastEffectiveInsulation01(),
            climate.LastPowerArmor(), climate.LastArmor());
        api->Show(_debugView);
        bool focusAccepted = api->Focus(_debugView, false, false);
        if (!focusAccepted) focusAccepted = FocusDebugOverlayFallback(_debugView);
        if (!focusAccepted)
        {
            _debugOpen = false;
            PushDebugPanelState(false);
            api->Unfocus(_debugView);
            SetDebugViewOwnsEscape(_debugView, false);
            SetDebugViewRole(_debugView, PRISMA_UI_API::ViewRole::kWidget);

            const auto now = ::GetTickCount64();
            if (_debugNextFocusRetryMs == 0 || _debugFocusFailedSinceMs == 0)
                _debugFocusFailedSinceMs = now;
            // Constraint: focus rejection gives up after kDebugFocusGiveUpMs, clears the pending open, destroys the view, and shows the player one message.
            if (now - _debugFocusFailedSinceMs >= kDebugFocusGiveUpMs)
            {
                _debugDesiredOpen = false;
                _debugTogglePending = false;
                _debugNextFocusRetryMs = 0;
                _debugFocusFailedSinceMs = 0;
                DestroyDebugView();
                RE::SendHUDMessage::ShowHUDMessage(
                    "[Radiation] Debug menu could not take focus. Press Shift+F8 to try again.", "", true, true);
                return;
            }
            _debugDesiredOpen = true;
            _debugTogglePending = true;
            _debugNextFocusRetryMs = now + kDebugFocusRetryMs;

            static ULONGLONG lastFocusFailureLog = 0;
            if (now - lastFocusFailureLog >= 1000)
            {
                lastFocusFailureLog = now;
                if (auto logger = spdlog::get("FalloutAnomaly"))
                    logger->warn("[RadHud] Debug-menu focus was rejected; retrying while the view settles.");
            }
            return;
        }

        _debugOpen = true;
        _debugTogglePending = false;
        _debugNextFocusRetryMs = 0;
        _debugFocusFailedSinceMs = 0;
        SetDebugViewOwnsEscape(_debugView, true);
        SetDebugViewRole(_debugView, PRISMA_UI_API::ViewRole::kPanel);
        PushDebugPanelState(true);
        RadiationEngine::GetSingleton().PushGhoulSettingsToHud();
        PushNeedsTuning(RadNeedsTuning::Json());
    }

    spdlog::get("FalloutAnomaly")->info("[HUD] Debug menu {}.", open ? "opened" : "closed");
}
