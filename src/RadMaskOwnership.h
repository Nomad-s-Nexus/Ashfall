#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace RadMaskOwnership
{
    inline constexpr std::size_t kMaxEntries = 1024;
    inline constexpr std::size_t kMaxOwnedActors = 128;
    inline constexpr std::size_t kMaxDisplacedGear = 4;
    inline constexpr std::uint32_t kTemporaryMaskFlag = 1u;
    inline constexpr std::uint32_t kTemporaryNpcMaskFormID = 0x001184C1;
    inline constexpr std::uint32_t kTemporaryMarkerFormID = 0x0002CB72;

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

    inline bool HasLogicalMetadata(const Entry& entry)
    {
        if (entry.flags != 0) return true;
        for (const auto formID : entry.displacedFormIDs)
            if (formID != 0) return true;
        return false;
    }

    inline bool IsValidLogicalEntry(const Entry& entry)
    {
        if (entry.actorFormID == 0 || entry.maskFormID == 0) return false;
        if ((entry.flags & ~kTemporaryMaskFlag) != 0) return false;
        if (entry.IsTemporary() && entry.maskFormID != kTemporaryNpcMaskFormID) return false;

        bool sawZero = false;
        for (std::size_t i = 0; i < entry.displacedFormIDs.size(); ++i)
        {
            const auto formID = entry.displacedFormIDs[i];
            if (formID == 0)
            {
                sawZero = true;
                continue;
            }
            if (!entry.IsTemporary() || sawZero || formID == entry.maskFormID || formID == kTemporaryMarkerFormID)
                return false;
            for (std::size_t j = 0; j < i; ++j)
                if (entry.displacedFormIDs[j] == formID)
                    return false;
        }
        return true;
    }

    inline bool IsValidSnapshot(const std::vector<Entry>& entries)
    {
        if (entries.size() > kMaxEntries) return false;
        if (entries.empty()) return true;

        bool logical = false;
        for (const auto& entry : entries)
            if (HasLogicalMetadata(entry)) logical = true;

        std::uint32_t previousActor = 0;
        if (logical)
        {
            if (entries.size() > kMaxOwnedActors) return false;
            for (const auto& entry : entries)
            {
                if (!IsValidLogicalEntry(entry) || entry.actorFormID <= previousActor)
                    return false;
                previousActor = entry.actorFormID;
            }
            return true;
        }

        std::size_t i = 0;
        while (i < entries.size())
        {
            const auto actorID = entries[i].actorFormID;
            if (actorID == 0 || actorID <= previousActor) return false;

            std::size_t end = i;
            while (end < entries.size() && entries[end].actorFormID == actorID)
            {
                if (entries[end].maskFormID == 0 || HasLogicalMetadata(entries[end])) return false;
                ++end;
            }

            const auto groupSize = end - i;
            if (groupSize > 1)
            {
                if (groupSize > 2 + kMaxDisplacedGear) return false;
                bool hasMask = false;
                bool hasMarker = false;
                for (std::size_t a = i; a < end; ++a)
                {
                    const auto formID = entries[a].maskFormID;
                    if (formID == kTemporaryNpcMaskFormID) hasMask = true;
                    if (formID == kTemporaryMarkerFormID) hasMarker = true;
                    for (std::size_t b = i; b < a; ++b)
                        if (entries[b].maskFormID == formID)
                            return false;
                }
                if (!hasMask || !hasMarker) return false;
            }

            previousActor = actorID;
            i = end;
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
