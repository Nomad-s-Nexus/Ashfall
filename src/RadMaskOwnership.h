#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace RadMaskOwnership
{
    inline constexpr std::size_t kMaxEntries = 1024;
    inline constexpr std::size_t kMaxDisplacedGear = 4;
    inline constexpr std::uint32_t kTemporaryMaskFlag = 1u;

    struct Entry
    {
        std::uint32_t actorFormID{ 0 };
        std::uint32_t maskFormID{ 0 };
        std::uint32_t flags{ 0 };
        std::array<std::uint32_t, kMaxDisplacedGear> displacedFormIDs{};

        bool operator==(const Entry&) const = default;
        [[nodiscard]] bool IsTemporary() const { return (flags & kTemporaryMaskFlag) != 0; }
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
            if ((entry.flags & ~kTemporaryMaskFlag) != 0)
                return false;

            bool sawZero = false;
            for (std::size_t i = 0; i < entry.displacedFormIDs.size(); ++i)
            {
                const auto formID = entry.displacedFormIDs[i];
                if (formID == 0)
                {
                    sawZero = true;
                    continue;
                }
                if (!entry.IsTemporary() || sawZero || formID == entry.maskFormID)
                    return false;
                for (std::size_t j = 0; j < i; ++j)
                    if (entry.displacedFormIDs[j] == formID)
                        return false;
            }
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

    bool Activate();
    bool Claim(std::uint32_t actorFormID, std::uint32_t maskFormID);
    bool ClaimTemporary(
        std::uint32_t actorFormID,
        std::uint32_t maskFormID,
        const std::array<std::uint32_t, kMaxDisplacedGear>& displacedFormIDs);
    bool Owns(std::uint32_t actorFormID, std::uint32_t maskFormID);
    std::optional<Entry> Lookup(std::uint32_t actorFormID);
    void Release(std::uint32_t actorFormID);
    std::vector<Entry> Snapshot();
    bool Restore(const std::vector<Entry>& entries);
    void Clear();
}
