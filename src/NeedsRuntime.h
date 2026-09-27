#pragma once

#include "NeedsSerializationPolicy.h"

#include <mutex>

class NeedsRuntime
{
public:
    static NeedsRuntime& GetSingleton()
    {
        static NeedsRuntime instance;
        return instance;
    }

    void Update();
    void Reset();
    [[nodiscard]] RadNeedsSerialization::PersistedState SnapshotForSave() const;
    void RestoreFromSave(const RadNeedsPolicy::NeedsState& state);

private:
    NeedsRuntime() = default;
    mutable std::mutex _mutex;
    RadNeedsPolicy::CalendarState _clock;
    RadNeedsPolicy::HourlyRates _rates;
    bool _configLoaded{ false };
};
