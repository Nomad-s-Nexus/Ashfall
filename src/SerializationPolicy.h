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

	struct FilterState
	{
		float charge;
		float condition;
	};

	struct PenaltyState
	{
		float carry;
		float heal;
		float speed;
		float ap;
	};

	inline constexpr float kMaxPersistedPenalty = 10000.0f;

	enum class StormClockCandidateDisposition
	{
		kRestore,
		kAmbiguous
	};

	enum class FilterCandidateDisposition
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

	[[nodiscard]] inline bool IsValidFilterState(const FilterState& a_state)
	{
		return std::isfinite(a_state.charge) && a_state.charge >= 0.0f && a_state.charge <= 100.0f &&
		       std::isfinite(a_state.condition) && a_state.condition >= 0.0f && a_state.condition <= 100.0f;
	}

	[[nodiscard]] inline bool IsValidPenaltyState(const PenaltyState& a_state)
	{
		const auto ok = [](float a_value) {
			return std::isfinite(a_value) && a_value >= 0.0f && a_value <= kMaxPersistedPenalty;
		};
		return ok(a_state.carry) && ok(a_state.heal) && ok(a_state.speed) && ok(a_state.ap);
	}

	[[nodiscard]] inline bool RestoredCarryPenaltyHeld(float a_restoredCarryPen, float a_total, float a_permanent)
	{
		if (a_restoredCarryPen <= 0.0f) {
			return true;
		}
		if (!std::isfinite(a_total) || !std::isfinite(a_permanent)) {
			return false;
		}
		return (a_permanent - a_total) >= a_restoredCarryPen * 0.5f;
	}

	[[nodiscard]] inline float CarryLeakRepairDelta(float a_total, float a_permanent)
	{
		if (!std::isfinite(a_total) || !std::isfinite(a_permanent) || a_permanent <= 0.0f || a_total >= 0.0f) {
			return 0.0f;
		}
		return a_permanent - a_total;
	}

	[[nodiscard]] inline bool PenaltyCandidatesAgree(std::span<const PenaltyState> a_candidates)
	{
		if (a_candidates.empty()) {
			return false;
		}
		for (const auto& candidate : a_candidates) {
			if (candidate.carry != a_candidates.front().carry || candidate.heal != a_candidates.front().heal ||
				candidate.speed != a_candidates.front().speed || candidate.ap != a_candidates.front().ap) {
				return false;
			}
		}
		return true;
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

	[[nodiscard]] inline FilterCandidateDisposition ClassifyFilterCandidates(
		std::span<const FilterState> a_candidates)
	{
		if (a_candidates.empty()) {
			return FilterCandidateDisposition::kAmbiguous;
		}

		for (const auto& candidate : a_candidates) {
			if (candidate.charge != a_candidates.front().charge ||
				candidate.condition != a_candidates.front().condition) {
				return FilterCandidateDisposition::kAmbiguous;
			}
		}
		return FilterCandidateDisposition::kRestore;
	}

	[[nodiscard]] inline FilterCandidateDisposition ClassifyFilterCandidates(
		std::initializer_list<FilterState> a_candidates)
	{
		return ClassifyFilterCandidates(std::span<const FilterState>{ a_candidates.begin(), a_candidates.size() });
	}
}
