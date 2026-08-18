#include "RadEngine_Internal.h"

using namespace RadInternal;

// Chronic tissue damage: accrual, permanent scarring, cures, radiation stages, SPECIAL/AV penalties, symptoms.
// Split out of RadiationEngine.cpp (was ~4,300 lines). Shared constants/helpers live in
// RadEngine_Internal.h; the class itself is declared in RadiationEngine.h.


void RadiationEngine::ApplyDebuffs(uint32_t radLevel)
{
    auto logger = Log();
    auto player = RE::PlayerCharacter::GetSingleton();

    if (!player)
    {
        logger->debug("[ApplyDebuffs] Player not available");
        return;
    }

    // Get the stage config for this rad level. A null stage normally can't happen (the shipped
    // thresholds.json defines a "0" Safe stage) — but if the config was user-edited to remove it,
    // clear any lingering stage perk/penalties instead of leaving the last stage applied forever.
    const auto* stage = _config->GetStageConfig(radLevel);
    if (!stage)
    {
        if (_currentStageLevel != 0)
            logger->warn("[ApplyDebuffs] No stage config for rad level {} — clearing stage effects.", radLevel);
        _currentStageLevel = 0;
        UpdateStagePerk(-1);
        ApplyStatModification(-1);
        return;
    }

    // Stage threshold at 100-rad intervals: 0 (safe), 100, 200, ... 1000 (capped).
    uint32_t stageThreshold = (radLevel / 100) * 100;
    if (stageThreshold > 1000) stageThreshold = 1000;

    // Drive the companion-ESL stage perk (Pipboy PERKS + STATUS visibility).
    // stageIndex: 100->0, 200->1, ... 1000->9; below 100 -> -1 (clear).
    const int stageIndex = (stageThreshold >= 100) ? static_cast<int>(stageThreshold / 100) - 1 : -1;

    if (stageThreshold != _currentStageLevel)
    {
        // Report the penalties ACTUALLY applied (kStagePenalties). The json endPenalty/strPenalty
        // values are design leftovers the DLL deliberately does not use — logging them misled tuning.
        const int endPen = (stageIndex >= 0) ? kStagePenalties[stageIndex].end : 0;
        const int strPen = (stageIndex >= 0) ? kStagePenalties[stageIndex].str : 0;
        _currentStageLevel = stageThreshold;
        logger->warn("========== RADIATION STAGE CHANGED ==========");
        logger->warn("  Radiation Level: {} rad", radLevel);
        logger->warn("  Stage: {}", stage->name);
        logger->warn("  Applied Penalties: END -{} / STR -{}", endPen, strPen);
        logger->warn("===========================================");
    }

    UpdateStagePerk(stageIndex);
    // Screen tint is an HTML overlay (radhud.html #radtint); the old engine-IMAD tint path was
    // removed (too subtle/opaque to tune). Record 0x830 remains in the ESP, unused.

    // Apply SPECIAL penalties (DLL-driven, floor-guarded). The ESL stage perks are LABEL-ONLY
    // (their abilities carry no SPECIAL effect) — this is the single source of the actual penalty.
    ApplyStatModification(stageIndex);
}




void RadiationEngine::UpdateStagePerk(int stageIndex)
{
    EnsureEslLoaded();
    if (!_eslAvailable) return;            // fallback path handles SPECIAL in ApplyStatModification
    if (stageIndex == _appliedPerkIndex) return;

    auto logger = Log();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player)
    {
        logger->warn("[ESL] UpdateStagePerk: player singleton NULL; deferring perk swap.");
        return;
    }

    logger->info("[ESL] Stage perk transition: index {} -> {}", _appliedPerkIndex, stageIndex);

    // Defensive removal: strip EVERY stage perk the player currently has, not just
    // the last one we think we applied. This prevents accumulation (and SPECIAL
    // stacking) if a previous removal didn't take, or state desynced on load.
    for (int i = 0; i < kStageCount; ++i)
    {
        if (i == stageIndex) continue;             // keep the target
        RE::BGSPerk* p = _stagePerks[i];
        if (p && player->GetPerkRank(p) > 0)
        {
            player->RemovePerk(p);
            logger->debug("[ESL]   REMOVE \"{}\" (0x{:08X}) -> rank now {}",
                PerkName(p), p->GetFormID(), player->GetPerkRank(p));
        }
    }

    // Add the new stage perk (its ability applies SPECIAL penalties + STATUS entry).
    if (stageIndex >= 0 && stageIndex < kStageCount && _stagePerks[stageIndex])
    {
        RE::BGSPerk* now = _stagePerks[stageIndex];
        if (player->GetPerkRank(now) == 0)
        {
            player->AddPerk(now, 1);
        }
        const uint8_t rankAfter = player->GetPerkRank(now);
        logger->info("[ESL]   ADD \"{}\" (0x{:08X}) rank now {} {}",
            PerkName(now), now->GetFormID(), rankAfter,
            rankAfter > 0 ? "[ok — visible in PERKS + STATUS]" : "[WARN AddPerk did not stick]");
    }
    else if (stageIndex < 0)
    {
        logger->info("[ESL]   No active radiation stage — all stage perks cleared (cured).");
    }

    _appliedPerkIndex = stageIndex;
}


