#include "pch.h"
#include "RadHud.h"
#include "RadiationEngine.h"
#include "ClimateRuntime.h"

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

    // OnDomReady callbacks may run on PrismaUI's web thread while CreateView/recovery runs on the
    // verified game thread. The bridge pointers themselves therefore need synchronization, not only
    // the bools they point at. Atomic pointer publication keeps late/recovered callbacks defined.
    std::atomic<std::atomic<bool>*> g_domReadyFlag{ nullptr };
    std::atomic<PRISMA_UI_API::IVPrismaUI2*> g_apiForCb{ nullptr };
    std::atomic<std::atomic<bool>*> g_toxicDomReadyFlag{ nullptr };
    std::atomic<PRISMA_HUD_API::IVPrismaHUD1*> g_hudApiForCb{ nullptr };
    std::atomic<std::atomic<bool>*> g_toxicWidgetRegisteredFlag{ nullptr };
    std::atomic<std::atomic<bool>*> g_filterDomReadyFlag{ nullptr };

    float EffectiveActorValueMax(const RE::PlayerCharacter& player, const RE::ActorValueInfo& value)
    {
        return std::max(0.0f,
            player.GetPermanentActorValue(value) +
            player.GetModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary, value));
    }

    // JS -> C++ bridge for the debug menu buttons. window.radDebug("storm:0") etc.
    void OnRadDebugCommand(const char* arg)
    {
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
    logger->info("[HUD] PrismaHUD_F4 {} — {}.",
        _hudApi ? "API acquired" : "not present",
        _hudApi ? "toxicity widget will be positionable in the HUD editor"
                : "standalone health, stamina, and tissue HUD enabled");

    StartPrismaViewRetry();
}

