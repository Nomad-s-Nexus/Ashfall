#include "RadEngine_Internal.h"

using namespace RadInternal;

void RadiationEngine::EnsureEslLoaded()
{
    if (_eslChecked) return;
    _eslChecked = true;

    auto logger = Log();
    logger->info("========== ESL RESOLUTION ({}) ==========", kEslName);

    auto* dh = RE::TESDataHandler::GetSingleton();
    if (!dh)
    {
        logger->error("[ESL] TESDataHandler singleton is NULL — cannot resolve forms.");
        logger->warn("[ESL] -> Falling back to C++ SPECIAL penalties (no Pipboy display).");
        logger->info("==========================================");
        return;
    }
    logger->debug("[ESL] TESDataHandler @ 0x{:X}", reinterpret_cast<uintptr_t>(dh));

    const auto lightIdx = dh->GetLoadedLightModIndex(kEslName);
    const auto fullIdx  = dh->GetLoadedModIndex(kEslName);
    if (lightIdx.has_value())
        logger->warn("[ESL] '{}' present as LIGHT/ESL plugin, light index 0x{:03X} (runtime FE{:03X}) — "
                     "it will load early and LOSE conflicts; ship it as a full ESP instead.",
            kEslName, lightIdx.value(), lightIdx.value());
    else if (fullIdx.has_value())
        logger->info("[ESL] '{}' present as FULL plugin, mod index 0x{:02X} (intended — loads last, wins conflicts).",
            kEslName, fullIdx.value());
    else
        logger->error("[ESL] '{}' is NOT in the load order — not active or not deployed to Data.", kEslName);

    int found = 0;
    for (int i = 0; i < kStageCount; ++i)
    {
        const uint32_t rawID = kPerkBaseFormID + i;
        RE::TESForm* form = dh->LookupForm(rawID, kEslName);

        if (!form)
        {
            logger->error("[ESL]   perk[{}] raw 0x{:06X}: LookupForm returned NULL", i, rawID);
            _stagePerks[i] = nullptr;
            continue;
        }

        if (form->GetFormType() != RE::ENUM_FORM_ID::kPERK)
        {
            logger->error("[ESL]   perk[{}] raw 0x{:06X}: resolved form is type {} (NOT a PERK)",
                i, rawID, static_cast<int>(form->GetFormType()));
            _stagePerks[i] = nullptr;
            continue;
        }

        _stagePerks[i] = static_cast<RE::BGSPerk*>(form);
        ++found;
        logger->debug("[ESL]   perk[{}] raw 0x{:06X} -> runtime 0x{:08X} \"{}\"",
            i, rawID, _stagePerks[i]->GetFormID(), PerkName(_stagePerks[i]));
    }


    {
        RE::TESForm* fl = dh->LookupForm(kRadWeatherListID, kEslName);
        _radWeatherList = (fl && fl->GetFormType() == RE::ENUM_FORM_ID::kFLST)
            ? static_cast<RE::BGSListForm*>(fl) : nullptr;
        logger->info("[ESL] Rad-weather FormList (0x{:03X}): {}", kRadWeatherListID,
            _radWeatherList ? "resolved (auto-mask-equip enabled)" : "NOT found (auto-equip disabled)");
    }

    {
        RE::TESForm* f = dh->LookupForm(kGasMaskFilterID, kEslName);
        _filterItem = (f && f->GetFormType() == RE::ENUM_FORM_ID::kMISC)
            ? static_cast<RE::TESBoundObject*>(f) : nullptr;
        logger->info("[ESL] Gas-mask filter (0x{:03X}): {}", kGasMaskFilterID,
            _filterItem ? "resolved (filter system enabled)" : "NOT found (filter system disabled)");
    }

    // A mask must carry this keyword as well as filling the mouth slot. Without it, mask protection
    {
        RE::TESForm* f = dh->LookupForm(kGasMaskKeywordID, kEslName);
        _gasMaskKeyword = (f && f->GetFormType() == RE::ENUM_FORM_ID::kKYWD)
            ? static_cast<RE::BGSKeyword*>(f) : nullptr;
        logger->info("[ESL] Gas-mask keyword (0x{:03X}): {}", kGasMaskKeywordID,
            _gasMaskKeyword ? "resolved (keyword-plus-mouth-slot protection enabled)"
                            : "NOT found (mask protection and auto-equip disabled)");
    }

    {
        RE::TESForm* f = dh->LookupForm(kGasMaskRepairKitID, kEslName);
        _repairKitItem = (f && f->GetFormType() == RE::ENUM_FORM_ID::kMISC)
            ? static_cast<RE::TESBoundObject*>(f) : nullptr;
        logger->info("[ESL] Gas-mask repair kit (0x{:03X}): {}", kGasMaskRepairKitID,
            _repairKitItem ? "resolved" : "NOT found (repair disabled)");
    }

    {
        RE::TESForm* gForm = dh->LookupForm(kTissueGlobalID, kEslName);
        _tissueGlobal = (gForm && gForm->GetFormType() == RE::ENUM_FORM_ID::kGLOB)
            ? static_cast<RE::TESGlobal*>(gForm) : nullptr;
        if (_tissueGlobal)
        {
            _tissueDamage = _tissueGlobal->GetValue();
            logger->info("[ESL] Tissue-damage global (0x{:03X}) resolved; loaded {:.1f}", kTissueGlobalID, _tissueDamage);
        }
        else
        {
            logger->warn("[ESL] Tissue-damage global (0x{:03X}) NOT found; tissue damage disabled.", kTissueGlobalID);
        }
    }

    {
        RE::TESForm* gForm = dh->LookupForm(kPermTissueGlobalID, kEslName);
        _permanentTissueGlobal = (gForm && gForm->GetFormType() == RE::ENUM_FORM_ID::kGLOB)
            ? static_cast<RE::TESGlobal*>(gForm) : nullptr;
        if (_permanentTissueGlobal)
        {
            _permanentTissueDamage = _permanentTissueGlobal->GetValue();
            logger->info("[ESL] Permanent-tissue global (0x{:03X}) resolved; loaded {:.1f}", kPermTissueGlobalID, _permanentTissueDamage);
        }
        else
        {
            logger->warn("[ESL] Permanent-tissue global (0x{:03X}) NOT found; chelation system disabled.", kPermTissueGlobalID);
        }
    }

    {
        RE::TESForm* qForm = dh->LookupForm(kStormFXQuestFormID, kEslName);
        _stormFXQuest = (qForm && qForm->GetFormType() == RE::ENUM_FORM_ID::kQUST)
            ? static_cast<RE::TESQuest*>(qForm) : nullptr;
        logger->info("[ESL] Storm FX quest (0x{:03X}): {}", kStormFXQuestFormID,
            _stormFXQuest ? "resolved (camera shake via Papyrus stage 10/20)" : "NOT found");
    }

    EnsureQuestRunning(_stormFXQuest);

    {
        RE::TESForm* hForm = dh->LookupForm(0x1A7C6A, "Fallout4.esm");
        _fireHazardForm = (hForm && hForm->GetFormType() == RE::ENUM_FORM_ID::kHAZD)
            ? static_cast<RE::BGSHazard*>(hForm) : nullptr;
        logger->info("[ESL] Fire hazard (DN121FireHazard 0x1A7C6A): {}",
            _fireHazardForm ? "resolved (firestorm ring spawn enabled)" : "NOT found");
    }

    {
        RE::TESForm* xForm = dh->LookupForm(0x886, kEslName);
        _nukeExplosionForm = (xForm && xForm->GetFormType() == RE::ENUM_FORM_ID::kEXPL)
            ? static_cast<RE::BGSExplosion*>(xForm) : nullptr;
        logger->info("[ESL] Firestorm distant-strike form (0x886): {}.",
            _nukeExplosionForm ? "resolved" : "MISSING");
    }


    for (int i = 0; i < kStormTypeCount && i < kStormTypeTableSize; ++i)
    {
        const uint32_t wid = kStormTypes[i].weatherID;
        RE::TESForm* wForm = dh->LookupForm(wid, kEslName);
        _stormWeathers[i] = (wForm && wForm->GetFormType() == RE::ENUM_FORM_ID::kWTHR)
            ? static_cast<RE::TESWeather*>(wForm) : nullptr;
        logger->info("[ESL] Storm weather '{}' (0x{:03X}): {}", kStormTypes[i].name, wid,
            _stormWeathers[i] ? "resolved" : "NOT found (runs without forced sky)");

        if (auto* w = _stormWeathers[i])
        {
            const StormType& s = kStormTypes[i];
            logger->info("[StormFog] '{}' was near={:.0f} far={:.0f} pow={:.2f} hRange={:.0f} hiDens={:.2f} -> near={:.0f} far={:.0f} pow={:.2f} hRange={:.0f} hiDens={:.2f}",
                s.name, w->fogData[0], w->fogData[1], w->fogData[4], w->fogData[9], w->fogData[12],
                s.fogNear, s.fogFar, s.fogPower, s.fogHeightRange, s.fogHiDensity);
            ApplyWeatherFog(w, s.fogNear, s.fogFar, s.fogPower,
                s.fogHeightMid, s.fogHeightRange, s.fogHiDensity);

            if (i == kDustStormIndex)
            {
                RE::TESForm* veForm = dh->LookupForm(0x1EB2D6, "Fallout4.esm");
                if (veForm && veForm->GetFormType() == RE::ENUM_FORM_ID::kRFCT)
                {
                    w->referenceEffect = static_cast<RE::BGSReferenceEffect*>(veForm);
                    logger->info("[StormFX] Dust Storm camera-dust VisualEffect attached (FXCameraAttachExplosionDustVE).");
                }
                else
                {
                    logger->warn("[StormFX] Dust Storm camera-dust VE (0x1EB2D6) not found (type={}).",
                        veForm ? static_cast<int>(veForm->GetFormType()) : -1);
                }

                RE::TESForm* aoForm = dh->LookupForm(0x0156CB, "DLCNukaWorld.esm");
                if (!aoForm || aoForm->GetFormType() != RE::ENUM_FORM_ID::kARTO)
                    aoForm = dh->LookupForm(0x1EB2D5, "Fallout4.esm");   // FXCameraAttachExplosionDust
                _dustBurstArt = (aoForm && aoForm->GetFormType() == RE::ENUM_FORM_ID::kARTO)
                    ? static_cast<RE::BGSArtObject*>(aoForm) : nullptr;
                logger->info("[StormFX] Dust wall-arrival burst art: {}.",
                    _dustBurstArt ? "resolved" : "NOT found");

                static constexpr std::uint32_t kDustyImageSpaces[8] = {
                    0x1F61A2,  // [0] Sunrise      CW_DustyDAWN
                    0x1F61A5,  // [1] Day          CW_DustyDAY
                    0x1F61A6,  // [2] Sunset       CW_DustyDUSK
                    0x1F61A9,  // [3] Night        CW_DustyNIGHT
                    0x1F61A3,  // [4] EarlySunrise CW_DustyDAWN_E
                    0x1F61A4,  // [5] LateSunrise  CW_DustyDAWN_L
                    0x1F61A7,  // [6] EarlySunset  CW_DustyDUSK_E
                    0x1F61A8,  // [7] LateSunset   CW_DustyDUSK_L
                };
                int isSet = 0;
                for (int k = 0; k < 8; ++k)
                {
                    RE::TESForm* isf = dh->LookupForm(kDustyImageSpaces[k], "Fallout4.esm");
                    if (isf && isf->GetFormType() == RE::ENUM_FORM_ID::kIMGS)
                    {
                        w->imageSpace[k] = static_cast<RE::TESImageSpace*>(isf);
                        ++isSet;
                    }
                }
                logger->info("[StormFX] Dust Storm imagespaces repointed to CommonwealthDusty brown grades: {}/8.", isSet);
            }

            if (const char* auroraPath = w->aurora.GetModel(); auroraPath && auroraPath[0] != '\0')
            {
                logger->info("[StormFX] '{}' aurora was '{}' -> cleared (cleaner sky, matches dust).",
                    s.name, auroraPath);
                w->aurora.SetModel("");
            }

            // (RVA 0x650917) does `rand % (ThunderFreq * ucThunderCount)` for ANY weather that
            constexpr int kThunderFreqIdx = static_cast<int>(RE::TESWeather::WeatherData::kThunderFreq);
            if (w->weatherData[kThunderFreqIdx] == 0)
            {
                w->weatherData[kThunderFreqIdx] = 100;
                logger->info("[StormFix] '{}' ThunderFreq was 0 (engine div-by-zero in Sky::UpdateSound) -> set 100.",
                    kStormTypes[i].name);
            }

            // Sky::UpdateWeather (RVA 0x64E530): currentWeatherPct ramps over a duration of
            constexpr int   kTransDeltaIdx = static_cast<int>(RE::TESWeather::WeatherData::kTransDelta);
            constexpr std::int8_t kStormTransDelta = 24;   // brisk but visibly gradual sky roll-in
            const std::int8_t wasTrans = w->weatherData[kTransDeltaIdx];
            w->weatherData[kTransDeltaIdx] = kStormTransDelta;
            logger->info("[StormFix] '{}' TransDelta {} -> {} (uniform, tunable sky roll-in speed).",
                kStormTypes[i].name, static_cast<int>(wasTrans), static_cast<int>(kStormTransDelta));

            // WIND (decompile-verified from Sky::UpdateWind, RVA 0x650190 — see README-Weather.md).
            constexpr int kWindSpeedIdx = static_cast<int>(RE::TESWeather::WeatherData::kWindSpeed);
            constexpr int kWindTurbIdx  = static_cast<int>(RE::TESWeather::WeatherData::kWindTurbulence);
            constexpr int kWindDirIdx   = static_cast<int>(RE::TESWeather::WeatherData::kWindDir);
            constexpr int kWindDirRngIdx= static_cast<int>(RE::TESWeather::WeatherData::kWindDirRange);
            auto asByte = [](int v) { return static_cast<std::int8_t>(static_cast<std::uint8_t>(v)); };

            int windSpeed, windTurb;
            switch (kStormTypes[i].themeId)
            {
            case 5: windSpeed = 220; windTurb = 200; break;  // Dust Storm  — howling, gusty
            case 4: windSpeed = 200; windTurb = 180; break;  // Ash Fallout — blowing ash
            case 1: windSpeed = 200; windTurb = 175; break;  // Rad Firestorm
            case 2: windSpeed = 150; windTurb = 140; break;  // Toxic Sulfur Storm
            case 3: windSpeed = 135; windTurb = 130; break;  // Acid Rain   — driving rain
            default: windSpeed = 170; windTurb = 150; break;
            }
            w->weatherData[kWindSpeedIdx]  = asByte(windSpeed);
            w->weatherData[kWindTurbIdx]   = asByte(windTurb);
            w->weatherData[kWindDirIdx]    = asByte(64);   // base heading (deterministic, not garbage)
            w->weatherData[kWindDirRngIdx] = asByte(96);   // moderate direction jitter
            logger->info("[StormFix] '{}' wind authored: speed={} turbulence={} (was dead calm).",
                kStormTypes[i].name, windSpeed, windTurb);

            // CAMERA-EFFECT TIMING (decompile-verified from Sky::UpdateEffects, RVA 0x655280 — see
            constexpr int kBeginEffectIdx = static_cast<int>(RE::TESWeather::WeatherData::kBeginEffect);
            constexpr int kEndEffectIdx   = static_cast<int>(RE::TESWeather::WeatherData::kEndEffect);
            w->weatherData[kBeginEffectIdx] = asByte(40);   // ~0.16: dust motes appear early in the roll-in
            w->weatherData[kEndEffectIdx]   = asByte(100);  // ~0.39: outgoing dust tears down as the storm clears
            logger->info("[StormFix] '{}' camera-VE thresholds authored (begin=40 end=100).",
                kStormTypes[i].name);
        }
    }

    {
        RE::TESForm* f = dh->LookupForm(0x0002B52A, "Fallout4.esm");
        _clearWeather = (f && f->GetFormType() == RE::ENUM_FORM_ID::kWTHR)
            ? static_cast<RE::TESWeather*>(f) : nullptr;
        logger->info("[ESL] Commonwealth clear weather (0x0002B52A): {}",
            _clearWeather ? "resolved (new-game clear-weather safety)" : "NOT found");
    }

    {
        _dustWeathers.clear();
        struct DustW { std::uint32_t local; const char* plugin; const char* name; };
        static constexpr DustW kDustWeathers[] = {
            { 0x1F61A1, "Fallout4.esm",                   "CommonwealthDusty" },
            { 0x00638C, "Gas Masks of the Wasteland.esp", "DustyLite" },
            { 0x003D78, "Gas Masks of the Wasteland.esp", "DustyCity" },
            { 0x0035DE, "Gas Masks of the Wasteland.esp", "DustyCommonwealth" },
            { 0x00638A, "Gas Masks of the Wasteland.esp", "DustyHeavy" },
            { 0x014209, "Gas Masks of the Wasteland.esp", "DustyExtreme" },
            { 0x015142, "Gas Masks of the Wasteland.esp", "DustyExtremeCopy" },
        };
        for (const auto& d : kDustWeathers)
        {
            RE::TESForm* f = dh->LookupForm(d.local, d.plugin);
            if (f && f->GetFormType() == RE::ENUM_FORM_ID::kWTHR)
                _dustWeathers.insert(f->GetFormID());
        }
        logger->info("[ESL] Dust-storm weathers (GMW + vanilla): {} resolved (suppressed at new-game startup).",
            _dustWeathers.size());
    }

    // co-save (SaveState/LoadState), so we must NOT re-roll it here. Doing so on every first tick
    {
        RE::TESForm* gForm = dh->LookupForm(kNextEmissionGlobalID, kEslName);
        _nextEmissionGlobal = (gForm && gForm->GetFormType() == RE::ENUM_FORM_ID::kGLOB)
            ? static_cast<RE::TESGlobal*>(gForm) : nullptr;
        logger->info("[ESL] Storm system gate global (0x{:03X}): {}", kNextEmissionGlobalID,
            _nextEmissionGlobal ? "resolved (storms enabled)" : "NOT found (storms disabled)");
    }

    {
        RE::TESForm* gForm = dh->LookupForm(kForceStormGlobalID, kEslName);
        _forceStormGlobal = (gForm && gForm->GetFormType() == RE::ENUM_FORM_ID::kGLOB)
            ? static_cast<RE::TESGlobal*>(gForm) : nullptr;
        logger->info("[ESL] Force-storm debug global (0x{:03X}): {}", kForceStormGlobalID,
            _forceStormGlobal ? "resolved ('set RadOverhaul_ForceStorm to 1-5')" : "NOT found");
    }

    for (int i = 0; i < kStormTypeCount; ++i)
    {
        const uint32_t imadId = kStormImadBase + static_cast<uint32_t>(i);
        RE::TESForm* f = dh->LookupForm(imadId, kEslName);
        _stormIMADs[i] = (f && f->GetFormType() == RE::ENUM_FORM_ID::kIMAD)
            ? static_cast<RE::TESImageSpaceModifier*>(f) : nullptr;
        logger->info("[ESL] Storm IMAD '{}' (0x{:03X}): {}", kStormTypes[i].name, imadId,
            _stormIMADs[i] ? "resolved" : "NOT found (runs without engine color grading)");
    }

    {
        RE::TESForm* f = dh->LookupForm(0x0758AE, "Fallout4.esm");
        _stormGhoulForm = (f && f->GetFormType() == RE::ENUM_FORM_ID::kLVLN)
            ? static_cast<RE::TESBoundObject*>(f) : nullptr;
        logger->info("[ESL] Feral ghoul leveled list (LCharFeralGhoul 0x0758AE): {}",
            _stormGhoulForm ? "resolved" : "NOT found (no ghoul spawns)");
    }
    {
        RE::TESForm* f = dh->LookupForm(0x0D39EE, "Fallout4.esm");
        _stormGlowingOneForm = (f && f->GetFormType() == RE::ENUM_FORM_ID::kLVLN)
            ? static_cast<RE::TESBoundObject*>(f) : nullptr;
        logger->info("[ESL] Glowing One leveled list (LCharFeralGhoulGlowingOne 0x0D39EE): {}",
            _stormGlowingOneForm ? "resolved" : "NOT found (no glowing one spawns)");
    }
    {
        RE::TESForm* f = dh->LookupForm(kActorTypeGhoulKeyword, "Fallout4.esm");
        _ghoulKeyword = (f && f->GetFormType() == RE::ENUM_FORM_ID::kKYWD)
            ? static_cast<RE::BGSKeyword*>(f) : nullptr;
        logger->info("[ESL] ActorTypeGhoul keyword (0x{:06X}): {}", kActorTypeGhoulKeyword,
            _ghoulKeyword ? "resolved (ghoul NPCs healed, not harmed, by storms)" : "NOT found");
    }
    {
        RE::TESForm* f = dh->LookupForm(0x4D8A1, "Fallout4.esm");
        _armorTypePowerKeyword = (f && f->GetFormType() == RE::ENUM_FORM_ID::kKYWD)
            ? static_cast<RE::BGSKeyword*>(f) : nullptr;
        logger->info("[ESL] ArmorTypePower keyword (0x4D8A1): {}",
            _armorTypePowerKeyword ? "resolved (full PA suit fully blocks rads)" : "NOT found");
    }

    {
        RE::TESForm* f = dh->LookupForm(0x000337F3, "Fallout4.esm");
        _workshopNPCFaction = (f && f->GetFormType() == RE::ENUM_FORM_ID::kFACT)
            ? static_cast<RE::TESFaction*>(f) : nullptr;
        logger->info("[ESL] WorkshopNPC faction (0x000337F3): {}",
            _workshopNPCFaction ? "resolved (settlers spared from storm peak)" : "NOT found");
    }

    _eslAvailable = (found == kStageCount);
    if (_eslAvailable)
        logger->info("[ESL] SUCCESS: {}/{} stage perks resolved. Pipboy PERKS/STATUS path ACTIVE.",
            found, kStageCount);
    else
        logger->warn("[ESL] PARTIAL: {}/{} perks resolved. Using C++ SPECIAL fallback (no Pipboy display).",
            found, kStageCount);
    logger->info("==========================================");

    // Constraint: mask extras re-resolve here, where the data handler is proven ready, in case init ran early.
    LoadMaskConfig();
}
