#pragma once

#include <string>
#include <string_view>

// Constraint: live survival-rate overrides; an unset slot leaves the RadSurvivalQuest script default in force.
namespace RadNeedsTuning
{
    // Constraint: loads [Survival] from the INI and registers the Papyrus native; runs once from F4SEPlugin_Load.
    void Install();

    // Constraint: a_command is "reset" or "Key:value"; a changed value is clamped and written to the INI.
    bool Handle(std::string_view a_command);

    // Constraint: {"Key":{"v":value,"o":overridden},...} with v the override or the reference default.
    std::string Json();
}
