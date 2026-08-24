# Rad priority correctness Implementation Plan

For agentic workers: execute task-by-task with executing-plans. Steps use checkbox syntax for tracking.

Goal: remove the four audited high-priority correctness failures without expanding unrelated storm or radiation behavior.

Architecture: keep policy decisions independent of Fallout runtime objects so they can run in a native test binary. Engine-facing code adapts player state, F4SE messages, references, and plugin forms to those policies. The plugin owns an explicit gas-mask keyword and native code resolves it with the other Rad Overhaul forms.

Tech Stack: C++23, xmake 3, commonlib_NonVR, F4SE, FO4RecordEditor, Fallout 4 OG 1.10.163.

## Global Constraints

- Preserve OG-only runtime rejection and use the verified main-thread dispatcher for every engine mutation.
- Back up RadiationOverhaul.esp before its first edit, then read edited records back and run check_plugin.
- Require both FARO_GasMask and the mouth biped slot. Do not broadly classify all mouth-slot armor.
- Never infer Chelation or RadAway use from inventory-count movement.
- If a source-identifying consumption event is unavailable, cure detection fails closed rather than restoring the unsafe inference.
- Keep build, static checks, plugin checks, and in-game OG acceptance as separate evidence.

---

### Task 1: Add a pure correctness-policy target

Files:
- Create: src/RadCorrectnessPolicy.h
- Create: tests/correctness_policy_tests.cpp
- Modify: xmake.lua

Interfaces:
- Produces Position, DomePosition, IsGasMask, CureSignal, ArmsRadAwayHealing, and IsCurrentLoadGeneration in the RadCorrectnessPolicy namespace.
- Produces the standalone rad_correctness_policy_tests target.

- [ ] Step 1: Write failing tests.

    using namespace RadCorrectnessPolicy;
    Require(DomePosition({10.0f, 20.0f, 30.0f}, 6000.0f) == Position{10.0f, 20.0f, 6030.0f});
    Require(IsGasMask(true, true));
    Require(!IsGasMask(true, false));
    Require(!CuresPermanentTissue(CureSignal::kInventoryMovement));
    Require(CuresPermanentTissue(CureSignal::kVerifiedChelationUse));
    Require(!ArmsRadAwayHealing(CureSignal::kInventoryMovement));
    Require(ArmsRadAwayHealing(CureSignal::kVerifiedRadAwayUse));
    Require(!IsCurrentLoadGeneration(42, 43));

- [ ] Step 2: Verify RED.

Run xmake build rad_correctness_policy_tests. Expected: compilation fails because the policy header does not exist.

- [ ] Step 3: Implement the policy header and target.

    namespace RadCorrectnessPolicy {
    struct Position { float x; float y; float z; auto operator==(const Position&) const -> bool = default; };
    constexpr Position DomePosition(Position player, float zOffset) { return {player.x, player.y, player.z + zOffset}; }
    constexpr bool IsGasMask(bool fillsMouth, bool hasKeyword) { return fillsMouth && hasKeyword; }
    enum class CureSignal { kNone, kInventoryMovement, kVerifiedChelationUse, kVerifiedRadAwayUse };
    constexpr bool CuresPermanentTissue(CureSignal signal) { return signal == CureSignal::kVerifiedChelationUse; }
    constexpr bool ArmsRadAwayHealing(CureSignal signal) { return signal == CureSignal::kVerifiedRadAwayUse; }
    constexpr bool IsCurrentLoadGeneration(std::uint64_t expected, std::uint64_t actual) { return expected == actual; }
    }

Add an xmake binary that compiles only the test source and does not link plugin sources.

- [ ] Step 4: Verify GREEN and commit.

Run xmake build rad_correctness_policy_tests and then ./build/tests/rad_correctness_policy_tests. Expected: exit 0.

    git add xmake.lua src/RadCorrectnessPolicy.h tests/correctness_policy_tests.cpp
    git commit -m "test(rad): add correctness policies"

### Task 2: Define an explicit gas-mask policy

Files:
- Modify: deployed RadiationOverhaul.esp through FO4RecordEditor
- Modify: src/RadEngine_Internal.h
- Modify: src/RadiationEngine.h
- Modify: src/RadiationEngine.cpp
- Modify: src/RadEngine_Mask.cpp
- Test: tests/correctness_policy_tests.cpp