void RadiationEngine::ApplyRestTissueHeal(float gameHoursElapsed, float currentRads)
{
    // Bulk-apply the reducible-tissue regression for time spent sleeping/waiting/fast-travelling.
    // Only the live Update() time-jump path is authoritative. OnPostLoad resets _prevGameDays to -1
    // before its legacy load-to-load elapsed-time check, so reject that call here: comparing two
    // different save timestamps does NOT prove the player rested and could grant duplicate/free healing.
    if (_prevGameDays < 0.0f) return;
    if (!_tissueGlobal || gameHoursElapsed <= 0.0f) return;
    if (currentRads >= kTissueHealBelow) return;
    if (_tissueDamage <= _permanentTissueDamage) return;

    const float heal = gameHoursElapsed * kRestTissueHealPerGameHour;
    if (heal > 0.0f)
    {
        Log()->info("[Rest] {:.1f} game-hours elapsed -> healing {:.1f} tissue.",
            gameHoursElapsed, heal);
        HealTissue(heal, "rest");
    }
}


void RadiationEngine::HealTissue(float amount, const char* source)
{
    if (amount <= 0.0f || !_tissueGlobal) return;
    float before = _tissueDamage;
    // Cannot heal below the permanent tissue floor — those injuries need chelation.
    _tissueDamage = std::max(_permanentTissueDamage, _tissueDamage - amount);
    float actual = before - _tissueDamage;
    if (actual <= 0.0f) return;
    _tissueGlobal->value = _tissueDamage;
    Log()->info("[TissueCure:{}] -{:.1f} ({:.1f} -> {:.1f}, perm floor {:.1f})",
        source, actual, before, _tissueDamage, _permanentTissueDamage);
}


void RadiationEngine::HealPermanentTissue(float amount, const char* source)
{
    if (amount <= 0.0f || !_permanentTissueGlobal) return;
    float before = _permanentTissueDamage;
    _permanentTissueDamage = std::max(0.0f, _permanentTissueDamage - amount);
    _permanentTissueGlobal->value = _permanentTissueDamage;
    // Also lower total tissue to match the dropped floor.
    if (_tissueGlobal && _tissueDamage > _permanentTissueDamage)
    {
        _tissueDamage -= (before - _permanentTissueDamage);
        if (_tissueDamage < _permanentTissueDamage) _tissueDamage = _permanentTissueDamage;
        _tissueGlobal->value = _tissueDamage;
    }
    Log()->info("[PermCure:{}] -{:.1f} perm ({:.1f} -> {:.1f})",
        source, before - _permanentTissueDamage, before, _permanentTissueDamage);
}