void RadHud::CreateView()
{
    auto logger = spdlog::get("FalloutAnomaly");
    _viewsReady.store(false);
    if (!_api) return;

    // Re-evaluate the in-world gate for each loaded game / new game.
    g_inWorld.store(false);
    // Force a mask-overlay re-sync on the next tick: a freshly (re)created view starts with the
    // overlay hidden, so the cached state must not suppress the first push after a load.
    _maskPushed = -1;

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

    // Publish callback state before creating views. These pointers are read from PrismaUI's web thread.
    g_apiForCb.store(api, std::memory_order_release);
    g_hudApiForCb.store(static_cast<PRISMA_HUD_API::IVPrismaHUD1*>(_hudApi), std::memory_order_release);
    _toxicWidgetRegistered.store(false);
    g_toxicWidgetRegisteredFlag.store(&_toxicWidgetRegistered, std::memory_order_release);

    // --- Main overlay view (radhud.html: gas-mask overlay, filter, storm banners, popup, debug) ---
    if (_view == 0 || !api->IsValid(_view))
    {
        _domReady = false;
        g_domReadyFlag.store(&_domReady, std::memory_order_release);

        _view = api->CreateView(kViewPath, [](PrismaView v) {
            if (auto* a = g_apiForCb.load(std::memory_order_acquire))
            {
                a->RegisterJSListener(v, "radDebug", OnRadDebugCommand);
                a->RegisterJSListener(v, "radHudLayout", OnRadHudLayoutCommand);
                a->Show(v);
            }
            // Flag LAST: a Push seeing _domReady=true must find the listener already registered.
            if (auto* flag = g_domReadyFlag.load(std::memory_order_acquire))
                flag->store(true, std::memory_order_release);
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
        g_toxicDomReadyFlag.store(&_toxicDomReady, std::memory_order_release);

        _toxicView = api->CreateView(kToxicViewPath, [](PrismaView v) {
            auto* a = g_apiForCb.load(std::memory_order_acquire);
            auto* hudApi = g_hudApiForCb.load(std::memory_order_acquire);
            if (a) a->Show(v);
            if (hudApi && a && a->IsValid(v))
            {
                hudApi->RegisterWidget(v, kToxicDesc);
                if (auto* flag = g_toxicWidgetRegisteredFlag.load(std::memory_order_acquire))
                    flag->store(true, std::memory_order_release);
            }
            if (auto* flag = g_toxicDomReadyFlag.load(std::memory_order_acquire))
                flag->store(true, std::memory_order_release);
        });

        // Same guarded hide-until-DOM-ready as the main view above.
        if (_toxicView != 0) { setHudViewOrder(_toxicView); logger->info("[HUD] Toxicity widget view created (handle {}).", _toxicView); if (!_toxicDomReady.load()) api->Hide(_toxicView); }
    }
    else if (_hudApi && api->IsValid(_toxicView))
    {
        // Existing toxic view on reload: re-register so PrismaHUD re-applies the saved layout.
        static_cast<PRISMA_HUD_API::IVPrismaHUD1*>(_hudApi)->RegisterWidget(_toxicView, kToxicDesc);
        _toxicWidgetRegistered.store(true);
    }

    // --- Filter/mask bar widget view (hud_filter.html), registered with PrismaHUD_F4 for positioning ---
    if (_filterView == 0 || !api->IsValid(_filterView))
    {
        _filterDomReady = false;
        g_filterDomReadyFlag.store(&_filterDomReady, std::memory_order_release);

        _filterView = api->CreateView(kFilterViewPath, [](PrismaView v) {
            auto* a = g_apiForCb.load(std::memory_order_acquire);
            auto* hudApi = g_hudApiForCb.load(std::memory_order_acquire);
            if (a) a->Show(v);
            if (hudApi && a && a->IsValid(v))
                hudApi->RegisterWidget(v, kFilterDesc);
            if (auto* flag = g_filterDomReadyFlag.load(std::memory_order_acquire))
                flag->store(true, std::memory_order_release);
        });

        // Same guarded hide-until-DOM-ready as the views above.
        if (_filterView != 0) { setHudViewOrder(_filterView); logger->info("[HUD] Filter widget view created (handle {}).", _filterView); if (!_filterDomReady.load()) api->Hide(_filterView); }
    }
    else if (_hudApi)
    {
        static_cast<PRISMA_HUD_API::IVPrismaHUD1*>(_hudApi)->RegisterWidget(_filterView, kFilterDesc);
    }

    EnsureClimateView();

    LoadLocalHudLayout();
    _localLayoutApplied = false;
    ApplyLocalHudLayout(false);

    _viewsReady.store(_view != 0 && api->IsValid(_view) &&
                      _toxicView != 0 && api->IsValid(_toxicView) &&
                      _filterView != 0 && api->IsValid(_filterView));
}

void RadHud::PushToxic(float tissue, float permanentTissue)
{
    if (!_api) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!_hudApi && !_localLayoutApplied) ApplyLocalHudLayout(false);
    if (_view == 0 || !api->IsValid(_view) ||
        _toxicView == 0 || !api->IsValid(_toxicView) ||
        _filterView == 0 || !api->IsValid(_filterView))
        CreateView();
    if (!IsPlayerInWorld()) return;
    if (_toxicView == 0 || !_toxicDomReady || !api->IsValid(_toxicView)) return;

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* values = RE::ActorValue::GetSingleton();
    float health = 0.0f, healthMax = 0.0f, rads = 0.0f, ap = 0.0f, apMax = 0.0f;
    const bool fallback = !_toxicWidgetRegistered.load();
    if (fallback && player && values)
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

void RadHud::PushGhoulSettings(bool enabled, int chancePercent)
{
    if (!_api || _view == 0 || !_domReady) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!api->IsValid(_view)) return;

    char json[96];
    std::snprintf(json, sizeof(json), "{\"enabled\":%s,\"chance\":%d}",
        enabled ? "true" : "false", std::clamp(chancePercent, 0, 100));
    api->InteropCall(_view, "setGhoulSettings", json);
}

void RadHud::PushArmor(
    float insulation01,
    float effectiveInsulation01,
    bool powerArmor,
    const std::vector<ClimateProtectionRuntime::ArmorInfo>& armor)
{
    if (!_api || !_debugOpen || _view == 0 || !_domReady)
        return;

    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!api->IsValid(_view))
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
            { "name", piece.name },
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

    const std::string serialized = payload.dump();
    if (serialized == _armorPayload)
        return;
    _armorPayload = serialized;
    api->InteropCall(_view, "setArmorInfo", _armorPayload.c_str());
}

