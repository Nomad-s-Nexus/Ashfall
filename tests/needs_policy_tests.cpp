#include "NeedsPolicy.h"
#include "NeedsSerializationPolicy.h"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <cstring>
#include <vector>

namespace
{
    void Require(bool condition)
    {
        if (!condition)
            std::abort();
    }

    bool Near(float lhs, float rhs, float epsilon = 0.0001f)
    {
        return std::abs(lhs - rhs) <= epsilon;
    }

    struct Stream
    {
        mutable std::vector<char> bytes;
        mutable std::size_t cursor{};
        bool writable{ true };

        bool OpenRecord(std::uint32_t type, std::uint32_t version) const
        {
            Require(type == RadNeedsSerialization::kRecord && version == RadNeedsSerialization::kVersion);
            return writable;
        }

        bool WriteRecordData(float value) const
        {
            const auto* begin = reinterpret_cast<const char*>(&value);
            bytes.insert(bytes.end(), begin, begin + sizeof(value));
            return writable;
        }

        std::size_t ReadRecordData(float& value) const
        {
            const auto count = std::min(sizeof(value), bytes.size() - cursor);
            std::memcpy(&value, bytes.data() + cursor, count);
            cursor += count;
            return count;
        }
    };
}

int main()
{
    using namespace RadNeedsPolicy;

    HourlyRates rates{};
    rates.hungerPerHour = 0.05f;
    rates.thirstPerHour = 0.10f;
    rates.sleepDebtPerHour = 0.025f;

    const auto advanced = Advance({}, 2.0f, rates);
    Require(Near(advanced.hunger01, 0.10f));
    Require(Near(advanced.thirst01, 0.20f));
    Require(Near(advanced.sleepDebt01, 0.05f));

    const auto noBackwardTime = Advance(advanced, -8.0f, rates);
    Require(Near(noBackwardTime.hunger01, advanced.hunger01));
    Require(Near(noBackwardTime.thirst01, advanced.thirst01));
    Require(Near(noBackwardTime.sleepDebt01, advanced.sleepDebt01));

    const auto saturated = Advance({}, 1000.0f, rates);
    Require(Near(saturated.hunger01, 1.0f));
    Require(Near(saturated.thirst01, 1.0f));
    Require(Near(saturated.sleepDebt01, 1.0f));

    const HourlyRates extremeRates{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()
    };
    const auto extreme = Advance({}, std::numeric_limits<float>::max(), extremeRates);
    Require(Near(extreme.hunger01, 1.0f));
    Require(Near(extreme.thirst01, 1.0f));
    Require(Near(extreme.sleepDebt01, 1.0f));

    Relief relief{};
    relief.hunger01 = 0.04f;
    relief.thirst01 = 0.15f;
    relief.sleepDebt01 = 0.02f;
    const auto relieved = ApplyRelief(advanced, relief);
    Require(Near(relieved.hunger01, 0.06f));
    Require(Near(relieved.thirst01, 0.05f));
    Require(Near(relieved.sleepDebt01, 0.03f));

    const NeedsState invalid{
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -5.0f
    };
    const auto sanitized = Sanitize(invalid);
    Require(Near(sanitized.hunger01, 0.0f));
    Require(Near(sanitized.thirst01, 0.0f));
    Require(Near(sanitized.sleepDebt01, 0.0f));

    using namespace RadNeedsSerialization;
    const auto saved = Snapshot(advanced);
    Require(IsValid(saved));
    const auto restored = Restore(saved);
    Require(Near(restored.hunger01, advanced.hunger01));
    Require(Near(restored.thirst01, advanced.thirst01));
    Require(Near(restored.sleepDebt01, advanced.sleepDebt01));

    PersistedState badSave{};
    badSave.hunger01 = 2.0f;
    Require(!IsValid(badSave));
    const auto fallback = Restore(badSave);
    Require(Near(fallback.hunger01, 0.0f));
    Require(Near(fallback.thirst01, 0.0f));
    Require(Near(fallback.sleepDebt01, 0.0f));

    Require(kVersion == 1);

    CalendarState clock;
    clock.Observe(10.0, rates);
    Require(Near(clock.state.hunger01, 0.0f));
    clock.Observe(10.0 + 2.0 / 24.0, rates);
    Require(Near(clock.state.hunger01, advanced.hunger01));
    clock.Observe(10.0 + 2.0 / 24.0, rates);
    Require(Near(clock.state.hunger01, advanced.hunger01));
    clock.Observe(9.0, rates);
    Require(Near(clock.state.hunger01, advanced.hunger01));
    clock.Observe(std::numeric_limits<double>::quiet_NaN(), rates);
    clock.Observe(30.0, rates);
    Require(Near(clock.state.hunger01, advanced.hunger01));
    clock = { restored };
    clock.Observe(200.0, rates);
    Require(Near(clock.state.hunger01, advanced.hunger01));
    clock.Observe(201.0, rates);
    Require(Near(clock.state.hunger01, 1.0f));
    clock = {};
    clock.Observe(500.0, rates);
    Require(Near(clock.state.hunger01, 0.0f));
    clock.Observe(std::numeric_limits<double>::max(), rates);
    Require(Near(clock.state.hunger01, 1.0f));

    RecordCandidates candidates;
    Require(Near(candidates.RestoreState().hunger01, 0.0f));
    candidates.Accept(saved);
    candidates.Accept(badSave);
    candidates.Accept(saved);
    Require(Near(candidates.RestoreState().hunger01, advanced.hunger01));
    candidates.Accept({0.7f, 0.5f, 0.3f});
    Require(Near(candidates.RestoreState().hunger01, 0.0f));
    candidates.Accept(saved);
    Require(Near(candidates.RestoreState().hunger01, 0.0f));

    Stream stream;
    Require(Write(stream, saved));
    Require(stream.bytes.size() == kPayloadSize);
    RecordCandidates loaded;
    Require(!loaded.Read(stream, kVersion + 1, kPayloadSize));
    Require(!loaded.Read(stream, kVersion, kPayloadSize - 1));
    Require(stream.cursor == 0);
    Require(loaded.Read(stream, kVersion, kPayloadSize));
    Require(Near(loaded.RestoreState().thirst01, saved.thirst01));
    stream.cursor = 0;
    stream.bytes.pop_back();
    loaded = {};
    Require(!loaded.Read(stream, kVersion, kPayloadSize));
    Require(Near(loaded.RestoreState().thirst01, 0.0f));
    stream = {};
    Require(!Write(stream, badSave));
    Require(stream.bytes.empty());
    stream.writable = false;
    Require(!Write(stream, saved));
    Require(stream.bytes.empty());
    return 0;
}
