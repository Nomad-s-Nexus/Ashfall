/*
 * PrismaHUD_F4_API.h
 * For modders: copy this file into your plugin's src/ to register your PrismaUI
 * views as HUD widgets so users can position them with the in-game HUD editor.
 *
 * Timing: call RegisterWidget() inside kPostLoadGame / kNewGame, after you have
 * called CreateView() and stored the PrismaView handle.
 *
 */
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif

#include <Windows.h>
#include <stdint.h>

typedef uint64_t PrismaView;

namespace PRISMA_HUD_API {

    constexpr const auto PrismaHUDPluginName = "PrismaHUD_F4";

    enum class InterfaceVersion : uint8_t { V1, V2, V3 };

    struct WidgetDesc {
        const char* id;           // Stable identifier used as the key in layout JSON (e.g. "prisma_inventory")
        const char* displayName;  // Human-readable label shown in the editor (e.g. "Inventory")
        // Default position/size as a FRACTION of the viewport (value / 1920 or / 1080), NOT pixels.
        // The editor multiplies these by the live viewport, so raw pixels put the drag box off-screen
        // (e.g. 30 -> 30*1920). Pass e.g. 30.0f/1920.0f for "30px from the left on a 1920-wide screen".
        float       defaultX;     // 0.0 = left edge, 1.0 = right edge
        float       defaultY;     // 0.0 = top edge,  1.0 = bottom edge
        float       defaultW;     // fraction of viewport width
        float       defaultH;     // fraction of viewport height
    };

    class IVPrismaHUD1 {
    protected:
        ~IVPrismaHUD1() = default;

    public:
        // Register a PrismaUI view as a positionable HUD widget.
        // Call on kPostLoadGame / kNewGame after CreateView(). Safe to call again on reload —
        // re-registering the same id with a new view handle replaces the old entry.
        virtual void RegisterWidget(PrismaView view, WidgetDesc desc) noexcept = 0;

        // Remove a widget from the registry (e.g. when your view is destroyed).
        virtual void UnregisterWidget(PrismaView view) noexcept = 0;

        // Force-apply the current saved layout to a single registered view.
        // Useful after recreating a view on reload.
        virtual void ApplyLayout(PrismaView view) noexcept = 0;
    };

    typedef void (*BindingCallback)(void* context);

    struct BindingDesc {
        const char* id;
        const char* displayName;
        const char* category;
        uint32_t defaultKey;
        bool requireShift;
        bool consume;
        bool suppressible;
    };

    class IVPrismaHUD2 {
    protected:
        ~IVPrismaHUD2() = default;

    public:
        virtual void RegisterBinding(BindingDesc desc, BindingCallback onDown, void* downContext,
                                     BindingCallback onUp, void* upContext) noexcept = 0;
        virtual void UnregisterBinding(const char* id) noexcept = 0;
        virtual uint32_t GetBoundKey(const char* id) noexcept = 0;
        virtual bool IsExclusiveModeActive() const noexcept = 0;
        virtual void SetExclusiveInputMode(bool on) noexcept = 0;
    };

    typedef void (*ActivateChoiceCallback)(void* context);

    class IVPrismaHUD3 {
    protected:
        ~IVPrismaHUD3() = default;

    public:
        virtual void RegisterActivateChoice(const char* id, const char* label, const char* iconId,
                                            ActivateChoiceCallback onTrigger, void* context) noexcept = 0;
        virtual void UnregisterActivateChoice(const char* id) noexcept = 0;
    };

    typedef void* (*RequestPluginAPIFunc)(InterfaceVersion interfaceVersion);

    [[nodiscard]] inline IVPrismaHUD1* RequestPluginAPI() {
        auto pluginHandle = GetModuleHandleW(L"PrismaHUD_F4.dll");
        if (!pluginHandle)
            return nullptr;

        auto fn = reinterpret_cast<RequestPluginAPIFunc>(
            GetProcAddress(pluginHandle, "RequestPrismaHUDAPI"));
        if (!fn)
            return nullptr;

        return static_cast<IVPrismaHUD1*>(fn(InterfaceVersion::V1));
    }

    [[nodiscard]] inline IVPrismaHUD2* RequestBindingsAPI() {
        auto pluginHandle = GetModuleHandleW(L"PrismaHUD_F4.dll");
        if (!pluginHandle)
            return nullptr;

        auto fn = reinterpret_cast<RequestPluginAPIFunc>(
            GetProcAddress(pluginHandle, "RequestPrismaHUDAPI"));
        if (!fn)
            return nullptr;

        return static_cast<IVPrismaHUD2*>(fn(InterfaceVersion::V2));
    }

    [[nodiscard]] inline IVPrismaHUD3* RequestActivateChoicesAPI() {
        auto pluginHandle = GetModuleHandleW(L"PrismaHUD_F4.dll");
        if (!pluginHandle)
            return nullptr;

        auto fn = reinterpret_cast<RequestPluginAPIFunc>(
            GetProcAddress(pluginHandle, "RequestPrismaHUDAPI"));
        if (!fn)
            return nullptr;

        return static_cast<IVPrismaHUD3*>(fn(InterfaceVersion::V3));
    }

} // namespace PRISMA_HUD_API