void RadiationEngine::CheckActiveCures(float currentRads, float currentHP, bool deltaValid)
{
    // The 4 Hz hook pauses through sleep/wait/fast-travel. Keep a pre-jump snapshot here because this
    // function is called every normal tick; when Update flags deltaValid=false, the snapshot tells us
    // whether the mask was actually worn and exposed immediately before time jumped. This avoids using
    // the destination state (which can differ after fast travel) and keeps time-jump drain consistent
    // with the normal rule: filters drain only while worn and not sheltered/PA/protected.
    {
        static bool  s_filterJumpInit = false;
        static float s_prevFilterGameDays = 0.0f;
        static bool  s_prevFilterMasked = false;
        static bool  s_prevFilterExposed = false;

        auto* player = RE::PlayerCharacter::GetSingleton();
        const float nowDays = GameDays();
        const bool maskedNow = player && IsWearingGasMask();
        const bool exposedNow = player && !IsPlayerSheltered() && !IsPlayerInPoweredShelter() &&
                                !IsPlayerInPowerArmor();

        // A load/new-game resets the cure baselines to -1. Re-seed the time-jump snapshot too so a
        // different save's game clock or mask state can never be mistaken for elapsed gameplay.
        const bool reseed = !s_filterJumpInit || _prevRads < 0.0f || _prevHP < 0.0f || nowDays <= 0.0f;
        if (!reseed && !deltaValid && nowDays > s_prevFilterGameDays &&
            _filterItem && s_prevFilterMasked && s_prevFilterExposed)
        {
            const float gameHours = (nowDays - s_prevFilterGameDays) * 24.0f;
            float timeScale = 0.0f;
            if (auto* cal = RE::Calendar::GetSingleton(); cal && cal->timeScale)
                timeScale = cal->timeScale->GetValue();

            if (gameHours > 0.0f && timeScale > 0.0f && _filterDurationSec > 0)
            {
                // FO4 timescale is game-seconds / real-second. Convert the skipped game-hours back to
                // equivalent real seconds, then apply the same percentage-per-real-second drain used by
                // the live 4 Hz path. Custom timescales therefore stay correct.
                const float realSeconds = gameHours * 3600.0f / timeScale;
                const float drainPct = realSeconds * (100.0f / static_cast<float>(_filterDurationSec));
                if (drainPct > 0.0f)
                {
                    const float before = _filterCharge;
                    int replacementsUsed = 0;

                    if (drainPct < _filterCharge)
                    {
                        _filterCharge -= drainPct;
                    }
                    else
                    {
                        // The current filter was exhausted during the skipped interval. If automatic
                        // replacement is enabled, consume enough spare filters to simulate the same
                        // continuous behavior the live tick path would have produced. Otherwise stop at 0.
                        const float remainingAfterCurrent = drainPct - _filterCharge;
                        _filterCharge = 0.0f;

                        if (_filterAutoReplace && player)
                        {
                            const int spares = FilterItemCount();
                            const int needed = static_cast<int>(std::floor(remainingAfterCurrent / 100.0f)) + 1;
                            replacementsUsed = std::min(spares, std::max(0, needed));
                            if (replacementsUsed > 0)
                            {
                                RE::TESObjectREFR::RemoveItemData data(_filterItem, replacementsUsed);
                                player->RemoveItem(data);
                                _filterCharge = std::clamp(
                                    static_cast<float>(replacementsUsed) * 100.0f - remainingAfterCurrent,
                                    0.0f, 100.0f);
                            }
                        }
                    }

                    _filterLowWarned = 0;  // re-evaluate low/depleted messaging on the next live tick
                    Log()->info(
                        "[Filter/TimeJump] {:.1f} game-hours @ timescale {:.1f} = {:.1f}s real; "
                        "charge {:.1f}% -> {:.1f}% (auto replacements {}).",
                        gameHours, timeScale, realSeconds, before, _filterCharge, replacementsUsed);
                }
            }
            else if (gameHours > 0.0f && timeScale <= 0.0f)
            {
                Log()->warn("[Filter/TimeJump] timescale unavailable/non-positive; skipped filter drain.");
            }
        }

        s_filterJumpInit = true;
        s_prevFilterGameDays = nowDays;
        s_prevFilterMasked = maskedNow;
        s_prevFilterExposed = exposedNow;
    }

    // PERMANENT (scarred) tissue: cured ONLY by the chelation compound, detected RELIABLY by a drop
    // in the player's inventory count of that item — NOT by stat deltas. The old delta heuristics
    // false-fired (a big RadAway + stimpak in one tick looked like a "doctor visit" and wiped all
    // scarring; so did sleeping). This runs every tick (consumption can happen anytime).
    if (_chelationItem)
    {
        if (auto* player = RE::PlayerCharacter::GetSingleton())
        {
            const std::uint32_t count = player->GetInventoryObjectCount(_chelationItem);
            if (_chelationCountInit && count < _prevChelationCount)
            {
                // Consumed at least one chelation compound — flush permanent + regular tissue.
                HealPermanentTissue(_permanentTissueDamage, "chelation");
                HealTissue(_tissueDamage, "chelation");
            }
            _prevChelationCount = count;
            _chelationCountInit = true;
        }
    }

    // The rest is delta-based and must be skipped across a time jump and on the first tick
    // (-1 sentinels = no previous tick recorded yet).
    if (!deltaValid) return;
    if (_prevRads < 0.0f || _prevHP < 0.0f) return;

    const float radDrop = _prevRads - currentRads;
    const float hpGain  = currentHP - _prevHP;

    // RadAway: a moderate per-tick rads drop heals REGULAR (reducible) tissue only — low stakes
    // (it re-accrues), so a delta heuristic is acceptable here. Floors at the permanent level.
    if (radDrop > 15.0f && hpGain < 20.0f)
        HealTissue(radDrop * 0.12f, "radaway");
}


