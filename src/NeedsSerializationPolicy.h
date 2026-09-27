#pragma once

#include "NeedsPolicy.h"

#include <cmath>
#include <cstdint>
#include <optional>

namespace RadNeedsSerialization
{
    inline constexpr std::uint32_t kRecord =
        static_cast<std::uint32_t>('N') |
        (static_cast<std::uint32_t>('E') << 8) |
        (static_cast<std::uint32_t>('E') << 16) |
        (static_cast<std::uint32_t>('D') << 24);
    inline constexpr std::uint32_t kVersion = 1;
    inline constexpr std::uint32_t kPayloadSize = sizeof(float) * 3;

    struct PersistedState
    {
        float hunger01{ 0.0f };
        float thirst01{ 0.0f };
        float sleepDebt01{ 0.0f };
    };

    [[nodiscard]] inline PersistedState Snapshot(const RadNeedsPolicy::NeedsState& state) noexcept
    {
        const auto safe = RadNeedsPolicy::Sanitize(state);
        return { safe.hunger01, safe.thirst01, safe.sleepDebt01 };
    }

    [[nodiscard]] inline bool IsValid(const PersistedState& state) noexcept
    {
        const auto valid01 = [](float value) {
            return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
        };
        return valid01(state.hunger01) &&
               valid01(state.thirst01) &&
               valid01(state.sleepDebt01);
    }

    [[nodiscard]] inline RadNeedsPolicy::NeedsState Restore(const PersistedState& state) noexcept
    {
        if (!IsValid(state))
            return {};
        return { state.hunger01, state.thirst01, state.sleepDebt01 };
    }

    class RecordCandidates
    {
    public:
        template <class Stream>
        bool Read(const Stream& stream, std::uint32_t version, std::uint32_t length)
        {
            if (version != kVersion || length != kPayloadSize) return false;
            PersistedState candidate;
            if (stream.ReadRecordData(candidate.hunger01) != sizeof(float) ||
                stream.ReadRecordData(candidate.thirst01) != sizeof(float) ||
                stream.ReadRecordData(candidate.sleepDebt01) != sizeof(float) || !IsValid(candidate))
                return false;
            Accept(candidate);
            return true;
        }

        void Accept(const PersistedState& candidate) noexcept
        {
            if (!IsValid(candidate)) return;
            if (!_first) _first = candidate;
            else if (_first->hunger01 != candidate.hunger01 ||
                     _first->thirst01 != candidate.thirst01 ||
                     _first->sleepDebt01 != candidate.sleepDebt01)
                _conflicting = true;
        }

        [[nodiscard]] RadNeedsPolicy::NeedsState RestoreState() const noexcept
        {
            return _first && !_conflicting ? Restore(*_first) : RadNeedsPolicy::NeedsState{};
        }

    private:
        std::optional<PersistedState> _first;
        bool _conflicting{ false };
    };

    template <class Stream>
    bool Write(const Stream& stream, const PersistedState& state)
    {
        return IsValid(state) && stream.OpenRecord(kRecord, kVersion) &&
            stream.WriteRecordData(state.hunger01) && stream.WriteRecordData(state.thirst01) &&
            stream.WriteRecordData(state.sleepDebt01);
    }
}
