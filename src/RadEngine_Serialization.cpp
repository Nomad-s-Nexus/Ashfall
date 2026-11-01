#include "RadEngine_Internal.h"
#include "ClimateRuntime.h"
#include "NeedsRuntime.h"
#include "NeedsSerializationPolicy.h"
#include "SerializationPolicy.h"
#include "RadMaskOwnership.h"

#include <algorithm>
#include <vector>

using namespace RadInternal;

namespace
{
    constexpr std::uint32_t kStormClockRecord  = 'SCLK';
    constexpr std::uint32_t kStormClockVersion = 1;
    constexpr std::uint32_t kFilterRecord      = 'FILT';
    constexpr std::uint32_t kFilterVersion     = 2;
    constexpr std::uint32_t kPenaltyRecord     = 'PENA';
    constexpr std::uint32_t kPenaltyVersion    = 1;
    constexpr std::uint32_t kPenaltyLen        = sizeof(float) * 4;
    constexpr std::uint32_t kMaskOwnershipRecord = 'MOWN';
    constexpr std::uint32_t kMaskOwnershipVersion = 1;
    constexpr std::uint32_t kMaskOwnershipHeaderLen = sizeof(std::uint32_t);
    constexpr std::uint32_t kMaskOwnershipEntryLen = sizeof(std::uint32_t) * 2;


    constexpr std::uint32_t kStormClockLen = sizeof(double) * 3 + sizeof(std::uint8_t);
    constexpr std::uint32_t kFilterV1Len = sizeof(float);
    constexpr std::uint32_t kFilterV2Len = sizeof(float) * 2;
}


void RadiationEngine::SaveState(const F4SE::SerializationInterface* a_intfc) const
{
    auto logger = Log();
    if (!a_intfc->OpenRecord(kStormClockRecord, kStormClockVersion))
    {
        logger->error("[Save] Could not open storm-clock record — schedule will not persist.");
        return;
    }
    a_intfc->WriteRecordData(_playSeconds);
    a_intfc->WriteRecordData(_nextStormPlaySec);
    a_intfc->WriteRecordData(_lastRadstormSec);
    const std::uint8_t scheduled = _stormScheduled ? 1 : 0;
    a_intfc->WriteRecordData(scheduled);
    logger->info("[Save] Storm clock persisted (play={:.0f}s, next={:.0f}s, scheduled={}).",
        _playSeconds, _nextStormPlaySec, _stormScheduled);


    if (a_intfc->OpenRecord(kFilterRecord, kFilterVersion))
    {
        a_intfc->WriteRecordData(_filterCharge);
        a_intfc->WriteRecordData(_maskCondition);
    }

    if (a_intfc->OpenRecord(kPenaltyRecord, kPenaltyVersion))
    {
        a_intfc->WriteRecordData(_appliedCarryPen);
        a_intfc->WriteRecordData(_appliedHealPen);
        a_intfc->WriteRecordData(_appliedSpeedPen);
        a_intfc->WriteRecordData(_appliedApPen);
    }
    else
    {
        logger->error("[Save] Could not open tissue-penalty record.");
    }

    const auto ownership = RadMaskOwnership::Snapshot();
    if (a_intfc->OpenRecord(kMaskOwnershipRecord, kMaskOwnershipVersion))
    {
        const auto count = static_cast<std::uint32_t>(ownership.size());
        a_intfc->WriteRecordData(count);
        for (const auto& entry : ownership)
        {
            a_intfc->WriteRecordData(entry.actorFormID);
            a_intfc->WriteRecordData(entry.maskFormID);
        }
    }
    else
    {
        logger->error("[Save] Could not open mask-ownership record.");
    }
}


