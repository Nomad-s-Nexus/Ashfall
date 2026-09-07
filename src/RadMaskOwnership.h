#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace RadMaskOwnership
{
    inline constexpr std::size_t kMaxEntries = 1024;

    struct Entry
    {
        std::uint32_t actorFormID;
        std::uint32_t maskFormID;

        bool operator==(const Entry&) const = default;
    };

    enum class CandidateDisposition
    {
        kRestore,
        kAmbiguous
    };

    inline bool IsValidSnapshot(const std::vector<Entry>& entries)
    {
        if (entries.size() > kMaxEntries) return false;
        std::uint32_t previousActor = 0;
        for (const auto& entry : entries)
        {
            if (entry.actorFormID == 0 || entry.maskFormID == 0 || entry.actorFormID <= previousActor)
                return false;
            previousActor = entry.actorFormID;
        }
        return true;
    }

    inline CandidateDisposition ClassifyCandidates(const std::vector<std::vector<Entry>>& candidates)
    {
        if (candidates.empty()) return CandidateDisposition::kAmbiguous;
        const auto& first = candidates.front();
        for (std::size_t i = 1; i < candidates.size(); ++i)
        {
            if (candidates[i] != first) return CandidateDisposition::kAmbiguous;
        }
        return CandidateDisposition::kRestore;
    }

    bool Claim(std::uint32_t actorFormID, std::uint32_t maskFormID);
    bool Owns(std::uint32_t actorFormID, std::uint32_t maskFormID);
    void Release(std::uint32_t actorFormID);
    std::vector<Entry> Snapshot();
    bool Restore(const std::vector<Entry>& entries);
    void Clear();
}