float RadiationEngine::UpdateTissueDamage(float particulateRads)
{
    // No global -> tissue system disabled; severity is just particulate rads.
    if (!_tissueGlobal)
        return particulateRads;

    // Accumulate from exposure (proportional to current particulate rads), or heal
    // slowly when rads are low. Tissue damage is NOT cleared by RadAway.
    if (particulateRads > kTissueHealBelow)
    {
        // DEATH SPIRAL: a body already wrecked by radiation absorbs further damage faster, so accrual
        // scales with how damaged it already is. Letting tissue run away now genuinely compounds.
        const float sev = std::clamp(_tissueDamage / kTissueMax, 0.0f, 1.0f);
        _tissueDamage += particulateRads * kTissueGainRate * (1.0f + sev * kTissueSpiral);
    }
    else
    {
        _tissueDamage -= kTissueHealRate;
    }

    // Clamp tissue to [permanent floor, max].
    if (_tissueDamage < _permanentTissueDamage) _tissueDamage = _permanentTissueDamage;
    if (_tissueDamage > kTissueMax)             _tissueDamage = kTissueMax;

    // Permanent scarring: chronic exposure above threshold slowly scars tissue. The rate scales
    // with how high tissue is, so heavy sustained exposure scars much faster. Only cleared by
    // chelation compound or doctor.
    if (_permanentTissueGlobal && _tissueDamage > kPermAccrualThresh)
    {
        _permanentTissueDamage += kPermAccrualRate * (_tissueDamage / kPermAccrualThresh);
        if (_permanentTissueDamage > _tissueDamage) _permanentTissueDamage = _tissueDamage;
        _permanentTissueGlobal->value = _permanentTissueDamage;
    }

    // Persist into the globals so they survive save/load.
    _tissueGlobal->value = _tissueDamage;

    // Effective severity combines particulate + a weighted tissue floor.
    return particulateRads + _tissueDamage * kTissueWeight;
}


void RadiationEngine::ApplyStatModification(int stageIndex)
{
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;
    auto* actorValue = RE::ActorValue::GetSingleton();
    if (!actorValue) return;

    // Penalty table lives in the header (kStagePenalties) so the ApplyDebuffs stage banner can
    // report the values actually applied here.
    int wantEnd = (stageIndex >= 0 && stageIndex < kStageCount) ? kStagePenalties[stageIndex].end : 0;
    int wantStr = (stageIndex >= 0 && stageIndex < kStageCount) ? kStagePenalties[stageIndex].str : 0;

    // PERMANENT SCARRING's own consequence: lost ENDURANCE = lost MAX HP you cannot heal, rest off or
    // RadAway away. Only the chelation compound (or a doctor) gives it back — which is what finally
    // makes the cure worth hunting. Scarring previously had ZERO gameplay effect. Computed separately
    // from the stage penalty because Fix Stats must NOT suspend it (see below).
    int scarEnd = 0;
    if (_permanentTissueDamage > 0.0f)
    {
        const float scar01 = std::clamp(_permanentTissueDamage / kTissueMax, 0.0f, 1.0f);
        scarEnd = static_cast<int>(std::lround(kScarEndMax * scar01));
    }

    // Manual Fix Stats suspension: if the player cleared their SPECIAL and is still at the same stage,
    // don't re-apply the STAGE-BASED radiation penalty (want -> 0). Any stage change clears the
    // suspension and resumes normal behavior: recovering a stage means they healed; climbing a stage
    // means they chose not to cure. The permanent SCAR penalty is deliberately NOT suspended — it is the
    // un-healable consequence that makes the chelation cure worth hunting, and Fix Stats is not a cure.
    if (_specialSuspendStage >= 0)
    {
        if (stageIndex == _specialSuspendStage)
        {
            wantEnd = 0;
            wantStr = 0;
        }
        else
        {
            Log()->info(
                "[FixStats] stage changed {} -> {}; resuming SPECIAL penalty.",
                _specialSuspendStage, stageIndex);
            _specialSuspendStage = -1;
        }
    }

    // Fold the scar END in AFTER the suspension so it always applies (Fix Stats can't clear it). Still a
    // single END modifier through ApplyClampedSpecial, so the floor guard can never be bypassed.
    wantEnd += scarEnd;

    ApplyClampedSpecial(player, actorValue->endurance, wantEnd, _appliedEndPenalty);
    ApplyClampedSpecial(player, actorValue->strength,  wantStr, _appliedStrPenalty);
}