Interfaces:
- Consumes RadCorrectnessPolicy::IsGasMask.
- Produces RadiationEngine::IsRecognizedGasMask(const RE::TESObjectARMO*) const and _gasMaskKeyword.

- [ ] Step 1: Back up and inspect the plugin.

Resolve RadiationOverhaul.esp in FO4RecordEditor, inspect existing records, and run backup_plugin before writes. If it is absent from the editor profile, configure that editor session for the Rad Overhaul deployment and repeat the read-only inspection.

- [ ] Step 2: Add and read back the keyword.

Create KYWD FARO_GasMask in RadiationOverhaul.esp. Read it back, record the returned local FormID in kGasMaskKeywordID, then resolve it in EnsureEslLoaded.

- [ ] Step 3: Patch only known protective armors.

Inventory candidate ARMO records. Add the keyword only to verified respiratory protective masks, read each changed Keywords list back, and preserve every bandana, scarf, cloth wrap, and cosmetic mouth armor unchanged.

- [ ] Step 4: Replace mouth-slot-only classification.

    bool RadiationEngine::IsRecognizedGasMask(const RE::TESObjectARMO* armor) const {
        return armor && RadCorrectnessPolicy::IsGasMask(
            armor->FillsBipedSlot(static_cast<std::int32_t>(RE::BIPED_OBJECT::kMouth)),
            _gasMaskKeyword && armor->HasKeyword(_gasMaskKeyword));
    }

Use it in worn detection and inventory auto-equip. A missing keyword fails closed and logs the disabled protection.

- [ ] Step 5: Save, validate, test, and commit.

Run save_plugin, get_record for the keyword and each patched armor, and check_plugin. Then run xmake build rad_correctness_policy_tests and ./build/tests/rad_correctness_policy_tests.

    git add deploy/RadiationOverhaul.esp src/RadEngine_Internal.h src/RadiationEngine.h src/RadiationEngine.cpp src/RadEngine_Mask.cpp
    git commit -m "fix(rad): require gas mask keyword"

### Task 3: Follow the player with each storm-dome layer

Files:
- Modify: src/RadiationEngine.h
- Modify: src/RadEngine_Storm.cpp
- Test: tests/correctness_policy_tests.cpp

Interfaces:
- Consumes RadCorrectnessPolicy::DomePosition.
- Produces RadiationEngine::UpdateStormDomePosition().

- [ ] Step 1: Add an offset regression case and verify RED.

    Require(DomePosition({-50.0f, 25.0f, 100.0f}, 6400.0f) == Position{-50.0f, 25.0f, 6500.0f});

Run xmake build rad_correctness_policy_tests and ./build/tests/rad_correctness_policy_tests. Expected: failure until arbitrary vertical offsets are represented.

- [ ] Step 2: Preserve layer offsets.

Replace the raw dome-handle vector with StormDomeLayer containing RE::ObjectRefHandle handle and float zOffset. Record kStormDomeZOffset for cloud layers and the extra light-node offsets for light layers. Reset follower state in spawn and cleanup.

- [ ] Step 3: Update at one hertz.

Add _stormDomeFollowTick and UpdateStormDomePosition(). Every fourth engine tick while active and exposed, set each live reference's position to DomePosition(playerPosition, zOffset). Skip expired handles and never move cleared or sheltered domes.

- [ ] Step 4: Verify and commit.

Run xmake build rad_correctness_policy_tests, ./build/tests/rad_correctness_policy_tests, and xmake build FalloutAnomalyRadiation. Expected: both targets exit 0.

    git add src/RadiationEngine.h src/RadEngine_Storm.cpp src/RadCorrectnessPolicy.h tests/correctness_policy_tests.cpp
    git commit -m "fix(rad): follow player with storm dome"

### Task 4: Gate tissue cures on verified use

Files:
- Modify: src/Hooks.h
- Modify: src/Hooks.cpp
- Modify: src/RadiationEngine.h
- Modify: src/RadEngine_Tissue.cpp
- Test: tests/correctness_policy_tests.cpp

Interfaces:
- Consumes RadCorrectnessPolicy::CureSignal.
- Produces RadiationEngine::NotifyVerifiedCureUse(CureSignal).

