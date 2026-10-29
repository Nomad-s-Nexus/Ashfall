#include "pch.h"
#include "ClimateShelterRuntime.h"

#include <RE/B/bhkPickData.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESObjectCELL.h>

namespace
{
    constexpr float kHeadZ = 96.0f;
    constexpr float kRayDistance = 768.0f;
    constexpr float kHorizontal = 0.342f;
    constexpr float kVertical = 0.940f;
    constexpr int kMinimumHits = 3;

    bool RayHits(RE::TESObjectCELL* cell, const RE::NiPoint3& from, const RE::NiPoint3& to)
    {
        if (!cell)
            return false;
        RE::bhkPickData pick;
        pick.SetStartEnd(from, to);
        return cell->Pick(pick) != nullptr || pick.HasHit();
    }
}

void ClimateShelterRuntime::Reset() noexcept
{
    _cached = {};
    _lastCellID = 0;
    _lastX = 0.0f;
    _lastY = 0.0f;
    _lastZ = 0.0f;
    _lastRoofed = false;
    _roofStateKnown = false;
    _lastProbeRoofless = false;
}

RadClimateShelterPolicy::Factors ClimateShelterRuntime::Sample(RE::PlayerCharacter* player)
{
    // Constraint: switching between the exterior and roofless-interior probes invalidates the shared roof cache.
    if (_lastProbeRoofless)
    {
        _roofStateKnown = false;
        _lastProbeRoofless = false;
    }

    if (!player)
    {
        _roofStateKnown = false;
        return RadClimateShelterPolicy::FromState(false, false, true);
    }

    auto* cell = player->GetParentCell();
    if (!cell)
    {
        _roofStateKnown = false;
        return RadClimateShelterPolicy::FromState(false, false, true);
    }

    if (cell->IsInterior())
    {
        _cached = RadClimateShelterPolicy::FromState(true, false, false);
        _lastCellID = cell->GetFormID();
        _lastRoofed = false;
        _roofStateKnown = true;
        return _cached;
    }

    if (!cell->IsExterior() || !cell->IsAttached())
    {
        _roofStateKnown = false;
        return RadClimateShelterPolicy::FromState(false, false, true);
    }

    const auto cellID = cell->GetFormID();
    const auto position = player->GetPosition();
    const float deltaX = position.x - _lastX;
    const float deltaY = position.y - _lastY;
    const float deltaZ = position.z - _lastZ;
    if (!RadClimateShelterPolicy::ShouldProbe(
            _roofStateKnown, cellID, _lastCellID, deltaX, deltaY, deltaZ))
        return _cached;

    const bool roofed = ProbeRoof(player, cell);
    _cached = RadClimateShelterPolicy::FromState(false, roofed, false);
    _lastCellID = cellID;
    _lastX = position.x;
    _lastY = position.y;
    _lastZ = position.z;

    if (!_roofStateKnown || roofed != _lastRoofed)
    {
        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->info("[Climate] Exterior roof cover changed: roofed={} thermal={:.0f}% rainCover={:.0f}%.",
                roofed, _cached.thermal01 * 100.0f, _cached.precipitation01 * 100.0f);
    }

    _lastRoofed = roofed;
    _roofStateKnown = true;
    return _cached;
}

RadClimateShelterPolicy::Factors ClimateShelterRuntime::SampleRooflessInterior(
    RE::PlayerCharacter* player,
    RE::TESObjectCELL* cell)
{
    if (!player || !cell)
    {
        _roofStateKnown = false;
        return RadClimateShelterPolicy::FromState(true, false, false);
    }

    // Constraint: switching into the roofless probe invalidates the shared roof cache.
    if (!_lastProbeRoofless)
    {
        _roofStateKnown = false;
        _lastProbeRoofless = true;
    }

    const auto cellID = cell->GetFormID();
    const auto position = player->GetPosition();
    const float deltaX = position.x - _lastX;
    const float deltaY = position.y - _lastY;
    const float deltaZ = position.z - _lastZ;
    if (!RadClimateShelterPolicy::ShouldProbe(
            _roofStateKnown, cellID, _lastCellID, deltaX, deltaY, deltaZ))
        return _cached;

    const bool roofed = ProbeRoof(player, cell);
    if (auto logger = spdlog::get("FalloutAnomaly"))
        logger->debug("[Climate] Roofless interior probe: cell=0x{:08X}, roofed={}.", cellID, roofed);
    _cached = RadClimateShelterPolicy::FromRooflessInterior(roofed);
    _lastCellID = cellID;
    _lastX = position.x;
    _lastY = position.y;
    _lastZ = position.z;

    if (!_roofStateKnown || roofed != _lastRoofed)
    {
        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->info("[Climate] Roofless interior roof cover changed: cell=0x{:08X}, roofed={}, rainCover={:.0f}%.",
                cellID, roofed, _cached.precipitation01 * 100.0f);
    }

    _lastRoofed = roofed;
    _roofStateKnown = true;
    return _cached;
}

bool ClimateShelterRuntime::ProbeRoof(RE::PlayerCharacter* player, RE::TESObjectCELL* cell) const
{
    const auto p = player->GetPosition();
    const RE::NiPoint3 head{ p.x, p.y, p.z + kHeadZ };
    const float horizontal = kRayDistance * kHorizontal;
    const float vertical = kRayDistance * kVertical;

    const bool up = RayHits(cell, head, { head.x, head.y, head.z + kRayDistance });
    const bool px = RayHits(cell, head, { head.x + horizontal, head.y, head.z + vertical });
    const bool nx = RayHits(cell, head, { head.x - horizontal, head.y, head.z + vertical });
    const bool py = RayHits(cell, head, { head.x, head.y + horizontal, head.z + vertical });
    const bool ny = RayHits(cell, head, { head.x, head.y - horizontal, head.z + vertical });
    const int hits = static_cast<int>(up) + static_cast<int>(px) + static_cast<int>(nx) +
        static_cast<int>(py) + static_cast<int>(ny);
    return up && hits >= kMinimumHits;
}