// Floor-guarded tracked penalty for a non-SPECIAL float AV. Same contract as ApplyClampedSpecial:
// clamp against the PERMANENT base so our modifier can never drive the AV negative in any gear state,
// and hold the delta in `applied` so it is re-derived (not stacked) each tick. A small dead-band stops
// per-tick modifier churn as tissue creeps.
void RadiationEngine::ApplyTrackedAV(RE::PlayerCharacter* player, RE::ActorValueInfo* av,
                                     float wantPenalty, float& applied)
{
    if (!player || !av) return;
    const float perm    = player->GetPermanentActorValue(*av);
    const float desired = std::clamp(wantPenalty, 0.0f, std::max(0.0f, perm));
    if (std::fabs(desired - applied) < 0.5f) return;   // dead-band
    player->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, *av, applied - desired);
    applied = desired;
}


// Chronic tissue damage now has real teeth. Called every tick.
void RadiationEngine::ApplyTissueEffects()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* av     = RE::ActorValue::GetSingleton();
    if (!player || !av || !_tissueGlobal) return;

    const float t = std::clamp(_tissueDamage / kTissueMax, 0.0f, 1.0f);

    // LAYER 1 — chronic wasting. Floors at the permanent scar automatically (tissue can't heal below it).
    ApplyTrackedAV(player, av->carryWeight,  kTissueCarryMax * t, _appliedCarryPen);
    ApplyTrackedAV(player, av->healRateMult, kTissueHealMax  * t, _appliedHealPen);

    // Sluggishness and fatigue only bite once you are genuinely sick (ramp from their thresholds).
    const float spd = (t > kTissueSpeedFrom) ? (t - kTissueSpeedFrom) / (1.0f - kTissueSpeedFrom) : 0.0f;
    const float fat = (t > kTissueApFrom)    ? (t - kTissueApFrom)    / (1.0f - kTissueApFrom)    : 0.0f;
    ApplyTrackedAV(player, av->speedMult,    kTissueSpeedMax * spd, _appliedSpeedPen);
    ApplyTrackedAV(player, av->actionPoints, kTissueApMax    * fat, _appliedApPen);

    // LAYER 2 — visceral symptoms.
    UpdateTissueSymptoms(t);
}


// Cough -> nausea -> vomiting, layered by tissue severity. Native only (sounds + notify + HP), so this
// is safe off the worker-thread Update hook — no Papyrus.
void RadiationEngine::UpdateTissueSymptoms(float t)
{
    if (t < kSymCoughFrom)
    {
        _symptomWarned = 0;   // recovered below the symptom floor — re-arm the escalation notices
        return;
    }
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* av     = RE::ActorValue::GetSingleton();
    if (!player) return;

    // One notify per escalation, not per fit.
    if (t >= kSymVomitFrom && _symptomWarned < 2)
    {
        Notify("You double over retching - the sickness is deep in you now.", true);
        _symptomWarned = 2;
    }
    else if (t >= kSymNauseaFrom && t < kSymVomitFrom && _symptomWarned < 1)
    {
        Notify("A wave of nausea rolls through you.", true);
        _symptomWarned = 1;
    }

    if (--_symptomTick > 0) return;
    // Fits come faster the sicker you are.
    static std::mt19937 rng{ std::random_device{}() };
    const int span = kSymMaxTicks - kSymMinTicks;
    _symptomTick = kSymMinTicks +
        std::uniform_int_distribution<int>(0, std::max(1, static_cast<int>(span * (1.0f - t))))(rng);

    const bool female = IsPlayerFemale();
    RE::BSSoundHandle h{};
    // Reuse the gas-mask choking VO: it is a gag/retch, which is exactly the sound of these fits.
    Play2DSound(female ? kSndChokeNoMaskF : kSndChokeNoMaskM, h);

    // A vomiting fit actually costs you: you lose HP as you retch — but a symptom must never KILL. These
    // fits fire regardless of shelter (chronic sickness doesn't stop indoors), so an unfloored hit could
    // down a low-HP player standing safely inside. Skip the hit if it would drop below a survivable floor.
    if (t >= kSymVomitFrom && av && av->health &&
        player->GetActorValue(*av->health) - kSymVomitHp >= kSymVomitFloorHp)
        player->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *av->health, -kSymVomitHp);
}