std::string RadHud::LocalHudLayoutJson() const
{
    json out;
    const auto add = [&out](const char* id, const LocalLayout& layout) {
        out[id] = { { "x", layout.x }, { "y", layout.y }, { "s", layout.s },
                    { "r", layout.r }, { "hidden", layout.hidden } };
    };
    add("rad_toxic", _toxicLayout);
    add("rad_filter", _filterLayout);
    add("rad_climate", _climateLayout);
    out["temperatureUnit"] = _climateFahrenheit ? "F" : "C";
    return out.dump();
}

void RadHud::LoadLocalHudLayout()
{
    if (_localLayoutLoaded) return;
    _localLayoutLoaded = true;
    std::ifstream file(".\\Data\\F4SE\\Plugins\\FalloutAnomaly\\hud-layout.json");
    if (!file.is_open()) return;
    const json data = json::parse(file, nullptr, false);
    if (!data.is_object()) return;
    const auto load = [&data](const char* id, LocalLayout& layout) {
        const auto it = data.find(id);
        if (it == data.end() || !it->is_object()) return;
        const auto read = [&it](const char* key, float& value, float lo, float hi) {
            const auto field = it->find(key);
            if (field != it->end() && field->is_number() && std::isfinite(field->get<double>()))
                value = std::clamp(static_cast<float>(field->get<double>()), lo, hi);
        };
        read("x", layout.x, 0.0f, 0.95f);
        read("y", layout.y, 0.0f, 0.95f);
        read("s", layout.s, 0.5f, 2.5f);
        read("r", layout.r, -180.0f, 180.0f);
        const auto hidden = it->find("hidden");
        if (hidden != it->end() && hidden->is_boolean()) layout.hidden = hidden->get<bool>();
    };
    load("rad_toxic", _toxicLayout);
    load("rad_filter", _filterLayout);
    load("rad_climate", _climateLayout);
    const auto unit = data.find("temperatureUnit");
    if (unit != data.end() && unit->is_string()) _climateFahrenheit = unit->get<std::string>() == "F";
}

