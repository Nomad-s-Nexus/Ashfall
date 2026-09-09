#pragma once

#include "ClimatePolicy.h"
#include "ClimateProtectionConfig.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace RE
{
    class BGSKeyword;
    class PlayerCharacter;
}

class ClimateProtectionRuntime
{
public:
    struct ArmorInfo
    {
        std::uint32_t formID{ 0 };
        std::string name;
        float warmth01{ 0.0f };
        float insulation01{ 0.0f };
        bool overridden{ false };
        bool head{ false };
        bool torso{ false };
        bool leftArm{ false };
        bool rightArm{ false };
        bool leftLeg{ false };
        bool rightLeg{ false };
    };

    void Reset() noexcept;
    void Invalidate() noexcept;
    [[nodiscard]] RadClimatePolicy::ProtectionSample Sample(RE::PlayerCharacter* player);
    [[nodiscard]] float LastInsulation01() const noexcept { return _cached.insulation01; }
    [[nodiscard]] bool LastPowerArmor() const noexcept { return _cached.powerArmor; }
    [[nodiscard]] const std::vector<ArmorInfo>& LastArmor() const noexcept { return _armorInfo; }

private:
    void EnsureConfigLoaded();
    void EnsureEventRegistration();
    [[nodiscard]] RadClimatePolicy::ProtectionSample Scan(RE::PlayerCharacter* player);

    ClimateProtectionConfig _config;
    RadClimatePolicy::ProtectionSample _cached{};
    std::vector<ArmorInfo> _armorInfo;
    std::vector<std::uint32_t> _lastWornForms;
    RE::BGSKeyword* _powerArmorKeyword{ nullptr };
    std::atomic<bool> _dirty{ true };
    bool _configLoaded{ false };
    bool _eventsRegistered{ false };
};