void RadiationEngine::ApplyClampedSpecial(RE::PlayerCharacter* player, RE::ActorValueInfo* av,
                                          int wantPenalty, std::int32_t& applied)
{
    if (!player || !av) return;

    // Clamp the penalty against the PERMANENT base (GetPermanentActorValue excludes ALL temporary
    // modifiers — both our penalty AND equipped-gear/chem bonuses). This is the key to being
    // gear-safe AND corruption-proof:
    //   * penalty <= perm - 1, so perm - penalty >= 1 ALWAYS -> our modifier can never floor the stat
    //     in ANY gear state (gear only ADDS on top), so equipping/removing END gear never bakes a
    //     leaked modifier into the base.
    //   * it is independent of gear, so it doesn't need recomputing when gear changes (no flooring-
    //     inference, which the old `GetActorValue + applied` math got wrong the instant gear came off).
    // Safe failure: if GetPermanentActorValue ever reads bogus (<1), maxPenalty is 0 -> no penalty
    // applied (the stat is left untouched) rather than damaged.
    const float perm       = player->GetPermanentActorValue(*av);
    const int   maxPenalty = std::max(0, static_cast<int>(perm) - 1);   // never drive the base below 1
    const int   desired    = std::min(wantPenalty, maxPenalty);

    if (desired != applied)
    {
        const float delta = static_cast<float>(applied - desired);   // +ve raises the AV, -ve lowers
        player->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, *av, delta);
        Log()->debug("[SPECIAL] penalty {} -> {} (perm {:.0f}, delta {:+.0f})",
            applied, desired, perm, delta);
        applied = desired;
    }
}


void RadiationEngine::RemoveStatModification()
{
    auto player = RE::PlayerCharacter::GetSingleton();
    auto* actorValue = RE::ActorValue::GetSingleton();
    if (!player || !actorValue) return;

    // Restore any applied SPECIAL penalties back to zero.
    if (actorValue->endurance && _appliedEndPenalty != 0)
    {
        player->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, *actorValue->endurance, static_cast<float>(_appliedEndPenalty));
        _appliedEndPenalty = 0;
    }
    if (actorValue->strength && _appliedStrPenalty != 0)
    {
        player->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, *actorValue->strength, static_cast<float>(_appliedStrPenalty));
        _appliedStrPenalty = 0;
    }
}


void RadiationEngine::DispelNegativeSpecials()
{
    // Force-heal SPECIAL corruption: clear any stat driven BELOW the game's floor of 1 (a 0/negative
    // reading — the reported "-10"). A real SPECIAL is ALWAYS >= 1, so a sub-floor value is always a
    // leaked/baked/foreign negative modifier, never a legitimate debuff — so this can only ever act on
    // a genuinely broken stat and can't clobber an intended temporary effect. Restores it to the
    // permanent base. Runs automatically on every load so a player who arrives with wrecked SPECIAL is
    // healed without needing the debug menu; the manual Fix Stats button does a full reset of all 7.
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* av     = RE::ActorValue::GetSingleton();
    if (!player || !av) return;

    struct AV { RE::ActorValueInfo* info; const char* name; };
    const AV specials[] = {
        { av->strength, "STR" }, { av->perception, "PER" }, { av->endurance, "END" },
        { av->charisma, "CHA" }, { av->intelligence, "INT" }, { av->agility, "AGI" }, { av->luck, "LCK" },
    };
    auto logger = Log();
    int fixed = 0;
    for (const auto& s : specials)
    {
        if (!s.info) continue;
        const float total = player->GetActorValue(*s.info);
        if (total >= 1.0f) continue;                          // healthy — leave it
        const float perm = player->GetPermanentActorValue(*s.info);
        if (perm < 1.0f) continue;                            // base read itself is bogus — don't guess
        player->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, *s.info, perm - total);  // raise to base
        logger->warn("[DispelNeg] {} was {:.0f} (base {:.0f}) — restored to base.", s.name, total, perm);
        ++fixed;
    }
    if (fixed > 0)
    {
        _appliedEndPenalty = 0;   // re-arm our floor-guarded rad penalty trackers so they re-evaluate
        _appliedStrPenalty = 0;
        char m[80];
        std::snprintf(m, sizeof(m), "Restored %d corrupted SPECIAL stat%s.", fixed, fixed == 1 ? "" : "s");
        Notify(m, false);
    }
}
