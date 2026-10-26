#pragma once

#include "NeedsPolicy.h"

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

private:
    NeedsRuntime() = default;
    void ResolveGlobals();

    mutable std::mutex _mutex;
    // Constraint: opaque so this header stays free of engine types; the .cpp owns the casts.
    void* _hunger{ nullptr };
    void* _thirst{ nullptr };
    void* _sleep{ nullptr };
    void* _risk{ nullptr };
    void* _disease{ nullptr };
};
