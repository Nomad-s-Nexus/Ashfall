#pragma once

// One radiation severity stage from thresholds.json. Only the NAME is consumed at runtime (stage
// banner / logging). The json's endPenalty/strPenalty (and the old limb/health/fast-travel/radaway
// fields) are design leftovers the DLL deliberately does NOT apply — the real SPECIAL penalties are
// RadiationEngine::kStagePenalties, and the other mechanics live in the ESP records.
struct StageConfig
{
    std::string name;
};

class RadiationConfig
{
public:
    static RadiationConfig& GetSingleton()
    {
        static RadiationConfig instance;
        return instance;
    }

    bool LoadFromFile(const std::string& basePath);

    const StageConfig* GetStageConfig(uint32_t radLevel) const;

private:
    RadiationConfig() = default;
    ~RadiationConfig() = default;

    std::map<uint32_t, StageConfig> _stages;  // Sorted by rad level
};
