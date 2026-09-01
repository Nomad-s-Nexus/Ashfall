/*
 * For modders: Copy this file into your own project if you wish to use this API.
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

namespace PRISMA_UI_API {
    constexpr const auto PrismaUIPluginName = "PrismaUI_F4";

    // Available PrismaUI interface versions
    enum class InterfaceVersion : uint8_t { V1, V2, V3, V4, V5, V6, V7, V8, V9, V10, V11, V12 };

    typedef void (*OnDomReadyCallback)(PrismaView view);
    typedef void (*JSCallback)(const char* result);
    typedef void (*JSListenerCallback)(const char* argument);

    // JavaScript console message severity level for use with RegisterConsoleCallback().
    enum class ConsoleMessageLevel : uint8_t { Log = 0, Warning, Error, Debug, Info };

    // Console message callback.
    typedef void (*ConsoleMessageCallback)(PrismaView view, ConsoleMessageLevel level, const char* message);

    // PrismaUI modder interface v1
    class IVPrismaUI1 {
    protected:
        ~IVPrismaUI1() = default;

    public:
        // Create view.
        virtual PrismaView CreateView(const char* htmlPath,
                                      OnDomReadyCallback onDomReadyCallback = nullptr) noexcept = 0;

        // Send JS code to UI.
        virtual void Invoke(PrismaView view, const char* script, JSCallback callback = nullptr) noexcept = 0;

        // Call JS function through JS Interop API (best performance).
        virtual void InteropCall(PrismaView view, const char* functionName, const char* argument) noexcept = 0;

        // Register JS listener.
        virtual void RegisterJSListener(PrismaView view, const char* functionName,
                                        JSListenerCallback callback) noexcept = 0;

        // Returns true if view has focus.
        virtual bool HasFocus(PrismaView view) noexcept = 0;

        // Set focus on view.
        virtual bool Focus(PrismaView view, bool pauseGame = false, bool disableFocusMenu = false) noexcept = 0;

        // Remove focus from view.
        virtual void Unfocus(PrismaView view) noexcept = 0;

        // Show a hidden view.
        virtual void Show(PrismaView view) noexcept = 0;

        // Hide a visible view.
        virtual void Hide(PrismaView view) noexcept = 0;

        // Returns true if view is hidden.
        virtual bool IsHidden(PrismaView view) noexcept = 0;

        // Get scroll size in pixels.
        virtual int GetScrollingPixelSize(PrismaView view) noexcept = 0;

        // Set scroll size in pixels.
        virtual void SetScrollingPixelSize(PrismaView view, int pixelSize) noexcept = 0;

        // Returns true if view exists.
        virtual bool IsValid(PrismaView view) noexcept = 0;

        // Completely destroy view.
        virtual void Destroy(PrismaView view) noexcept = 0;

        // Set view order.
        virtual void SetOrder(PrismaView view, int order) noexcept = 0;

        // Get view order.
        virtual int GetOrder(PrismaView view) noexcept = 0;

        // Create inspector view for debugging.
        virtual void CreateInspectorView(PrismaView view) noexcept = 0;

        // Show or hide the inspector overlay.
        virtual void SetInspectorVisibility(PrismaView view, bool visible) noexcept = 0;

        // Returns true if inspector is visible.
        virtual bool IsInspectorVisible(PrismaView view) noexcept = 0;

        // Set inspector window position and size.
        virtual void SetInspectorBounds(PrismaView view, float topLeftX, float topLeftY, unsigned int width,
                                        unsigned int height) noexcept = 0;

        // Returns true if any view has active focus.
        virtual bool HasAnyActiveFocus() noexcept = 0;
    };

    // PrismaUI modder interface v2 (extends v1)
    class IVPrismaUI2 : public IVPrismaUI1 {
    protected:
        ~IVPrismaUI2() = default;

    public:
        // Register a callback to receive JavaScript console messages from a view.
        // Pass nullptr to unregister.
        virtual void RegisterConsoleCallback(PrismaView view, ConsoleMessageCallback callback) noexcept = 0;
    };

    // PrismaUI modder interface v3 (extends v2)
    class IVPrismaUI3 : public IVPrismaUI2 {
    protected:
        ~IVPrismaUI3() = default;

    public:
        // Register translations for a view from a Fallout 4 translation file.
        // pluginName is the bare plugin name matching the translation file, e.g. "MyPlugin_F4".
        // The framework detects the game language, loads Data\Interface\Translations\<pluginName>_<lang>.txt,
        // and injects window.L10N / window.t into the page before scripts run (OnWindowObjectReady).
        // Call this immediately after CreateView, before the DOM is ready.
        virtual void RegisterTranslations(PrismaView view, const char* pluginName) noexcept = 0;
    };

    // Callback type for EnumerateViews. Called once per view with the view's ID and its
    // original html path (relative to Data/PrismaUI_F4/views/, e.g. "debug_panel.html").
    typedef void (*ViewEnumCallback)(PrismaView id, const char* htmlPath, void* userdata);

    // PrismaUI modder interface v4 (extends v3)
    class IVPrismaUI4 : public IVPrismaUI3 {
    protected:
        ~IVPrismaUI4() = default;

    public:
        // Game-thread-safe JS listener. Callback fires on the game thread — RE:: access is safe
        // directly inside the callback with no AddTask required.
        // Use this instead of RegisterJSListener whenever you need to touch game state.
        virtual void BindUIEvent(PrismaView view, const char* functionName,
                                 JSListenerCallback callback) noexcept = 0;

        // Enumerate all currently-registered views across all plugins.
        // Callback is invoked synchronously for each view; htmlPath is the relative path
        // passed to CreateView (e.g., "Interface/PrismaMCM/mcm.html").
        // Safe to call from any thread.
        virtual void EnumerateViews(ViewEnumCallback callback, void* userdata) noexcept = 0;
    };

    class IVPrismaUI5 : public IVPrismaUI4 {
    protected:
        ~IVPrismaUI5() = default;

    public:
        virtual void* GetViewSRV(PrismaView view) noexcept = 0;
        virtual void SetViewOffscreen(PrismaView view, bool offscreen) noexcept = 0;
        virtual bool BindViewToGeometry(PrismaView view, void* rootObject, const char* geometryName) noexcept = 0;
        virtual bool BindViewToScreenTexture(PrismaView view, void* rootObject, const char* textureSubstring) noexcept = 0;
        virtual void UnbindViewFromGeometry(PrismaView view) noexcept = 0;
    };

    class IVPrismaUI6 : public IVPrismaUI5 {
    protected:
        ~IVPrismaUI6() = default;

    public:
        virtual bool SuppressHUDWidget(const char* className, bool suppress) noexcept = 0;
        virtual bool SuppressVanillaMenu(const char* menuName, bool suppress) noexcept = 0;
        virtual bool CloseVanillaMenu(const char* menuName) noexcept = 0;
    };

    typedef bool (*MenuSuppressPredicate)();

    class IVPrismaUI7 : public IVPrismaUI6 {
    protected:
        ~IVPrismaUI7() = default;

    public:
        virtual void SuppressVanillaMenuIf(const char* menuName, MenuSuppressPredicate predicate) noexcept = 0;
        virtual void EnableActivateChoiceFilter(bool enable, bool dropDefaultTake) noexcept = 0;
        virtual void SuppressActivateChoicePerk(uint32_t perkFormID, bool suppress) noexcept = 0;
    };

    typedef void (*ViewEnumCallbackEx)(PrismaView id, const char* htmlPath, const char* owner, void* userdata);

    enum class ViewHealth : int {
        kUnknown = -1,
        kCreating = 0,
        kDomReady = 1,
        kLive = 2,
        kLoadFailed = 3,
        kDomReadyTimeout = 4,
        kUnresponsive = 5,
        kJsError = 6,
    };

    class IVPrismaUI8 : public IVPrismaUI7 {
    protected:
        ~IVPrismaUI8() = default;

    public:
        virtual void EnumerateViewsEx(ViewEnumCallbackEx callback, void* userdata) noexcept = 0;
        virtual bool GetActivateChoiceLabel(uint32_t buttonIndex, char* outBuffer, size_t bufferSize) noexcept = 0;
        virtual bool TriggerActivateChoice(uint32_t buttonIndex) noexcept = 0;
        virtual ViewHealth GetViewHealth(PrismaView view) noexcept = 0;
        virtual void SetViewOffscreenSize(PrismaView view, int width, int height) noexcept = 0;
    };

    class IVPrismaUI9 : public IVPrismaUI8 {
    protected:
        ~IVPrismaUI9() = default;

    public:
        virtual bool IsUsingGamepad() noexcept = 0;
        virtual int GetControllerStyle() noexcept = 0;
        virtual void SetControllerStyle(int style) noexcept = 0;
        virtual void NoteInputDevice(int device) noexcept = 0;
        virtual bool GetButtonPrompt(const char* userEvent, char* outBuffer, size_t bufferSize) noexcept = 0;
        virtual bool GetGamepadButtonName(uint32_t bsButtonCode, char* outBuffer, size_t bufferSize) noexcept = 0;
        virtual void SetViewOwnsEscape(PrismaView view, bool owns) noexcept = 0;
        virtual void SetViewOffscreenBackground(PrismaView view, uint32_t argb) noexcept = 0;
    };

    enum class ViewRole : uint32_t { kUnspecified = 0, kWidget = 1, kPanel = 2 };

    struct InputRegion {
        int32_t x;
        int32_t y;
        int32_t width;
        int32_t height;
    };

    class IVPrismaUI10 : public IVPrismaUI9 {
    protected:
        ~IVPrismaUI10() = default;

    public:
        virtual void SetViewRole(PrismaView view, ViewRole role) noexcept = 0;
        virtual ViewRole GetViewRole(PrismaView view) noexcept = 0;
        virtual PrismaView GetFocusedView() noexcept = 0;
        virtual bool IsAnyPanelVisible(PrismaView ignoreView) noexcept = 0;
        virtual bool FocusOverlay(PrismaView view, bool pauseGame = false, bool disableFocusMenu = false) noexcept = 0;
        virtual bool SetInputRegions(PrismaView view, const InputRegion* regions, uint32_t count) noexcept = 0;
    };

    using GameThreadTaskCallback = void (*)(void* userdata);
    using GameThreadUIEventCallback = void (*)(const char* argument, void* userdata);

    class IVPrismaUI11 : public IVPrismaUI10 {
    protected:
        ~IVPrismaUI11() = default;

    public:
        virtual bool DispatchToGameThread(GameThreadTaskCallback callback, void* userdata) noexcept = 0;
        virtual bool IsGameThread() noexcept = 0;
        virtual bool BindGameThreadUIEvent(PrismaView view, const char* functionName,
                                           GameThreadUIEventCallback callback, void* userdata) noexcept = 0;
    };

    class IVPrismaUI12 : public IVPrismaUI11 {
    protected:
        ~IVPrismaUI12() = default;

    public:
        virtual bool BindControllerAction(PrismaView view, const char* canonicalButton, const char* action) noexcept = 0;
        virtual bool UnbindControllerAction(PrismaView view, const char* canonicalButton) noexcept = 0;
        virtual void ClearControllerActions(PrismaView view) noexcept = 0;
    };

    // Maps interface types to InterfaceVersion enum values.
    // compile-time constraint -- only request interface versions that actually exist.
    template <typename T>
    struct InterfaceVersionMap;

    template <>
    struct InterfaceVersionMap<IVPrismaUI1> {
        static constexpr InterfaceVersion version = InterfaceVersion::V1;
    };

    template <>
    struct InterfaceVersionMap<IVPrismaUI2> {
        static constexpr InterfaceVersion version = InterfaceVersion::V2;
    };

    template <>
    struct InterfaceVersionMap<IVPrismaUI3> {
        static constexpr InterfaceVersion version = InterfaceVersion::V3;
    };

    template <>
    struct InterfaceVersionMap<IVPrismaUI4> {
        static constexpr InterfaceVersion version = InterfaceVersion::V4;
    };

    template <>
    struct InterfaceVersionMap<IVPrismaUI5> { static constexpr InterfaceVersion version = InterfaceVersion::V5; };
    template <>
    struct InterfaceVersionMap<IVPrismaUI6> { static constexpr InterfaceVersion version = InterfaceVersion::V6; };
    template <>
    struct InterfaceVersionMap<IVPrismaUI7> { static constexpr InterfaceVersion version = InterfaceVersion::V7; };
    template <>
    struct InterfaceVersionMap<IVPrismaUI8> { static constexpr InterfaceVersion version = InterfaceVersion::V8; };
    template <>
    struct InterfaceVersionMap<IVPrismaUI9> { static constexpr InterfaceVersion version = InterfaceVersion::V9; };
    template <>
    struct InterfaceVersionMap<IVPrismaUI10> { static constexpr InterfaceVersion version = InterfaceVersion::V10; };
    template <>
    struct InterfaceVersionMap<IVPrismaUI11> { static constexpr InterfaceVersion version = InterfaceVersion::V11; };
    template <>
    struct InterfaceVersionMap<IVPrismaUI12> { static constexpr InterfaceVersion version = InterfaceVersion::V12; };

    typedef void* (*RequestPluginAPIFunc)(InterfaceVersion interfaceVersion);

    /// Request the PrismaUI API interface.
    /// Recommended: Send your request during or after F4SE::MessagingInterface::kGameDataReady to make sure the dll
    /// has already been loaded
    [[nodiscard]] inline void* RequestPluginAPI(InterfaceVersion a_interfaceVersion = InterfaceVersion::V1) {
        auto pluginHandle = GetModuleHandleW(L"PrismaUI_F4.dll");
        if (!pluginHandle) {
            return nullptr;
        }

        auto requestAPIFunction =
            reinterpret_cast<RequestPluginAPIFunc>(GetProcAddress(pluginHandle, "RequestPluginAPI"));

        if (requestAPIFunction) {
            return requestAPIFunction(a_interfaceVersion);
        }

        return nullptr;
    }

    /// Request a specific PrismaUI API interface version.
    /// Returns nullptr if the loaded PrismaUI DLL does not support the requested version.
    ///
    /// Usage:
    ///   auto* api = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI4>(); // recommended
    ///   auto* api = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI3>(); // legacy
    template <typename T>
    [[nodiscard]] inline T* RequestPluginAPI() {
        return static_cast<T*>(RequestPluginAPI(InterfaceVersionMap<T>::version));
    }
}
