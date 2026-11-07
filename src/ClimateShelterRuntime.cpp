#include "pch.h"
#include "ClimateShelterRuntime.h"
#include "RadEngine_Internal.h"

#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESObjectCELL.h>

#include <cmath>

namespace
{
    constexpr float kHeadZ = 96.0f;
    constexpr float kRayDistance = 768.0f;
    constexpr float kHorizontal = 0.342f;
    constexpr float kVertical = 0.940f;
    constexpr float kWallZ = 80.0f;
    constexpr float kWallDistance = 600.0f;
    constexpr float kPi = 3.14159265f;
}

void ClimateShelterRuntime::Reset() noexcept
{
    _cached = {};
    _settled = {};
    _lastCellID = 0;
    _lastX = 0.0f;
    _lastY = 0.0f;
    _lastZ = 0.0f;
    _lastProbeTime = {};
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
    if (!_roofStateKnown || cellID != _lastCellID)
        _settled = {};
    const auto now = std::chrono::steady_clock::now();
    const float elapsedSeconds = std::chrono::duration<float>(now - _lastProbeTime).count();
    const bool timed = _roofStateKnown && RadClimateShelterPolicy::ShouldReprobeTimed(_settled, elapsedSeconds);
    if (!timed && !RadClimateShelterPolicy::ShouldProbe(
            _roofStateKnown, cellID, _lastCellID, deltaX, deltaY, deltaZ))
        return _cached;

    const bool roofed = ProbeRoof(player, cell);
    const int wallHits = roofed ? ProbeWallHits(player, cell) : 0;
    const auto observed = RadClimateShelterPolicy::LevelFor(
        RadClimateShelterPolicy::Classify(roofed, wallHits, _enclosureEnabled));
    _settled = RadClimateShelterPolicy::Observe(_settled, observed);
    _cached = RadClimateShelterPolicy::FromLevel(_settled.value);
    const bool settledRoofed = _settled.value != RadClimateShelterPolicy::Level::kOpen;
    _lastCellID = cellID;
    _lastX = position.x;
    _lastY = position.y;
    _lastZ = position.z;
    _lastProbeTime = now;

    if (!_roofStateKnown || settledRoofed != _lastRoofed)
    {
        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->info("[Climate] Exterior roof cover changed: roofed={} thermal={:.0f}% rainCover={:.0f}%.",
                settledRoofed, _cached.thermal01 * 100.0f, _cached.precipitation01 * 100.0f);
    }

    _lastRoofed = settledRoofed;
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
    if (!_roofStateKnown || cellID != _lastCellID)
        _settled = {};
    const auto now = std::chrono::steady_clock::now();
    const float elapsedSeconds = std::chrono::duration<float>(now - _lastProbeTime).count();
    const bool timed = _roofStateKnown && RadClimateShelterPolicy::ShouldReprobeTimed(_settled, elapsedSeconds);
    if (!timed && !RadClimateShelterPolicy::ShouldProbe(
            _roofStateKnown, cellID, _lastCellID, deltaX, deltaY, deltaZ))
        return _cached;

    const bool roofed = ProbeRoof(player, cell);
    if (auto logger = spdlog::get("FalloutAnomaly"))
        logger->debug("[Climate] Roofless interior probe: cell=0x{:08X}, roofed={}.", cellID, roofed);
    _settled = RadClimateShelterPolicy::Observe(
        _settled,
        roofed ? RadClimateShelterPolicy::Level::kRoofed : RadClimateShelterPolicy::Level::kOpen);
    const bool settledRoofed = _settled.value != RadClimateShelterPolicy::Level::kOpen;
    _cached = RadClimateShelterPolicy::FromRooflessInterior(settledRoofed);
    _lastCellID = cellID;
    _lastX = position.x;
    _lastY = position.y;
    _lastZ = position.z;
    _lastProbeTime = now;

    if (!_roofStateKnown || settledRoofed != _lastRoofed)
    {
        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->info("[Climate] Roofless interior roof cover changed: cell=0x{:08X}, roofed={}, rainCover={:.0f}%.",
                cellID, settledRoofed, _cached.precipitation01 * 100.0f);
    }

    _lastRoofed = settledRoofed;
    _roofStateKnown = true;
    return _cached;
}

bool ClimateShelterRuntime::ProbeRoof(RE::PlayerCharacter* player, RE::TESObjectCELL* cell) const
{
    const auto p = player->GetPosition();
    const RE::NiPoint3 head{ p.x, p.y, p.z + kHeadZ };
    const float horizontal = kRayDistance * kHorizontal;
    const float vertical = kRayDistance * kVertical;

    const bool up = RadInternal::RayHitsStructure(cell, head, { head.x, head.y, head.z + kRayDistance });
    const bool px = RadInternal::RayHitsStructure(cell, head, { head.x + horizontal, head.y, head.z + vertical });
    const bool nx = RadInternal::RayHitsStructure(cell, head, { head.x - horizontal, head.y, head.z + vertical });
    const bool py = RadInternal::RayHitsStructure(cell, head, { head.x, head.y + horizontal, head.z + vertical });
    const bool ny = RadInternal::RayHitsStructure(cell, head, { head.x, head.y - horizontal, head.z + vertical });
    const int hits = static_cast<int>(up) + static_cast<int>(px) + static_cast<int>(nx) +
        static_cast<int>(py) + static_cast<int>(ny);
    return RadClimateShelterPolicy::RoofCover(up, hits);
}

int ClimateShelterRuntime::ProbeWallHits(RE::PlayerCharacter* player, RE::TESObjectCELL* cell) const
{
    const auto p = player->GetPosition();
    const RE::NiPoint3 origin{ p.x, p.y, p.z + kWallZ };
    int hits = 0;
    for (int i = 0; i < RadClimateShelterPolicy::kWallRays; ++i)
    {
        const float angle = static_cast<float>(i) * (2.0f * kPi / static_cast<float>(RadClimateShelterPolicy::kWallRays));
        const RE::NiPoint3 target{
            origin.x + std::cos(angle) * kWallDistance,
            origin.y + std::sin(angle) * kWallDistance,
            origin.z
        };
        hits += RadInternal::RayHitsStructure(cell, origin, target) ? 1 : 0;
    }
    return hits;
}
