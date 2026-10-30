# NPC outbreak system: plan

Status: plan only. Nothing here is implemented. Source: the Discord outbreak design pasted by the user on 2026-10-30.
Scope is NPCs in general (settlers, companions, wanderers), not settlers alone.

## What exists today

- `deploy/Scripts/Source/User/RadSurvivalDiseases.psc` is player-only. It accrues a 0..1 risk
  (`AccrueRisk`, `AddRisk`), starts a disease at a threshold (`StartDisease`), advances it by stage
  (`AdvanceDisease`, `ApplySpell`, `ApplyPerk`) and cures it (`CureDisease`, `TryRestCure`).
  Per-source risk is tunable (RiskRain, RiskSwim, RiskTaintedWater and so on) through the `[Tuning]` table.
- There is no settler or NPC needs or disease mechanic in the mod. The only NPC state is the auto-mask set
  (`src/RadiationEngine.h:_autoMaskedNpcs`) and companion handling in `src/RadHudCompanion.cpp`.
- Settler workshop jobs, beds and sleeping are vanilla (workshop scripts and AI packages). The mod does not touch them.

## Design (from the request)

Three outbreak phases, ordered by how bad they are and how easy they are to stop:

| Phase | Name | Spread | NPC effect | Player-visible sign |
|---|---|---|---|---|
| 1 | Early | low | none, a little ill | occasional cough / sneeze; outline with the detector item |
| 2 | Progress | high | NPCs at stage 2 stop working and lie in beds | coughing, in bed, outline |
| 3 | Final | high | NPCs may die | as phase 2 plus a death warning message |

Two items:

1. **Detector** (the "see infected" item): while equipped or used, infected NPCs get a coloured outline
   (colour by stage) and a short range scan.
2. **Medicines**: an antidote, an antibiotic, a general medicine. Each cures or prevents at a different strength
   (cheap items prevent / cure phase 1, strong items are needed for phase 2 and 3).

One workshop object: a **doctor station**. Assign NPCs to it. Each assigned doctor has a tier. Prevention and
treatment strength scale with the tier:

- Tier 0 (regular settler, no training): a slight prevention modifier only.
- Each tier above adds a fixed percentage to both treatment and prevention.

Audio: coughing and sneezing for NPCs and for the player, generated with ElevenLabs.

## Systems and how each could work

### 1. Infection state per NPC

Per-actor state needs: infected (bool), stage 0..3, hours in stage, immune-until time. Options:

- **Alias or ActiveMagicEffect on the actor** (recommended). A disease Spell with an ActiveMagicEffect script keeps
  stage and timers on the actor, so state is saved with the actor and cleaned up on death or removal. No global
  table to leak.
- Quest-side array of actors: simpler to write but needs periodic pruning and breaks when actors are deleted.

The NPC set that can be infected: workshop settlers, followers, and wasteland NPCs near the player. To stay cheap, run
the outbreak tick only on loaded actors within a radius, plus workshop settlers on their own slow tick.

### 2. Spread and phase control

- A global outbreak level (0..3) per workshop settlement and one for the wasteland. An outbreak starts when
  a source triggers (a tainted-water event, a spawn from a rad storm, a debug command).
- Spread chance per game hour between NPCs within a short range, scaled by the phase multiplier and reduced by
  prevention (doctor tier, medicine, masks).
- Reuse the existing risk model: infection is an exposure event that adds risk, and the existing tunable table
  gets keys for the new rates so the debug panel can adjust them.

### 3. Stage effects

- Stage 1: sign effects only (cough / sneeze sound, optional outline).
- Stage 2: apply a vanilla AI package that sends the NPC to bed and keeps them there while sick. Workshop
  assignment must be restored on recovery. This is the riskiest part: settler AI packages interact with the
  workshop assignment system, and it needs an in-game proof before building the rest.
- Stage 3: damage over time that can kill; protected/essential NPCs (quest NPCs, companions with the essential flag) are
  excluded by default, with a toggle for companions.

### 4. Detector item

- Outline: an Image Space Modifier or the vanilla "highlight" shader applied to the actor (`EffectShader` on the
  actor). Needs an in-engine test for the shader pass color per stage and for the distance cut-off.
- Alternatives if shaders are limited: a HUD marker through the PrismaUI overlay (a world-to-screen marker over each
  infected NPC). More work in C++, but it avoids dependence on shaders.

### 5. Medicines and doctor station

- Medicines: three `ALCH` records with an effect that cures or immunises the target (use on the player, or hand to a
  settler through a dialogue / activation). NPC dosing is the open question below.
- Doctor station: a workshop furniture form. Assignment uses the vanilla workshop assign flow, and the station's
  script reads the assigned actor's tier.
- Tier source: no vanilla stat is "doctor". Candidate: tier from a perk rank or a custom actor value set by a
  recruitment or training item. Needs a decision.

### 6. Audio

Required clips (all NPC and player variants as separate short files):

- cough: 4 variants, quiet and loud
- sneeze: 3 variants
- wet cough (stage 2/3): 3 variants
- groan / labored breathing (stage 3): 2 variants
- death rattle: 1 variant

Voice-sets differ by sex. Plan for male and female sets (2x). Total about 30 clips. Generation with ElevenLabs
is not started: no text-to-speech request or external call is made until the user approves it. Clips become `.xwm`
or `.wav` sound descriptors, played with the same `Play2DSound` / 3D sound route the mod already uses for the
choking cough.

## Milestones

1. **Proof of concept (gate for everything else).** One infected settler: spells state, sneeze sound, sent to bed at
   stage 2 and returned to work on recovery. This answers the AI package risk. Without this proof the rest is
   guesswork.
2. State on the actor (ActiveMagicEffect), spread tick, phase control, debug commands (`outbreak:start`, `outbreak:stage`).
3. Detector item and outline (shader or HUD marker, chosen after test).
4. Medicines and doctor station, tier handling.
5. Audio set and sounds.
6. Tuning keys in the debug panel (spread rate, stage durations, death toggle, companion protection), contracts and tests.

## Decisions (answered 2026-10-30)

1. **Who can die:** any NPC. Companions do not die: they fall ill and are out of action until they heal.
   NPCs marked by quests are ignored entirely.
2. **Doctor tier:** open. Vanilla already has doctor NPCs, so the tier should come from them rather than a new stat.
   Proposal pending the user's reply: a hand-kept list of vanilla doctor NPCs gets a fixed tier, every other
   assigned settler is tier 0.
3. **Treatment:** the player can give medicine to an NPC, or an assigned doctor heals them.
4. **Detector:** a PrismaUI icon over infected NPCs (HUD marker), not a shader outline.
5. **Audio:** generate with the ElevenLabs connector. A one-clip test was generated first (see below) before the full set.

## Risks

- Settler AI: forcing a bed package can fight the workshop job assignment. Needs the milestone 1 proof.
- Performance: per-NPC ticks across a big settlement. Mitigation: slow tick, loaded actors only.
- Save compatibility: new records and a new spell on actors. Disabling the system must clear effects cleanly.
- No in-game verification exists for any of this yet. Keep the whole feature behind one toggle, default off.

## Out of scope for the first cut

Quest content, new settlements, and balancing vanilla item values. Items and the doctor station reuse existing
models until art exists.
