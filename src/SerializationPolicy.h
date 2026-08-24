#pragma once

#include <cmath>
#include <initializer_list>
#include <span>

namespace RadiationSerialization
{
	inline constexpr double kNoPreviousVanillaRadstorm = -1.0e30;

	struct StormClockState
	{
		double playSeconds;
		double nextStormPlaySeconds;
		double lastRadstormSeconds;
		bool   scheduled;
	};

	enum class StormClockCandidateDisposition
	{
		kRestore,
		kAmbiguous
	};

	[[nodiscard]] inline bool IsValidStormClock(const StormClockState& a_state)
	{
		if (!std::isfinite(a_state.playSeconds) || !std::isfinite(a_state.nextStormPlaySeconds) ||
			!std::isfinite(a_state.lastRadstormSeconds) || a_state.playSeconds < 0.0) {
			return false;
		}

		if (a_state.scheduled && a_state.nextStormPlaySeconds < 0.0) {
			return false;
		}

		return a_state.lastRadstormSeconds == kNoPreviousVanillaRadstorm ||
		       (a_state.lastRadstormSeconds >= 0.0 && a_state.lastRadstormSeconds <= a_state.playSeconds);
	}

	[[nodiscard]] inline StormClockCandidateDisposition ClassifyStormClockCandidates(
		std::span<const StormClockState> a_candidates)
	{
		if (a_candidates.empty()) {
			return StormClockCandidateDisposition::kAmbiguous;
		}

		for (const auto& candidate : a_candidates) {
			if (candidate.playSeconds != a_candidates.front().playSeconds ||
				candidate.nextStormPlaySeconds != a_candidates.front().nextStormPlaySeconds ||
				candidate.lastRadstormSeconds != a_candidates.front().lastRadstormSeconds ||
				candidate.scheduled != a_candidates.front().scheduled) {
				return StormClockCandidateDisposition::kAmbiguous;
			}
		}
		return StormClockCandidateDisposition::kRestore;
	}

	[[nodiscard]] inline StormClockCandidateDisposition ClassifyStormClockCandidates(
		std::initializer_list<StormClockState> a_candidates)
	{
		return ClassifyStormClockCandidates(std::span<const StormClockState>{ a_candidates.begin(), a_candidates.size() });
	}
}
