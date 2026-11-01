#include "pch.h"
#include "Hooks.h"

#include <RE/B/BSSkyShader.h>

#include <atomic>

namespace Hooks
{
    namespace
    {
        constexpr std::size_t kSetupGeometrySlot = 7;
        constexpr std::ptrdiff_t kPassPropertyOffset = 0x10;
        constexpr std::ptrdiff_t kSkyObjectTypeOffset = 0xA8;
        constexpr std::ptrdiff_t kBlendAlphaOffset = 0x7C;
        constexpr int kSkyObjectClouds = 3;

        std::atomic<float> s_domeBlend{ 0.0f };
        std::atomic_bool s_installed{ false };
        std::atomic<std::uint32_t> s_cloudPasses{ 0 };
        std::atomic<std::uint32_t> s_cloudPassesScaled{ 0 };
        using SetupGeometryFn = void (*)(void*, void*);
        SetupGeometryFn s_original = nullptr;

        void SetupGeometryHook(void* shader, void* pass)
        {
            s_cloudPasses.fetch_add(1, std::memory_order_relaxed);
            const float blend = s_domeBlend.load(std::memory_order_relaxed);
            float* alpha = nullptr;
            float saved = 0.0f;
            if (blend > 0.0f && pass)
            {
                auto* property = *reinterpret_cast<char**>(static_cast<char*>(pass) + kPassPropertyOffset);
                if (property && *reinterpret_cast<const int*>(property + kSkyObjectTypeOffset) == kSkyObjectClouds)
                {
                    s_cloudPassesScaled.fetch_add(1, std::memory_order_relaxed);
                    alpha = reinterpret_cast<float*>(property + kBlendAlphaOffset);
                    saved = *alpha;
                    *alpha = saved * (1.0f - blend);
                }
            }
            s_original(shader, pass);
            if (alpha) *alpha = saved;
        }
    }

    void SetStormDomeBlend(float blend) noexcept
    {
        s_domeBlend.store(blend, std::memory_order_relaxed);
    }

    void GetSkyCloudStats(std::uint32_t& passes, std::uint32_t& scaled) noexcept
    {
        passes = s_cloudPasses.load(std::memory_order_relaxed);
        scaled = s_cloudPassesScaled.load(std::memory_order_relaxed);
    }

    bool InstallSkyCloudHook()
    {
        if (s_installed.load(std::memory_order_acquire)) return true;
        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE::BSSkyShader[0] };
        s_original = reinterpret_cast<SetupGeometryFn>(reinterpret_cast<void**>(vtable.get())[kSetupGeometrySlot]);
        if (!s_original || s_original == &SetupGeometryHook) return false;
        vtable.write_vfunc(kSetupGeometrySlot, &SetupGeometryHook);
        s_installed.store(true, std::memory_order_release);
        return true;
    }
}
