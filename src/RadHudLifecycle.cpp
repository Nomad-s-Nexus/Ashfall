#include "RadHud.h"
#include "PrismaUI_F4_API.h"

#include <chrono>
#include <thread>

namespace
{
    void SuppressVanillaMetersOnPrismaThread(void* userData)
    {
        auto* api11 = static_cast<PRISMA_UI_API::IVPrismaUI11*>(userData);
        if (!api11 || !RadHud::GetSingleton().ViewsReady()) return;
        if (RadHud::GetSingleton().IsCompanionMode()) return;
        api11->SuppressHUDWidget("HUDPlayerHealthMeter", true);
        api11->SuppressHUDWidget("HUDActionPointMeter", true);
    }

    void CreateRadHudViewOnPrismaThread(void* userData)
    {
        auto& hud = RadHud::GetSingleton();
        auto* api11 = static_cast<PRISMA_UI_API::IVPrismaUI11*>(userData);
        // Constraint: when all views exist, only re-apply meter suppression (no un-suppress or CreateView).
        if (hud.ViewsExist())
        {
            SuppressVanillaMetersOnPrismaThread(api11);
            return;
        }
        if (api11 && !hud.IsCompanionMode())
        {
            api11->SuppressHUDWidget("HUDPlayerHealthMeter", false);
            api11->SuppressHUDWidget("HUDActionPointMeter", false);
        }
        hud.CreateView();
        if (hud.ViewsReady())
            SuppressVanillaMetersOnPrismaThread(api11);
    }
}

bool RadHud::StartPrismaViewRetry()
{
    if (!_threadApi)
        _threadApi = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI11>();
    if (!_threadApi) return false;

    // Constraint: at most one retry starts per second, and the atomic guard keeps retries from overlapping.
    const auto nowMs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    // Constraint: a throttled call returns true only when a retry is already running or the views already exist; otherwise nothing started and the caller must create the view.
    if (nowMs < _viewRetryNotBeforeMs.load()) return _viewCreateRetry.load() || ViewsExist();
    if (_viewCreateRetry.exchange(true)) return true;
    _viewRetryNotBeforeMs.store(nowMs + 1000);

    auto* api11 = static_cast<PRISMA_UI_API::IVPrismaUI11*>(_threadApi);
    if (ViewsExist())
    {
        // Constraint: all views already exist, so no retry thread is needed; re-apply suppression only.
        _viewCreateRetry.store(false);
        api11->DispatchToGameThread(SuppressVanillaMetersOnPrismaThread, api11);
        return true;
    }
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
                    if (RadHud::GetSingleton().ViewsReady() &&
                        api11->DispatchToGameThread(SuppressVanillaMetersOnPrismaThread, api11))
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
    return true;
}
