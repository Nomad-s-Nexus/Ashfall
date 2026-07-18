#include "pch.h"
#include "RadiationConfig.h"

bool RadiationConfig::LoadFromFile(const std::string& basePath)
{
    auto logger = spdlog::get("FalloutAnomaly");

    try
    {
        // Only thresholds.json is consumed by the engine (the radiation stage table, read via
        // GetStageConfig in ApplyDebuffs). The old zones/items/perks config belonged to the
        // abandoned C++-simulation design and is no longer loaded — those mechanics now live in
        // the game/ESL records (vanilla rad regions, item magic effects, the stage perks).
        // Load thresholds.json
        std::ifstream thresholdsFile(basePath + "/thresholds.json");
        if (!thresholdsFile.is_open())
        {
            // Loud, not fatal: without the stage table every stage debuff/perk is silently dead,
            // which previously produced a "initialized successfully" log with a fully inert stage
            // system and zero diagnostic. The rest of the mod (storms, masks, tissue) still works.
            logger->error("thresholds.json missing/unreadable at '{}/thresholds.json' — radiation "
                          "stage debuffs and Pip-Boy stage perks are DISABLED this session.", basePath);
            return true;
        }

        json thresholdsJson;
        thresholdsFile >> thresholdsJson;
        if (thresholdsJson.contains("stages"))
        for (const auto& [level, data] : thresholdsJson["stages"].items())
        {
            uint32_t radLevel = std::stoul(level);
            StageConfig stage;
            // Only the stage NAME is used at runtime; the json's penalty/limb/health/etc. fields
            // are design leftovers (see StageConfig in RadiationConfig.h).
            stage.name = data.value("name", std::string{});
            _stages[radLevel] = stage;
        }
        if (_stages.empty())
            logger->error("thresholds.json parsed but contains no 'stages' — stage debuffs DISABLED.");
        else
            logger->info("Loaded {} radiation stages from config.", _stages.size());

        return true;
    }
    catch (const std::exception& e)
    {
        logger->error("Failed to load config: {}", e.what());
        return false;
    }
}

const StageConfig* RadiationConfig::GetStageConfig(uint32_t radLevel) const
{
    if (_stages.empty())
    {
        return nullptr;
    }

    // Find the highest stage <= radLevel
    auto it = _stages.upper_bound(radLevel);
    if (it == _stages.begin())
    {
        return nullptr;  // Below lowest stage
    }
    --it;
    return &it->second;
}
