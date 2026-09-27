#include "pch.h"
#include "RadHud.h"
#include "RadiationEngine.h"
#include "PrismaHUD_F4_API.h"
#include "PrismaUI_F4_API.h"

#include <Windows.h>

namespace
{
    struct WidgetLayout
    {
        float x;
        float y;
        float scale;
        float rotation;
        bool hidden;
    };

    struct LayoutState
    {
        WidgetLayout toxic{ 30.0f / 1920.0f, 882.0f / 1080.0f, 1.0f, 0.0f, false };
        WidgetLayout filter{ 22.0f / 1920.0f, 1014.0f / 1080.0f, 1.0f, 0.0f, false };
        WidgetLayout needs{ 22.0f / 1920.0f, 812.0f / 1080.0f, 1.0f, 0.0f, false };
        WidgetLayout climate{ 22.0f / 1920.0f, 944.0f / 1080.0f, 1.0f, 0.0f, false };
        bool fahrenheit{ false };
        bool loaded{ false };
        bool preview{ false };
        std::uint64_t generation{ 1 };
        std::uint64_t toxicAppliedGeneration{ 0 };
        std::uint64_t filterAppliedGeneration{ 0 };
        std::uint64_t climateAppliedGeneration{ 0 };
        std::uint64_t needsAppliedGeneration{ 0 };
        PrismaView toxicAppliedView{ 0 };
        PrismaView filterAppliedView{ 0 };
        PrismaView climateAppliedView{ 0 };
        PrismaView needsAppliedView{ 0 };
        PrismaView toxicReleasedView{ 0 };
        PrismaView filterReleasedView{ 0 };
        PrismaView climateReleasedView{ 0 };
        PrismaView needsReleasedView{ 0 };
        PrismaView listenerView{ 0 };
    };

    LayoutState g_layout;

    void OnRadHudLayoutCommand(const char* payload)
    {
        std::string command{ "hudlayout:" };
        if (payload) command += payload;
        RadiationEngine::GetSingleton().HandleDebugCommand(command.c_str());
    }

    float FiniteClamp(float value, float fallback, float low, float high)
    {
        return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    }

    std::filesystem::path LayoutPath()
    {
        HMODULE module = GetModuleHandleW(L"FalloutAnomalyRadiation.dll");
        if (!module) return {};

        std::vector<wchar_t> buffer(512);
        for (;;)
        {
            const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0) return {};
            if (length < buffer.size() - 1)
            {
                auto directory = std::filesystem::path(std::wstring(buffer.data(), length)).parent_path() /
                                 L"FalloutAnomalyRadiation";
                std::error_code ec;
                std::filesystem::create_directories(directory, ec);
                return directory / L"hud_layout.json";
            }
            buffer.resize(buffer.size() * 2);
        }
    }

    json WidgetJson(const WidgetLayout& value)
    {
        return json{
            { "x", value.x },
            { "y", value.y },
            { "s", value.scale },
            { "r", value.rotation },
            { "hidden", value.hidden }
        };
    }

    json FullJson()
    {
        return json{
            { "rad_toxic", WidgetJson(g_layout.toxic) },
            { "rad_filter", WidgetJson(g_layout.filter) },
            { "rad_climate", WidgetJson(g_layout.climate) },
            { "rad_needs", WidgetJson(g_layout.needs) },
            { "temperatureUnit", g_layout.fahrenheit ? "F" : "C" }
        };
    }

    void LoadWidget(const json& source, WidgetLayout& target)
    {
        if (!source.is_object()) return;
        target.x = FiniteClamp(source.value("x", target.x), target.x, 0.0f, 0.95f);
        target.y = FiniteClamp(source.value("y", target.y), target.y, 0.0f, 0.95f);
        target.scale = FiniteClamp(source.value("s", target.scale), target.scale, 0.5f, 2.5f);
        target.rotation = FiniteClamp(source.value("r", target.rotation), target.rotation, -180.0f, 180.0f);
        target.hidden = source.value("hidden", target.hidden);
    }

    void EnsureLoaded()
    {
        if (g_layout.loaded) return;
        g_layout.loaded = true;

        const auto path = LayoutPath();
        if (path.empty()) return;

        std::ifstream input(path);
        if (!input) return;

        try
        {
            json saved;
            input >> saved;
            if (saved.contains("rad_toxic")) LoadWidget(saved["rad_toxic"], g_layout.toxic);
            if (saved.contains("rad_filter")) LoadWidget(saved["rad_filter"], g_layout.filter);
            if (saved.contains("rad_climate")) LoadWidget(saved["rad_climate"], g_layout.climate);
            if (saved.contains("rad_needs")) LoadWidget(saved["rad_needs"], g_layout.needs);
            const auto unit = saved.find("temperatureUnit");
            if (unit != saved.end() && unit->is_string()) g_layout.fahrenheit = unit->get<std::string>() == "F";
        }
        catch (...)
        {
            if (auto logger = spdlog::get("FalloutAnomaly"))
                logger->warn("[HUD] Ignoring invalid built-in HUD layout file: {}", path.string());
        }
    }

    void SaveLayout()
    {
        const auto path = LayoutPath();
        if (path.empty()) return;

        auto temporary = path;
        temporary += L".tmp";

        {
            std::ofstream output(temporary, std::ios::trunc);
            if (!output) return;
            output << FullJson().dump(2) << '\n';
            if (!output.good()) return;
        }

        std::error_code ec;
        std::filesystem::rename(temporary, path, ec);
        if (ec)
        {
            ec.clear();
            std::filesystem::remove(path, ec);
            ec.clear();
            std::filesystem::rename(temporary, path, ec);
        }

        if (ec)
        {
            std::error_code cleanup;
            std::filesystem::remove(temporary, cleanup);
            if (auto logger = spdlog::get("FalloutAnomaly"))
                logger->warn("[HUD] Failed to persist built-in HUD layout: {}", ec.message());
        }
    }

    WidgetLayout* SelectWidget(const std::string& id)
    {
        if (id == "rad_toxic") return &g_layout.toxic;
        if (id == "rad_filter") return &g_layout.filter;
        if (id == "rad_climate") return &g_layout.climate;
        if (id == "rad_needs") return &g_layout.needs;
        return nullptr;
    }

    void UpdateWidgetFromCommand(const json& command, WidgetLayout& target)
    {
        if (command.contains("x")) target.x = FiniteClamp(command.value("x", target.x), target.x, 0.0f, 0.95f);
        if (command.contains("y")) target.y = FiniteClamp(command.value("y", target.y), target.y, 0.0f, 0.95f);
        if (command.contains("s")) target.scale = FiniteClamp(command.value("s", target.scale), target.scale, 0.5f, 2.5f);
        if (command.contains("r")) target.rotation = FiniteClamp(command.value("r", target.rotation), target.rotation, -180.0f, 180.0f);
        if (command.contains("hidden")) target.hidden = command.value("hidden", target.hidden);
    }
}

