#pragma once

#include "RadNeedsTuningPolicy.h"

#include <string>
#include <string_view>

// Constraint: live setting overrides; an unset slot leaves the script default or the INI value for that system in force.
namespace RadNeedsTuning
{
    // Constraint: loads [Tuning] from the INI and registers the Papyrus natives; runs once from F4SEPlugin_Load.
    void Install();

    // Constraint: a_command is "reset" or "Key:value"; a changed value is clamped and written to the INI.
    bool Handle(std::string_view a_command);

    std::string Json();

    void SetLive(RadNeedsTuningPolicy::Key a_key, float a_value);

    // Constraint: the override when one is set, else a_fallback; safe from any thread.
    float Value(RadNeedsTuningPolicy::Key a_key, float a_fallback);
    inline bool On(RadNeedsTuningPolicy::Key a_key, bool a_fallback)
    {
        return Value(a_key, a_fallback ? 1.0f : 0.0f) >= 0.5f;
    }
}
