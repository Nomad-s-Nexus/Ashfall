#pragma once

namespace RE
{
    class TESForm;

    namespace BSScript
    {
        class IVirtualMachine;
    }
}

// Constraint: extra relief entries come from the INI bridge; no HC_* form is read or written.
namespace RadReliefConfig
{
    // Constraint: registration needs the VM, so it runs once from F4SEPlugin_Load.
    void InstallPapyrusBridge();

    // Constraint: the INI is read on the game-main thread after records are available; retried until then.
    void ActivateOnGameThread();

    bool IsExtraFood(const RE::TESForm* a_form);
    bool IsExtraThirst(const RE::TESForm* a_form);
}
