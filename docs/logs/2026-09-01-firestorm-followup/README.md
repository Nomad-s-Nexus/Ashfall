# Firestorm follow-up runtime evidence

This focused excerpt comes from the Alpha 2 runtime session on 2026-09-01. It records the firestorm, the distant nuke event, the weather handoff, and the attempted clear/endstorm commands.

Findings:

- The captured return weather was `0x1C3D5E`, which was then reported as an incoming vanilla radstorm after the managed firestorm ended.
- `weather:clear` and `endstorm` began another smooth handoff and the session ended before the target reached stable clear weather.
- The nuke log recorded distance and height but not the final strike coordinates or player-facing rotation, so the visual placement symptom could not be audited from the old log alone.
- No Rad crash dump was produced for the reported hard freeze. The follow-up fix therefore adds deterministic debug clear handling and richer nuke placement logging, but still requires a fresh in-game traversal test.

The excerpt is intentionally limited to this test window rather than copying the accumulated FalloutAnomaly.log.