bool RadHud::SaveLocalHudLayout()
{
    const std::string payload = LocalHudLayoutJson();
    std::ofstream file(".\\Data\\F4SE\\Plugins\\FalloutAnomaly\\hud-layout.json.tmp", std::ios::trunc);
    if (!file.is_open()) return false;
    file << payload;
    file.flush();
    if (!file.good()) return false;
    file.close();
    return ::MoveFileExW(L".\\Data\\F4SE\\Plugins\\FalloutAnomaly\\hud-layout.json.tmp",
                         L".\\Data\\F4SE\\Plugins\\FalloutAnomaly\\hud-layout.json",
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

void RadHud::ApplyLocalHudLayout(bool preview)
{
    if (!_api) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (_climateView != 0 && _climateDomReady && api->IsValid(_climateView))
        api->InteropCall(_climateView, "setClimateUnit", _climateFahrenheit ? "\"F\"" : "\"C\"");
    if (_hudApi) return;
    const auto apply = [api, preview](uint64_t view, bool ready, const LocalLayout& layout) {
        if (!ready || view == 0 || !api->IsValid(view)) return;
        char payload[160];
        std::snprintf(payload, sizeof(payload),
            "{\"x\":%.6f,\"y\":%.6f,\"s\":%.3f,\"r\":%.1f,\"hidden\":%s}",
            layout.x, layout.y, layout.s, layout.r, layout.hidden ? "true" : "false");
        api->InteropCall(view, "setLocalHUDLayout", payload);
        api->InteropCall(view, "setEditorPreview", preview ? "1" : "0");
    };
    const bool toxicApplied = _toxicView != 0 && _toxicDomReady && api->IsValid(_toxicView);
    const bool filterApplied = _filterView != 0 && _filterDomReady && api->IsValid(_filterView);
    if (toxicApplied) apply(_toxicView, true, _toxicLayout);
    if (filterApplied) apply(_filterView, true, _filterLayout);
    const bool climateApplied = _climateView != 0 && _climateDomReady && api->IsValid(_climateView);
    if (climateApplied) apply(_climateView, true, _climateLayout);
    _localLayoutApplied = toxicApplied && filterApplied && climateApplied;
}

void RadHud::HandleHudLayoutCommand(const std::string& command)
{
    LoadLocalHudLayout();
    const json data = json::parse(command, nullptr, false);
    if (!data.is_object()) return;
    const auto actionIt = data.find("action");
    if (actionIt == data.end() || !actionIt->is_string()) return;
    const std::string action = actionIt->get<std::string>();
    if (action == "unit")
    {
        const auto unit = data.find("unit");
        if (unit == data.end() || !unit->is_string()) return;
        const std::string value = unit->get<std::string>();
        if (value != "C" && value != "F") return;
        _climateFahrenheit = value == "F";
        ApplyLocalHudLayout(true);
        const auto save = data.find("save");
        if (save != data.end() && save->is_boolean() && save->get<bool>()) {
            const bool saved = SaveLocalHudLayout();
            if (_view != 0 && _domReady) {
                auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
                if (api->IsValid(_view)) api->InteropCall(_view, "setHudLayoutStatus", saved ? "1" : "0");
            }
        }
        return;
    }
    if (action == "preview")
    {
        if (data.contains("on") && data["on"].is_boolean()) ApplyLocalHudLayout(data["on"].get<bool>());
        return;
    }
    if (action != "update") return;
    const auto widgetIt = data.find("widget");
    if (widgetIt == data.end() || !widgetIt->is_string()) return;
    const std::string widget = widgetIt->get<std::string>();
    LocalLayout* layout = widget == "rad_toxic" ? &_toxicLayout :
                          widget == "rad_filter" ? &_filterLayout :
                          widget == "rad_climate" ? &_climateLayout : nullptr;
    if (!layout) return;
    const auto number = [&data](const char* key, float lo, float hi, float& out) {
        const auto it = data.find(key);
        if (it == data.end() || !it->is_number() || !std::isfinite(it->get<double>())) return false;
        out = std::clamp(static_cast<float>(it->get<double>()), lo, hi);
        return true;
    };
    if (!number("x", 0.0f, 0.95f, layout->x) || !number("y", 0.0f, 0.95f, layout->y) ||
        !number("s", 0.5f, 2.5f, layout->s) || !number("r", -180.0f, 180.0f, layout->r) ||
        !data.contains("hidden") || !data["hidden"].is_boolean()) return;
    layout->hidden = data["hidden"].get<bool>();
    ApplyLocalHudLayout(true);
    const auto save = data.find("save");
    if (save != data.end() && save->is_boolean() && save->get<bool>())
    {
        const bool saved = SaveLocalHudLayout();
        if (_view != 0 && _domReady) {
            auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
            if (api->IsValid(_view)) api->InteropCall(_view, "setHudLayoutStatus", saved ? "1" : "0");
        }
    }
}

void RadHud::OpenHudEditor()
{
    if (!_api || _view == 0 || !_domReady || _hudApi) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!api->IsValid(_view)) return;
    LoadLocalHudLayout();
    api->InteropCall(_view, "OpenLocalHudEditor", LocalHudLayoutJson().c_str());
    ApplyLocalHudLayout(true);
}

void RadHud::ResetDebugMenuForSession()
{
    _debugOpen = false;
    _debugTogglePending = false;
    _debugDesiredOpen = false;
    _armorPayload.clear();
    ApplyLocalHudLayout(false);
}

void RadHud::ToggleDebugMenu()
{
    auto logger = spdlog::get("FalloutAnomaly");
    static ULONGLONG lastToggle = 0; const auto now = ::GetTickCount64(); if (now - lastToggle < 100) return; lastToggle = now;

    // Distinguish the three states so a still-initializing view is never mistaken for "not installed":
    //   1. PrismaUI genuinely absent (_api == nullptr)  -> vanilla-HUD fallback message.
    //   2. view created + DOM ready                      -> toggle now.
    //   3. view not created yet, OR created but DOM still loading -> DEFER (do not discard the press).

    // Case 1: framework not present. This is the ONLY case that shows the fallback — the old code also
    // fired it while _domReady was still false, which is exactly why the first Shift+F8 was "lost".
    if (!_api)
    {
        RE::SendHUDMessage::ShowHUDMessage(
            "[Radiation] Debug menu requires the PrismaUI HUD (not installed). SPECIAL stats are "
            "auto-repaired on each load; use RadAway/sleep to recover.", "", true, true);
        if (logger) logger->warn("[RadHud] ToggleDebugMenu: PrismaUI framework not present.");
        return;
    }

    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);

    // Case 2: ready now — apply immediately and clear any stale deferred request.
    if (_view != 0 && api->IsValid(_view) && _domReady.load())
    {
        _debugTogglePending = false;
        SetDebugMenu(!_debugOpen);
        return;
    }

    // Case 3: view still coming up. Stash the desired final state and let PumpDeferredDebug apply it
    // once the DOM is ready — the press is retained, not dropped. Last press wins (a second press
    // before the view is ready cancels the first). We do NOT create the view here — CreateView owns
    // that behind the world/load readiness gate.
    _debugDesiredOpen   = _debugTogglePending ? !_debugDesiredOpen : !_debugOpen;
    _debugTogglePending = true;
    if (logger)
        logger->info("[RadHud] Debug-menu toggle deferred until view ready (view={}, domReady={}); desired={}.",
            _view, _domReady.load(), _debugDesiredOpen ? "open" : "closed");
}