void RadiationEngine::LoadState(const F4SE::SerializationInterface* a_intfc)
{
    auto logger = Log();
    ClimateRuntime::GetSingleton().Reset();
    NeedsRuntime::GetSingleton().Reset();
    std::uint32_t type = 0, version = 0, length = 0;





    std::vector<RadiationSerialization::StormClockState> clockCandidates;
    std::vector<RadiationSerialization::FilterState> filterCandidates;
    std::vector<RadiationSerialization::PenaltyState> penaltyCandidates;
    int penaltySeen = 0;
    std::vector<std::vector<RadMaskOwnership::Entry>> ownershipCandidates;
    int clockSeen = 0, clockRejected = 0;
    int filterSeen = 0, filterRejected = 0;
    int ownershipSeen = 0, ownershipRejected = 0;
    std::vector<RadClimateSerialization::PersistedState> climateCandidates;
    int climateSeen = 0, climateRejected = 0;

    const auto readExact = [a_intfc]<class T>(T& a_value) {
        return a_intfc->ReadRecordData(a_value) == sizeof(T);
    };

    while (a_intfc->GetNextRecordInfo(type, version, length))
    {
        if (type == RadNeedsSerialization::kRecord)
        {
            // Constraint: an old save still carries the NEED record; it is read and dropped.
            logger->info("[Load] Legacy NEED record v{} len {} ignored; needs are plugin globals.", version, length);
            continue;
        }
        if (type == RadClimateSerialization::kRecord)
        {
            ++climateSeen;
            if (version != RadClimateSerialization::kVersion || length != RadClimateSerialization::kPayloadSize)
            {
                ++climateRejected;
                logger->warn("[Load] Climate record #{} has unsupported version {} or length {} — ignoring.",
                    climateSeen, version, length);
                continue;
            }
            float coreC = 0.0f;
            float wetness01 = 0.0f;
            const bool read = readExact(coreC) && readExact(wetness01);
            const RadClimateSerialization::PersistedState candidate{ coreC, wetness01 };
            if (!read || !RadClimateSerialization::IsValid(candidate))
            {
                ++climateRejected;
                logger->warn("[Load] Climate record #{} is invalid or truncated (core={}, wet={}) — ignoring.",
                    climateSeen, coreC, wetness01);
                continue;
            }
            climateCandidates.push_back(candidate);
            continue;
        }
        if (type == kPenaltyRecord)
        {
            ++penaltySeen;
            RadiationSerialization::PenaltyState candidate{};
            const bool read = version == kPenaltyVersion && length == kPenaltyLen &&
                readExact(candidate.carry) && readExact(candidate.heal) &&
                readExact(candidate.speed) && readExact(candidate.ap);
            if (!read || !RadiationSerialization::IsValidPenaltyState(candidate))
            {
                logger->warn("[Load] Tissue-penalty record #{} v{} len {} is invalid or truncated - ignoring.",
                    penaltySeen, version, length);
                continue;
            }
            penaltyCandidates.push_back(candidate);
            continue;
        }
        if (type == kMaskOwnershipRecord)
        {
            ++ownershipSeen;
            if (version != kMaskOwnershipVersion || length < kMaskOwnershipHeaderLen)
            {
                ++ownershipRejected;
                logger->warn("[Load] Mask-ownership record #{} v{} len {} is unsupported — ignoring.",
                    ownershipSeen, version, length);
                continue;
            }

            std::uint32_t count = 0;
            if (!readExact(count) || count > RadMaskOwnership::kMaxEntries)
            {
                ++ownershipRejected;
                logger->warn("[Load] Mask-ownership record #{} has invalid count — ignoring.", ownershipSeen);
                continue;
            }

            const auto expectedLength = kMaskOwnershipHeaderLen + count * kMaskOwnershipEntryLen;
            if (length != expectedLength)
            {
                ++ownershipRejected;
                logger->warn("[Load] Mask-ownership record #{} bad length {} (expected {}) — ignoring.",
                    ownershipSeen, length, expectedLength);
                continue;
            }

            std::vector<RadMaskOwnership::Entry> candidate;
            candidate.reserve(count);
            bool read = true;
            for (std::uint32_t i = 0; i < count; ++i)
            {
                RadMaskOwnership::Entry entry{};
                if (!readExact(entry.actorFormID) || !readExact(entry.maskFormID))
                {
                    read = false;
                    break;
                }
                candidate.push_back(entry);
            }
            if (!read || !RadMaskOwnership::IsValidSnapshot(candidate))
            {
                ++ownershipRejected;
                logger->warn("[Load] Mask-ownership record #{} is invalid or truncated — ignoring.", ownershipSeen);
                continue;
            }
            ownershipCandidates.push_back(std::move(candidate));
            continue;
        }
        if (type == kFilterRecord)
        {
            ++filterSeen;
            float charge = 100.0f;
            float condition = 100.0f;
            const bool isV1 = version == 1 && length == kFilterV1Len;
            const bool isV2 = version == kFilterVersion && length == kFilterV2Len;
            const bool read = isV1 ? readExact(charge) :
                              isV2 ? readExact(charge) && readExact(condition) : false;
            const RadiationSerialization::FilterState candidate{ charge, condition };
            if (!read || !RadiationSerialization::IsValidFilterState(candidate))
            {
                ++filterRejected;
                logger->warn("[Load] Filter record #{} v{} len {} is invalid or truncated — ignoring.",
                    filterSeen, version, length);
                continue;
            }
            filterCandidates.push_back(candidate);
            continue;
        }
        if (type != kStormClockRecord)
        {
            logger->warn("[Load] Unknown co-save record 0x{:08X} (len {}) — skipping.", type, length);
            continue;
        }
        ++clockSeen;
        if (version != kStormClockVersion)
        {
            ++clockRejected;
            logger->warn("[Load] Storm-clock record #{} v{} != v{} — ignoring.", clockSeen, version, kStormClockVersion);
            continue;
        }
        if (length != kStormClockLen)
        {
            ++clockRejected;
            logger->warn("[Load] Storm-clock record #{} bad length {} (expected {}) — ignoring.",
                clockSeen, length, kStormClockLen);
            continue;
        }
        double p = 0.0, n = 0.0, l = 0.0;
        std::uint8_t s = 0;
        const bool read = readExact(p) && readExact(n) && readExact(l) && readExact(s);
        const RadiationSerialization::StormClockState candidate{ p, n, l, s != 0 };
        if (!read || (s != 0 && s != 1) || !RadiationSerialization::IsValidStormClock(candidate))
        {
            ++clockRejected;
            logger->warn("[Load] Storm-clock record #{} is invalid or truncated (play={}, next={}, last={}, sched={}) — ignoring.",
                clockSeen, p, n, l, static_cast<int>(s));
            continue;
        }
        clockCandidates.push_back(candidate);
    }

    if (climateCandidates.size() == 1 ||
        (climateCandidates.size() > 1 && RadClimateSerialization::ClassifyCandidates(climateCandidates) ==
            RadClimateSerialization::CandidateDisposition::kRestore))
    {
        ClimateRuntime::GetSingleton().RestoreFromSave(climateCandidates.front());
        logger->info("[Load] Climate state restored (core={:.2f}C, wet={:.0f}%){}.",
            climateCandidates.front().coreC, climateCandidates.front().wetness01 * 100.0f,
            climateCandidates.size() > 1 ? " [identical duplicates]" : "");
    }
    else if (!climateCandidates.empty())
    {
        logger->warn("[Load] {} conflicting valid climate records — keeping defaults.", climateCandidates.size());
    }
    else if (climateSeen > 0)
    {
        logger->warn("[Load] {} climate record(s) present but none valid ({} rejected) — keeping defaults.",
            climateSeen, climateRejected);
    }

    if (clockCandidates.size() == 1 ||
        (clockCandidates.size() > 1 && RadiationSerialization::ClassifyStormClockCandidates(clockCandidates) ==
            RadiationSerialization::StormClockCandidateDisposition::kRestore))
    {
        const auto& clock = clockCandidates.front();
        _playSeconds      = clock.playSeconds;
        _nextStormPlaySec = clock.nextStormPlaySeconds;
        _lastRadstormSec  = clock.lastRadstormSeconds;
        _stormScheduled   = clock.scheduled;
        logger->info("[Load] Storm clock restored (play={:.0f}s, next={:.0f}s, scheduled={}){}.",
            _playSeconds, _nextStormPlaySec, _stormScheduled, clockCandidates.size() > 1 ? " [identical duplicates]" : "");
    }
    else if (!clockCandidates.empty())
    {
        logger->warn("[Load] {} conflicting valid storm-clock records — refusing to choose a clock; scheduling fresh.",
            clockCandidates.size());
        ScheduleNextStorm();
    }
    else if (clockSeen > 0)
    {
        logger->warn("[Load] {} storm-clock record(s) present but none valid ({} rejected) — scheduling fresh.",
            clockSeen, clockRejected);
        ScheduleNextStorm();
    }

    if (filterCandidates.size() == 1 ||
        (filterCandidates.size() > 1 && RadiationSerialization::ClassifyFilterCandidates(filterCandidates) ==
            RadiationSerialization::FilterCandidateDisposition::kRestore))
    {
        const auto& filter = filterCandidates.front();
        _filterCharge = filter.charge;
        _maskCondition = filter.condition;
        logger->info("[Load] Gas-mask filter restored (charge {:.0f}%, condition {:.0f}%){}.",
            _filterCharge, _maskCondition, filterCandidates.size() > 1 ? " [identical duplicates]" : "");
    }
    else if (!filterCandidates.empty())
    {
        logger->warn("[Load] {} conflicting valid filter records — refusing to choose; keeping fresh defaults.",
            filterCandidates.size());
    }
    else if (filterSeen > 0)
    {
        logger->warn("[Load] {} filter record(s) present but none valid ({} rejected) — keeping fresh defaults.",
            filterSeen, filterRejected);
    }

    if (RadiationSerialization::PenaltyCandidatesAgree(penaltyCandidates))
    {
        const auto& pen = penaltyCandidates.front();
        _appliedCarryPen = pen.carry;
        _appliedHealPen  = pen.heal;
        _appliedSpeedPen = pen.speed;
        _appliedApPen    = pen.ap;
        _penaltiesFromSave = true;
        logger->info("[Load] Tissue penalties restored (carry {:.1f}, heal {:.1f}, speed {:.1f}, AP {:.1f}).",
            pen.carry, pen.heal, pen.speed, pen.ap);
    }
    else if (penaltySeen > 0)
    {
        logger->warn("[Load] {} tissue-penalty record(s) present but not one agreeing valid record - penalties start at 0.",
            penaltySeen);
    }

    if (RadMaskOwnership::ClassifyCandidates(ownershipCandidates) == RadMaskOwnership::CandidateDisposition::kRestore)
    {
        std::vector<RadMaskOwnership::Entry> resolved;
        resolved.reserve(ownershipCandidates.front().size());
        for (const auto& entry : ownershipCandidates.front())
        {
            const auto actorID = a_intfc->ResolveFormID(entry.actorFormID);
            const auto maskID = a_intfc->ResolveFormID(entry.maskFormID);
            if (actorID && maskID)
                resolved.push_back({ *actorID, *maskID });
        }
        std::ranges::sort(resolved, {}, &RadMaskOwnership::Entry::actorFormID);
        if (RadMaskOwnership::Restore(resolved))
        {
            logger->info("[Load] Auto-mask ownership restored ({} of {} entries resolved).",
                resolved.size(), ownershipCandidates.front().size());
        }
        else
        {
            RadMaskOwnership::Clear();
            logger->warn("[Load] Resolved auto-mask ownership was ambiguous — ownership discarded.");
        }
    }
    else if (!ownershipCandidates.empty())
    {
        RadMaskOwnership::Clear();
        logger->warn("[Load] {} conflicting valid mask-ownership records — ownership discarded.",
            ownershipCandidates.size());
    }
    else if (ownershipSeen > 0)
    {
        RadMaskOwnership::Clear();
        logger->warn("[Load] {} mask-ownership record(s) present but none valid ({} rejected) — ownership discarded.",
            ownershipSeen, ownershipRejected);
    }
}
