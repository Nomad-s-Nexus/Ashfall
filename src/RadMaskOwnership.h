#pragma once

#include <cstdint>
#include <vector>

namespace RadMaskOwnership
{
    struct Entry
    {
        std::uint32_t actorFormID;
        std::uint32_t maskFormID;

        bool operator==(const Entry&) const = default;
    };

    bool Claim(std::uint32_t actorFormID, std::uint32_t maskFormID);
    bool Owns(std::uint32_t actorFormID, std::uint32_t maskFormID);
    void Release(std::uint32_t actorFormID);
    std::vector<Entry> Snapshot();
    bool Restore(const std::vector<Entry>& entries);
    void Clear();
}
