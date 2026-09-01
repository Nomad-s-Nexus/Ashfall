#include "RadHud.h"
#include "PrismaUI_F4_API.h"

#include <chrono>
#include <thread>

namespace
{
    void CreateRadHudViewOnPrismaThread(void* userData)
    {
        auto& hud = RadHud::GetSingleton();
        hud.CreateView();

        // Do not remove the game's only Health/AP presentation until Rad has valid replacement views.
        // Once the views exist, use PrismaUI's supported VanillaUI suppression API. This works whether
        // the visible replacements are Rad's standalone fallback or PrismaHUD's widgets, and avoids any
        // direct Scaleform/HUDMenu mutation from Rad.
        if (hud.ViewsReady())
        {
            auto* api11 = static_cast<PRISMA_UI_API::IVPrismaUI11*>(userData);
            if (api11)
            {
                api11->SuppressHUDWidget("HUDPlayerHealthMeter", true);
                api11->SuppressHUDWidget("HUDActionPointMeter", true);
            }
        }
    }
}

void RadHud::StartPrismaViewRetry()
{
    _threadApi = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI11>();
    if (!_threadApi || _viewCreateRetry.exchange(true)) return;

    auto* api11 = static_cast<PRISMA_UI_API::IVPrismaUI11*>(_threadApi);
    auto* retrying = &_viewCreateRetry;
    std::thread([api11, retrying]() {
        // A successful DispatchToGameThread only proves the callback was accepted, not that all three
        // views became valid immediately. Give each accepted CreateView attempt a bounded settle window
        // before dispatching another one. This avoids duplicate view creation on slow web-runtime startup.
        for (int attempt = 0; attempt < 10; ++attempt)
        {
            if (api11->DispatchToGameThread(CreateRadHudViewOnPrismaThread, api11))
            {
                for (int settle = 0; settle < 10; ++settle)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    if (RadHud::GetSingleton().ViewsReady())
                    {
                        retrying->store(false);
                        return;
                    }
                }
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }
        retrying->store(false);
    }).detach();
}