- [ ] Step 1: Verify the engine contract before writing the hook.

Inspect the selected commonlib_NonVR event or function and its OG Address Library contract. The callback must establish player identity and identify Chelation versus vanilla RadAway. If that cannot be demonstrated, delete the unsafe count-delta branches and leave cure state unarmed.

- [ ] Step 2: Add the failing event-policy test.

    Require(!CuresPermanentTissue(CureSignal::kInventoryMovement));
    Require(!ArmsRadAwayHealing(CureSignal::kInventoryMovement));
    Require(CuresPermanentTissue(CureSignal::kVerifiedChelationUse));
    Require(ArmsRadAwayHealing(CureSignal::kVerifiedRadAwayUse));

Run xmake build rad_correctness_policy_tests and ./build/tests/rad_correctness_policy_tests. Expected: failure before the selected event maps to a verified cure signal.

- [ ] Step 3: Route verified use only.

Implement NotifyVerifiedCureUse. Chelation clears permanent and reducible tissue only for kVerifiedChelationUse. RadAway opens the bounded radiation-drop window only for kVerifiedRadAwayUse. Delete every inventory snapshot and count-less-than-previous-count cure branch.

- [ ] Step 4: Verify and commit.

Run xmake build rad_correctness_policy_tests, ./build/tests/rad_correctness_policy_tests, and a grep that finds no count-less-than-previous-count cure pattern in src/RadEngine_Tissue.cpp.

    git add src/Hooks.h src/Hooks.cpp src/RadiationEngine.h src/RadEngine_Tissue.cpp src/RadCorrectnessPolicy.h tests/correctness_policy_tests.cpp
    git commit -m "fix(rad): gate tissue cures on item use"

### Task 5: Invalidate stale load-sensitive tasks

Files:
- Modify: src/main.cpp
- Test: tests/correctness_policy_tests.cpp

Interfaces:
- Consumes RadCorrectnessPolicy::IsCurrentLoadGeneration.
- Produces guarded post-load and new-game queued lambdas.

- [ ] Step 1: Add and verify stale-generation tests.

    Require(IsCurrentLoadGeneration(42, 42));
    Require(!IsCurrentLoadGeneration(42, 43));

Run xmake build rad_correctness_policy_tests and ./build/tests/rad_correctness_policy_tests. Expected: failure before equality is enforced.

- [ ] Step 2: Capture a shared engine-load generation.

Add g_radLoadGeneration. Capture it at kPostLoadGame and kNewGame. Check it at the start of every resulting main-thread lambda before ResetDebugMenuForSession, OnPostLoad, OnNewGame, ResetAppliedPenalties, or HUD waiter startup. Advance it in kPreLoadGame before HUD waiter invalidation.

- [ ] Step 3: Verify and commit.

Run xmake build rad_correctness_policy_tests, ./build/tests/rad_correctness_policy_tests, and xmake build FalloutAnomalyRadiation. Expected: both targets exit 0.

    git add src/main.cpp src/RadCorrectnessPolicy.h tests/correctness_policy_tests.cpp
    git commit -m "fix(rad): invalidate stale load tasks"

### Task 6: Verify boundaries and document evidence

Files:
- Modify: docs/CHANGELOG.md
- Modify: docs/STATUS.md

- [ ] Step 1: Run full static and native verification.

Run xmake build rad_correctness_policy_tests, ./build/tests/rad_correctness_policy_tests, xmake build FalloutAnomalyRadiation, and a grep that finds no count-less-than-previous-count cure pattern. Expected: both builds and test binary exit 0.

- [ ] Step 2: Verify plugin records.

Read back FARO_GasMask and every changed ARMO with FO4RecordEditor, then run check_plugin. Confirm no unapproved cosmetic armor gained the keyword.

- [ ] Step 3: Record remaining runtime acceptance.

Document source, build, and plugin evidence separately from pending Windows OG checks: dome movement during approach, bandana versus approved mask, drop/sell/transfer of both cure items, actual consumption of both cure items, and reload while old work is queued.

- [ ] Step 4: Commit verification documentation.

    git add docs/CHANGELOG.md docs/STATUS.md
    git commit -m "docs(rad): record correctness verification"

