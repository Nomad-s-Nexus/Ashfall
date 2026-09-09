#include "pch.h"
#include "ClimateHeatRuntime.h"
#include "ClimateHeatPolicy.h"

#include <RE/B/BSContainer.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TES.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESObjectCELL.h>
#include <RE/T/TESObjectREFR.h>

namespace
{
    constexpr float kCookingFireRadius = 900.0f;
    constexpr float kCookingFireStrength = 0.70f;
    constexpr float kBonfireBarrelRadius = 650.0f;
    constexpr float kBonfireBarrelStrength = 0.50f;
    constexpr float kMaximumSearchRadius = kCookingFireRadius;

    float Distance(const RE::NiPoint3& lhs, const RE::NiPoint3& rhs) noexcept
    {
        const float x = lhs.x - rhs.x;
        const float y = lhs.y - rhs.y;
        const float z = lhs.z - rhs.z;
        return std::sqrt(x * x + y * y + z * z);
    }
}

void ClimateHeatRuntime::Reset() noexcept
{
    _sources = {};
    _sourceCount = 0;
    _lastCellID = 0;
    _lastX = 0.0f;
    _lastY = 0.0f;
    _lastZ = 0.0f;
    _cachedHeat01 = 0.0f;
    _sourcesResolved = false;
    _sampleKnown = false;
    _lastHeated = false;
}

void ClimateHeatRuntime::EnsureSourcesResolved()
{
    if (_sourcesResolved)
        return;

    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler)
        return;

    const auto add = [this, dataHandler](std::uint32_t localFormID, float radius, float strength01) {
        if (_sourceCount >= _sources.size())
            return;
        if (auto* form = dataHandler->LookupForm(localFormID, "Fallout4.esm"))
            _sources[_sourceCount++] = { form->GetFormID(), radius, strength01 };
    };

    add(0x002476B7, kCookingFireRadius, kCookingFireStrength);
    add(0x00048280, kBonfireBarrelRadius, kBonfireBarrelStrength);
    add(0x00048281, kBonfireBarrelRadius, kBonfireBarrelStrength);
    add(0x00048282, kBonfireBarrelRadius, kBonfireBarrelStrength);
    _sourcesResolved = true;

    if (auto logger = spdlog::get("FalloutAnomaly"))
        logger->info("[Climate] Heat source forms resolved: {}.", _sourceCount);
}

const ClimateHeatRuntime::SourceProfile* ClimateHeatRuntime::ProfileFor(
    std::uint32_t runtimeFormID) const noexcept
{
    for (std::size_t i = 0; i < _sourceCount; ++i)
    {
        if (_sources[i].runtimeFormID == runtimeFormID)
            return &_sources[i];
    }
    return nullptr;
}

float ClimateHeatRuntime::Sample(RE::PlayerCharacter* player)
{
    EnsureSourcesResolved();
    if (!player || !_sourcesResolved)
    {
        _sampleKnown = false;
        return 0.0f;
    }

    auto* cell = player->GetParentCell();
    if (!cell || !cell->IsAttached())
    {
        _sampleKnown = false;
        return 0.0f;
    }

    const auto position = player->GetPosition();
    const auto cellID = cell->GetFormID();
    if (!RadClimateHeatPolicy::ShouldRefresh(
            _sampleKnown,
            cellID,
            _lastCellID,
            position.x - _lastX,
            position.y - _lastY,
            position.z - _lastZ))
        return _cachedHeat01;

    auto* world = RE::TES::GetSingleton();
    if (!world)
    {
        _sampleKnown = false;
        return 0.0f;
    }

    float remaining = 1.0f;
    world->ForEachReferenceInRange(player, kMaximumSearchRadius, [&](RE::TESObjectREFR* ref) {
        if (!ref || ref == player || !ref->Get3D())
            return RE::BSContainer::ForEachResult::kContinue;

        auto* base = ref->GetObjectReference();
        if (!base)
            return RE::BSContainer::ForEachResult::kContinue;

        const auto* profile = ProfileFor(base->GetFormID());
        if (!profile)
            return RE::BSContainer::ForEachResult::kContinue;

        const RadClimateHeatPolicy::SourceSample sample{
            Distance(position, ref->GetPosition()),
            profile->radius,
            profile->strength01,
            true
        };
        remaining *= 1.0f - RadClimateHeatPolicy::Contribution(sample);
        return RE::BSContainer::ForEachResult::kContinue;
    });

    _cachedHeat01 = std::clamp(1.0f - remaining, 0.0f, 1.0f);
    _lastCellID = cellID;
    _lastX = position.x;
    _lastY = position.y;
    _lastZ = position.z;
    _sampleKnown = true;

    const bool heated = _cachedHeat01 > 0.001f;
    if (heated != _lastHeated)
    {
        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->info("[Climate] Nearby heat changed: active={} heat={:.0f}%.", heated, _cachedHeat01 * 100.0f);
        _lastHeated = heated;
    }

    return _cachedHeat01;
}
