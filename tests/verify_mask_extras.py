"""Contract for the [Masks] INI section: protective gear declared by other plugins.

The engine recognises our own masks by the FARO_GasMask keyword in RadiationOverhaul.esp. A mask mod
that ships its own armor - Gasmask Overhaul (Nexus 108845) and its PPM-88 - carries no such keyword,
and this repository must not edit another plugin's records, so the INI declares the extra gear and the
native side resolves it. This test pins the INI entries, the native parsing and the policy split.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INI = ROOT / "deploy/F4SE/Plugins/FalloutAnomalyRadiation.ini"
MASK_SRC = ROOT / "src/RadEngine_Mask.cpp"
POLICY = ROOT / "src/RadCorrectnessPolicy.h"
HEADER = ROOT / "src/RadiationEngine.h"
GASMASK_OVERHAUL_KEYWORD = "Gasmask Overhaul.esp:0x011001"
GASMASK_OVERHAUL_ARMOR = "Gasmask Overhaul.esp:0x011004"

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def text(path):
    return path.read_text(encoding="utf-8", errors="replace")


def body(source, signature):
    match = re.search(re.escape(signature) + r"(.*?)\n\}", source, re.S)
    return match.group(1) if match else ""


check(INI.is_file(), f"missing {INI.relative_to(ROOT)}")
ini = text(INI) if INI.is_file() else ""
check("[Masks]" in ini, "the INI must ship a [Masks] section")
keys = dict(re.findall(r"^(ExtraKeywords|ExtraMasks)=(.*)$", ini, re.M))
check("ExtraKeywords" in keys, "[Masks] must ship an ExtraKeywords key")
check("ExtraMasks" in keys, "[Masks] must ship an ExtraMasks key")
check(GASMASK_OVERHAUL_KEYWORD.lower() in (keys.get("ExtraKeywords") or "").lower(),
      "[Masks] must declare Gasmask Overhaul's PPM-88 keyword so that mask is protective")
check(GASMASK_OVERHAUL_ARMOR.lower() in (keys.get("ExtraMasks") or "").lower(),
      "[Masks] must list Gasmask Overhaul's PPM-88 armor so an eye-slot layout still counts")

check(MASK_SRC.is_file(), f"missing {MASK_SRC.relative_to(ROOT)}")
source = text(MASK_SRC) if MASK_SRC.is_file() else ""
load = body(source, "void RadiationEngine::LoadMaskConfig()")
check(load, "RadEngine_Mask.cpp must define LoadMaskConfig")
for key in ("ExtraKeywords", "ExtraMasks"):
    check(f'GetPrivateProfileStringA("Masks", "{key}"' in load,
          f"LoadMaskConfig must read [Masks] {key}")
check("ResolveMaskEntry(" in load, "an INI entry must resolve through the shared helper")
check("LookupForm(" in body(source, "RE::TESForm* ResolveMaskEntry("),
      "an entry must resolve through TESDataHandler::LookupForm, not a raw form id")
check("kKYWD" in load, "an ExtraKeywords entry must be type-checked as a keyword")
check("kARMO" in load, "an ExtraMasks entry must be type-checked as an armor")
check("LoadMaskConfig();" in body(source, "void RadiationEngine::LoadFilterIni()"),
      "LoadMaskConfig must run with the other INI loads")
elsewhere = ROOT / "src/RadEngine_ESL.cpp"
esl = text(elsewhere) if elsewhere.is_file() else ""
check("LoadMaskConfig();" in body(esl, "void RadiationEngine::EnsureEslLoaded()"),
      "mask extras must re-resolve once the data handler is proven ready, in case init ran early")

# Constraint: any mouth-slot armor with radiation resistance is a mask, so most mask mods need no entry.
check("InferFromRadResist=1" in ini, "[Masks] must ship InferFromRadResist=1")
check("MinRadResist=" in ini, "[Masks] must ship MinRadResist")
for key in ("InferFromRadResist", "MinRadResist"):
    check(f'GetPrivateProfileIntA("Masks", "{key}"' in load, f"LoadMaskConfig must read [Masks] {key}")

# Constraint: a drained or broken mask must be recoverable in game without editing files first.
settings = dict(re.findall(r"^(DurationSeconds|ReplaceKey|RepairKey)=(\d+)$", ini, re.M))
check(int(settings.get("DurationSeconds", "0")) >= 900,
      "[Filter] DurationSeconds must be long enough that a mask is not drained in minutes")
check(int(settings.get("ReplaceKey", "0")) != 0, "[Filter] ReplaceKey must ship bound, not 0")
check(int(settings.get("RepairKey", "0")) != 0, "[Condition] RepairKey must ship bound, not 0")

recognise = body(source, "bool RadiationEngine::IsRecognizedGasMask(")
check("RadCorrectnessPolicy::IsConfiguredMask(" in recognise,
      "mask recognition must go through the policy predicate, so both branches stay testable")
check("IsListedMask(" in recognise, "a listed armor must count as a mask")
check("_extraMaskKeywords" in recognise, "an extra keyword must count as a mask")
check("_gasMaskKeyword" in recognise, "the vanilla FARO_GasMask rule must stay in place")
check("HasRadResistance(" in recognise and "IsRadShieldedMask(" in recognise,
      "mask recognition must fall back to radiation resistance")
rad_body = body(source, "bool RadiationEngine::HasRadResistance(")
check("0x00060A85" in rad_body and "damageTypes" in rad_body,
      "the inference must read the armor's radiation damage-type resistance")

policy = text(POLICY) if POLICY.is_file() else ""
check("constexpr bool IsConfiguredMask(" in policy, "RadCorrectnessPolicy must expose IsConfiguredMask")
check("return listed || IsGasMask(fillsMouth, hasKeyword);" in body(policy, "constexpr bool IsConfiguredMask("),
      "a listed armor must bypass the slot rule and the vanilla rule must still apply otherwise")
check("constexpr bool IsRadShieldedMask(" in policy, "RadCorrectnessPolicy must expose IsRadShieldedMask")

header = text(HEADER) if HEADER.is_file() else ""
check("_extraMaskKeywords" in header and "_extraMasks" in header,
      "the engine must keep the configured keywords and armors as members")
check("void  LoadMaskConfig();" in header, "RadiationEngine must declare LoadMaskConfig")

if failures:
    print("mask extras contract failures:")
    for failure in failures:
        print("  FAIL", failure)
    raise SystemExit(1)

print("Extra-mask contract verified: INI entries, native resolution and the policy split")
