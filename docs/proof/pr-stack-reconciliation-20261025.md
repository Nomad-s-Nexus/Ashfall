# PR stack reconciliation, 2026-10-25

Repository: https://git.nomadicinteractive.dev/PRISMA/LargePluginMods

## TLDR

The four open Rad Overhaul PRs form one stack, `main` -> #66 -> #68 -> #70 -> #71. The branch
bases are already correct. The remaining divergence was the `commonlib_NonVR` pin: `main` and
#66 carried different revisions from #68/#70/#71. #66 now pins the same revision as the rest of
the stack, so the landing order no longer changes which CommonLib `main` ends up with.

| PR | Base | Head | Exact-head CI |
|---|---|---|---|
| 66 | `main` | `6728153` + this reconciliation | run 28 PASS on `6728153` |
| 68 | #66 branch | `cc5c523` | run 29 PASS |
| 70 | #68 branch | `aa0b8b0` | run 30 PASS |
| 71 | #70 branch | `8f4b30c` | run 31 PASS |

Runs 28-31 all completed on 2026-10-04 against the literal branch heads. A build result is not
gameplay acceptance; every PR still carries its own runtime gate.

## CommonLib pin

| Ref | Pin before | Pin after | Checkout |
|---|---|---|---|
| `main` | `6baa8006` (2026-08-23) | unchanged until #66 lands | - |
| #66 | `1b2f8583` | `ed383ff9` | `COMMONLIBF4_PREVIOUS_PATH` -> `COMMONLIBF4_PATH` |
| #68/#70/#71 | `ed383ff9` | unchanged | `COMMONLIBF4_PATH` |

`ed383ff9` is a superset of `1b2f8583`: both carry the AE `CreateExplosionAtLocation` id
`2192306` fix (`961358dd` + one line); `ed383ff9` adds the survival bindings and the public
`BGSInventoryInterface` event sources that #70/#71 need. #66 does not need the additions but is
compatible with them, and the alternative - landing #66 on the stale `6baa8006` - would have put
a pre-AE-fix CommonLib on `main` and then handed the pin back and forth on every later merge.

The release workflow in #66 was still the older bash/`xwin` pipeline while #68/#70/#71 use the
PowerShell/MSVC pipeline that runs the same seven contract tests. #66 now uses the MSVC
pipeline, so the release path is identical across the stack.

## Remaining landing-order conflicts

Landing #66 then #68 still conflicts in two files. Both conflicts are additive and are resolved
by taking #68's side for those hunks only - never a wholesale ours/theirs resolution.

1. `.github/workflows/rad-overhaul-build.yml` - #68 adds eleven climate/HUD contract invocations
   and three climate policy executables to the test lists. Those tests do not exist in #66 and
   must not be added there. Resolve by keeping #68's expanded lists.
2. `Rad-Overhaul-Fallout-4/docs/code-notes/Rad-Overhaul-Fallout-4.md` - every line #66 adds to
   this document is already present in #68's version, so the conflict is positional only.
   Resolve by keeping #68's document.

The pin hunk no longer conflicts, which was the only conflict that could have silently changed
runtime behavior.

## Not changed

- No force-push, no retarget, no merge, no PR closure.
- No gameplay code, no tuning, no penalties.
- PR #70 is not refreshed onto #68 until #68 is final, and #71 not until #70 is final.

## Verification

- `git merge-tree` dry run of `main` + #66 + #68 reports conflicts only in the two files above.
- Both workflow files parse as YAML and keep their pinned revision consistent with the
  `COMMONLIBF4_PATH` checkout the runner already provides for #68/#70/#71.
- `tests/verify_ci_deploy.py` still passes, so the CI deployment guard is unchanged.
