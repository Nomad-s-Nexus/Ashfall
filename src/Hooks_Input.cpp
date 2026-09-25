#include "pch.h"
#include "Hooks.h"

#ifdef MAX_PATH
#undef MAX_PATH
#endif

#ifdef MEM_RELEASE
#undef MEM_RELEASE
#endif

#include <RE/B/BSInputEventUser.h>
#include <RE/B/ButtonEvent.h>
#include <RE/M/MenuControls.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace Hooks
{
    class RadInputHandler final : public RE::BSInputEventUser
    {
    public:
        static RadInputHandler* GetSingleton() { static RadInputHandler instance; return &instance; }
        bool ShouldHandleEvent(const RE::InputEvent* e) override
        {
            return e && e->eventType == RE::INPUT_EVENT_TYPE::kButton && e->device == RE::INPUT_DEVICE::kKeyboard;
        }
        void OnButtonEvent(const RE::ButtonEvent* e) override
        {
            if (!e || !e->QJustPressed() || e->GetBSButtonCode() != RE::BS_BUTTON_CODE::kF8 ||
                (::GetAsyncKeyState(VK_SHIFT) & 0x8000) == 0) return;
            const_cast<RE::ButtonEvent*>(e)->handled = RE::InputEvent::HANDLED_RESULT::kStop;
        }
    };

    bool InstallInputHandler()
    {
        static bool installed = false;
        if (installed) return true;
        auto logger = spdlog::get("FalloutAnomaly");
        auto* controls = RE::MenuControls::GetSingleton();
        if (!controls) { if (logger) logger->error("[Input] MenuControls unavailable; Shift+F8 disabled."); return false; }
        controls->RegisterHandler(RadInputHandler::GetSingleton());
        installed = true;
        if (logger) logger->info("[Input] Rad debug handler registered with MenuControls (Shift+F8).");
        return true;
    }

}
