#include "pch.h"
#include "RadHud.h"
#include "RadiationEngine.h"

#include "PrismaHUD_F4_API.h"
#include "PrismaUI_F4_API.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string_view>

namespace
{
    std::string VkLabel(int vk)
    {
        if (vk <= 0) return {};
        if (vk >= 0x70 && vk <= 0x7B) return "F" + std::to_string(vk - 0x6F);
        if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, static_cast<char>(vk));
        char hex[8];
        std::snprintf(hex, sizeof(hex), "#%X", vk);
        return hex;
    }

    bool ResolveCompanionMode()
    {
        constexpr const char* kIni = ".\\Data\\F4SE\\Plugins\\FalloutAnomalyRadiation.ini";
        char setting[16]{};
        ::GetPrivateProfileStringA("Hud", "Mode", "auto", setting, sizeof(setting), kIni);
        std::string mode = setting;
        std::ranges::transform(mode, mode.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const auto logger = spdlog::get("FalloutAnomaly");

        if (mode == "phosphor")
        {
            if (logger) logger->info("[HUD] Resolved HUD mode: {} (forced by [Hud] Mode).", mode);
            return true;
        }
        if (mode != "auto" && logger)
            logger->warn("[HUD] [Hud] Mode=\"{}\" is not auto|phosphor; using auto.", mode);

        const bool loaded = ::GetModuleHandleW(L"PhosphorHUD.dll") != nullptr;
        std::error_code ec;
        const bool onDisk = std::filesystem::exists("Data/F4SE/Plugins/PhosphorHUD.dll", ec) ||
            std::filesystem::exists("Data/F4SE/Plugins/PhosphorHUD.ini", ec);
        const bool companion = loaded || onDisk;
        if (logger)
            logger->info("[HUD] Resolved HUD mode: {} (auto: PhosphorHUD.dll loaded={}, PhosphorHUD files on disk={}).",
                companion ? "phosphor-companion" : "own-vitals", loaded, onDisk);
        return companion;
    }
}

bool CompanionWidgetDefault(const char* id, float& x, float& y)
{
    struct Slot { const char* id; float x; float y; };
    constexpr Slot kSlots[] = {
        // Constraint: companion rows keep clear of Phosphor's rings, XP text and compass row; see docs/code-notes/Ashfall.md.
        { "rad_toxic", 396.0f, 538.0f }, { "rad_needs", 396.0f, 646.0f },
        { "rad_climate", 396.0f, 754.0f }, { "rad_filter", 396.0f, 862.0f } };
    if (!id || !RadHud::GetSingleton().IsCompanionMode()) return false;
    for (const auto& slot : kSlots)
    {
        if (std::string_view(slot.id) != id) continue;
        x = slot.x / 1920.0f;
        y = slot.y / 1080.0f;
        return true;
    }
    return false;
}

PRISMA_HUD_API::WidgetDesc CompanionPlaced(const PRISMA_HUD_API::WidgetDesc& desc)
{
    auto placed = desc;
    CompanionWidgetDefault(desc.id, placed.defaultX, placed.defaultY);
    return placed;
}

bool RadHud::IsCompanionMode(){
    int resolved = _companionMode.load();
    if (resolved < 0)
    {
        resolved = ResolveCompanionMode() ? 1 : 0;
        _companionMode.store(resolved);
    }
    return resolved == 1;
}

void RadHud::PushTheme(uint64_t view)
{
    if (!_api || view == 0) return;
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    if (!api->IsValid(view)) return;
    api->InteropCall(view, "setTheme", "\"phosphor\"");
}

void RadHud::PushFilterKeys(uint64_t view)
{
    auto* api = static_cast<PRISMA_UI_API::IVPrismaUI2*>(_api);
    const auto keys = json{
        { "replace", VkLabel(RadiationEngine::GetSingleton().FilterReplaceKey()) },
        { "repair", VkLabel(RadiationEngine::GetSingleton().ConditionRepairKey()) }
    }.dump();
    if (keys == _keysPayload) return;
    _keysPayload = keys;
    api->InteropCall(view, "setKeys", keys.c_str());
}