void RadHud::InvalidateLocalHudLayoutViews()
{
    g_layout.toxicAppliedGeneration = 0;
    g_layout.filterAppliedGeneration = 0;
    g_layout.climateAppliedGeneration = 0;
    g_layout.needsAppliedGeneration = 0;
    g_layout.toxicAppliedView = 0;
    g_layout.filterAppliedView = 0;
    g_layout.climateAppliedView = 0;
    g_layout.needsAppliedView = 0;
    g_layout.toxicReleasedView = 0;
    g_layout.filterReleasedView = 0;
    g_layout.climateReleasedView = 0;
    g_layout.needsReleasedView = 0;
    g_layout.listenerView = 0;
}

void RadHud::SyncLocalHudLayout()
{
    if (!_api) return;
    EnsureLoaded();

    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);

    if (_view != 0 && _domReady.load() && api->IsValid(_view) && g_layout.listenerView != _view)
    {
        api->RegisterJSListener(_view, "radHudLayout", OnRadHudLayoutCommand);
        g_layout.listenerView = _view;
    }

    const bool climateReady = _climateView != 0 && _climateDomReady.load() && api->IsValid(_climateView);
    if (climateReady && (g_layout.climateAppliedView != _climateView ||
                         g_layout.climateAppliedGeneration != g_layout.generation))
        api->InteropCall(_climateView, "setClimateUnit", g_layout.fahrenheit ? "\"F\"" : "\"C\"");

    const bool needsReady = _needsView != 0 && _needsReadyView.load() == _needsView && api->IsValid(_needsView);

    if (_hudApi)
    {
        if (climateReady && g_layout.climateReleasedView != _climateView)
        {
            api->InteropCall(_climateView, "releaseLocalHUDLayout", "");
            g_layout.climateReleasedView = _climateView;
            g_layout.climateAppliedView = _climateView;
            g_layout.climateAppliedGeneration = g_layout.generation;
        }
        if (needsReady && g_layout.needsReleasedView != _needsView)
        {
            api->InteropCall(_needsView, "releaseLocalHUDLayout", "");
            g_layout.needsReleasedView = _needsView;
            g_layout.needsAppliedView = _needsView;
            g_layout.needsAppliedGeneration = g_layout.generation;
        }
        if (_toxicView != 0 && _toxicDomReady.load() && api->IsValid(_toxicView) &&
            g_layout.toxicReleasedView != _toxicView)
        {
            api->InteropCall(_toxicView, "releaseLocalHUDLayout", "");
            g_layout.toxicReleasedView = _toxicView;
        }
        if (_filterView != 0 && _filterDomReady.load() && api->IsValid(_filterView) &&
            g_layout.filterReleasedView != _filterView)
        {
            api->InteropCall(_filterView, "releaseLocalHUDLayout", "");
            g_layout.filterReleasedView = _filterView;
        }
        if (g_layout.preview)
        {
            g_layout.preview = false;
            if (_view != 0 && _domReady.load() && api->IsValid(_view))
                api->InteropCall(_view, "CloseLocalHudEditor", "");
        }
        return;
    }

    if (_toxicView != 0 && _toxicDomReady.load() && api->IsValid(_toxicView) &&
        (g_layout.toxicAppliedView != _toxicView ||
         g_layout.toxicAppliedGeneration != g_layout.generation))
    {
        const auto payload = WidgetJson(g_layout.toxic).dump();
        api->InteropCall(_toxicView, "setLocalHUDLayout", payload.c_str());
        g_layout.toxicAppliedView = _toxicView;
        g_layout.toxicAppliedGeneration = g_layout.generation;
    }

    if (_filterView != 0 && _filterDomReady.load() && api->IsValid(_filterView) &&
        (g_layout.filterAppliedView != _filterView ||
         g_layout.filterAppliedGeneration != g_layout.generation))
    {
        const auto payload = WidgetJson(g_layout.filter).dump();
        api->InteropCall(_filterView, "setLocalHUDLayout", payload.c_str());
        g_layout.filterAppliedView = _filterView;
        g_layout.filterAppliedGeneration = g_layout.generation;
    }

    if (climateReady && (g_layout.climateAppliedView != _climateView ||
                         g_layout.climateAppliedGeneration != g_layout.generation))
    {
        const auto payload = WidgetJson(g_layout.climate).dump();
        api->InteropCall(_climateView, "setLocalHUDLayout", payload.c_str());
        g_layout.climateAppliedView = _climateView;
        g_layout.climateAppliedGeneration = g_layout.generation;
    }
    if (needsReady && (g_layout.needsAppliedView != _needsView ||
                         g_layout.needsAppliedGeneration != g_layout.generation))
    {
        const auto payload = WidgetJson(g_layout.needs).dump();
        api->InteropCall(_needsView, "setLocalHUDLayout", payload.c_str());
        g_layout.needsAppliedView = _needsView;
        g_layout.needsAppliedGeneration = g_layout.generation;
    }
}

