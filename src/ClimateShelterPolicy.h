#pragma once

namespace RadClimateShelterPolicy
{
    struct Factors
    {
        float thermal01{ 0.0f };
        float precipitation01{ 0.0f };
    };

    [[nodiscard]] constexpr Factors FromState(bool indoors, bool roofed, bool unresolved) noexcept
    {
        if (indoors || unresolved)
            return { 1.0f, 1.0f };
        if (roofed)
            return { 0.25f, 1.0f };
        return {};
    }
}