void RadHud::PumpDeferredDebug()
{
    // Called every frame from PollInput on the verified game-main thread. Applies a toggle that arrived
    // before the view finished loading. All state here is main-thread-only; the cross-thread readiness
    // signal (_domReady) is atomic, so there is no race with the DOM-ready callback.
    if (!_debugTogglePending) return;
    if (!_api) { _debugTogglePending = false; return; }   // framework went away — drop the request

    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (_view == 0 || !api->IsValid(_view) || !_domReady.load())
        return;                                           // still not ready — keep the request pending

    _debugTogglePending = false;
    SetDebugMenu(_debugDesiredOpen);
    if (auto logger = spdlog::get("FalloutAnomaly"))
        logger->info("[RadHud] Applied deferred debug-menu toggle: {}.", _debugDesiredOpen ? "opened" : "closed");
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
            auto& engine = RadiationEngine::GetSingleton();
            PushGhoulSettings(engine.StormGhoulsEnabled(), engine.StormGhoulChancePercent());
            auto& climate = ClimateRuntime::GetSingleton();
            PushArmor(climate.LastInsulation01(), climate.LastEffectiveInsulation01(),
                climate.LastPowerArmor(), climate.LastArmor());
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
            _armorPayload.clear();
            ApplyLocalHudLayout(false);
        }
    }

    spdlog::get("FalloutAnomaly")->info("[HUD] Debug menu {}.", open ? "opened" : "closed");
}