void RadHud::SetLocalHudEditorPreview(bool on)
{
    if (!_api) return;
    EnsureLoaded();

    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    g_layout.preview = on && !_hudApi;

    const char* value = g_layout.preview ? "1" : "0";
    if (_toxicView != 0 && _toxicDomReady.load() && api->IsValid(_toxicView))
        api->InteropCall(_toxicView, "setEditorPreview", value);
    if (_filterView != 0 && _filterDomReady.load() && api->IsValid(_filterView))
        api->InteropCall(_filterView, "setEditorPreview", value);
    if (_climateView != 0 && _climateDomReady.load() && api->IsValid(_climateView))
        api->InteropCall(_climateView, "setEditorPreview", value);
    if (_needsView != 0 && _needsReadyView.load() == _needsView && api->IsValid(_needsView))
        api->InteropCall(_needsView, "setEditorPreview", value);

    if (!g_layout.preview && _view != 0 && _domReady.load() && api->IsValid(_view))
        api->InteropCall(_view, "CloseLocalHudEditor", "");
}

void RadHud::ApplyLocalHudLayout(const std::string& payload)
{
    if (payload.empty()) return;
    EnsureLoaded();

    try
    {
        const auto command = json::parse(payload);
        const auto action = command.value("action", std::string{});
        if (action == "unit")
        {
            const auto unit = command.value("unit", std::string{});
            if (unit != "C" && unit != "F") return;
            g_layout.fahrenheit = unit == "F";
            ++g_layout.generation;
            if (command.value("save", false)) SaveLayout();
            SyncLocalHudLayout();
            return;
        }
        if (_hudApi) return;
        if (action == "sync")
        {
            SyncLocalHudLayout();
            return;
        }
        if (action == "preview")
        {
            SetLocalHudEditorPreview(command.value("on", false));
            return;
        }
        if (action != "update") return;

        auto* widget = SelectWidget(command.value("widget", std::string{}));
        if (!widget) return;

        UpdateWidgetFromCommand(command, *widget);
        ++g_layout.generation;
        if (command.value("save", false)) SaveLayout();
        SyncLocalHudLayout();
    }
    catch (...)
    {
        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->warn("[HUD] Ignoring invalid built-in HUD layout command.");
    }
}

void RadHud::OpenHudLayoutEditor()
{
    auto logger = spdlog::get("FalloutAnomaly");

    if (_hudApi)
    {
        if (!_hudEditorApi)
            _hudEditorApi = PRISMA_HUD_API::RequestEditorAPI();

        auto* editor = static_cast<PRISMA_HUD_API::IVPrismaHUD4*>(_hudEditorApi);
        if (!editor)
        {
            if (logger) logger->warn("[HUD] PrismaHUD owns the HUD but its editor API is unavailable.");
            return;
        }

        SyncLocalHudLayout();
        SetDebugMenu(false);
        editor->OpenEditor();
        if (logger) logger->info("[HUD] Opened PrismaHUD layout editor from Shift+F8.");
        return;
    }

    if (!_api || _view == 0 || !_domReady.load()) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!api->IsValid(_view)) return;

    EnsureLoaded();
    SyncLocalHudLayout();
    SetLocalHudEditorPreview(true);
    const auto layout = FullJson().dump();
    api->InteropCall(_view, "OpenLocalHudEditor", layout.c_str());
    if (logger) logger->info("[HUD] Opened built-in temporary Rad HUD layout editor.");
}
