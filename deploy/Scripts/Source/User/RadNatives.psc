ScriptName RadNatives native hidden

; Constraint: global natives bound by the F4SE plugin; no quest script declares a native, so every source compiles with the Bethesda compiler.

; Constraint: returns the relief flag bits for an ingestible (bit 1 = food, bit 2 = thirst) resolved from the [Relief] INI lists; 0 when unlisted or unbound.
int Function GetReliefFlags(Form akItem) global native

; Constraint: returns override + 1.0, or 0.0 when unset or unbound, so a missing DLL keeps the script defaults.
float Function TuningOverride(int aiKey) global native
