#include "pch.h"
#include "RadReliefConfig.h"

#include "Hooks.h"
#include "RadReliefPolicy.h"

#include <RE/B/BGSKeyword.h>
#include <RE/B/BGSKeywordForm.h>
#include <RE/B/BSScriptUtil.h>
#include <RE/B/BSScript_IVirtualMachine.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESFormUtil.h>
#include <RE/T/TESQuest.h>

#include <algorithm>
#include <atomic>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <Windows.h>

namespace
{
    constexpr const char* kIni = ".\\Data\\F4SE\\Plugins\\FalloutAnomalyRadiation.ini";
    constexpr DWORD       kIniBufferSize = 4096;

    std::atomic_bool g_loaded{ false };

    std::vector<RE::BGSKeyword*> g_foodKeywords;
    std::vector<RE::BGSKeyword*> g_thirstKeywords;
    std::vector<RE::TESForm*>    g_foodItems;
    std::vector<RE::TESForm*>    g_thirstItems;

    std::string ReadIniList(const char* a_key, bool& a_truncated)
    {
        std::string buffer(kIniBufferSize, '\0');
        const auto  size = ::GetPrivateProfileStringA("Relief", a_key, "", buffer.data(), kIniBufferSize, kIni);
        a_truncated = size == kIniBufferSize - 1;
        buffer.resize(size);
        return buffer;
    }

    void ReportEntry(const char* a_key, std::string_view a_raw, std::string_view a_reason)
    {
        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->warn("[Relief] {} entry '{}': {}.", a_key, std::string(a_raw), a_reason);
    }

    void ResolveEntries(RE::TESDataHandler* a_data, const char* a_key, bool a_requireKeyword, std::vector<RE::TESForm*>& a_out)
    {
        bool       truncated = false;
        const auto raw = ReadIniList(a_key, truncated);
        auto       logger = spdlog::get("FalloutAnomaly");
        if (truncated && logger)
            logger->warn("[Relief] {} is longer than {} bytes; only the first part is used.", a_key, kIniBufferSize - 1);

        std::size_t rejected = 0;
        for (const auto token : RadReliefPolicy::SplitEntries(raw))
        {
            RadReliefPolicy::FormRef ref;
            if (!RadReliefPolicy::ParseFormRef(token, ref))
            {
                ++rejected;
                ReportEntry(a_key, token, "malformed, expected Plugin.esp:0xLOCALID");
                continue;
            }
            auto* form = a_data->LookupForm(ref.localID, ref.plugin.c_str());
            if (!form)
            {
                ++rejected;
                ReportEntry(a_key, token, "plugin or form not found");
                continue;
            }
            if (a_requireKeyword && form->GetFormType() != RE::ENUM_FORM_ID::kKYWD)
            {
                ++rejected;
                ReportEntry(a_key, token, "not a KYWD record");
                continue;
            }
            a_out.push_back(form);
        }
        if (logger)
            logger->info("[Relief] {}: {} resolved, {} rejected.", a_key, a_out.size(), rejected);
    }

    void ResolveKeywordList(RE::TESDataHandler* a_data, const char* a_key, std::vector<RE::BGSKeyword*>& a_out)
    {
        std::vector<RE::TESForm*> forms;
        ResolveEntries(a_data, a_key, true, forms);
        for (auto* form : forms)
            a_out.push_back(static_cast<RE::BGSKeyword*>(form));
    }

    bool MatchesKeyword(const RE::TESForm* a_form, const std::vector<RE::BGSKeyword*>& a_keywords)
    {
        const auto* keywordForm = a_form ? a_form->As<RE::BGSKeywordForm>() : nullptr;
        return keywordForm && std::any_of(a_keywords.begin(), a_keywords.end(),
            [keywordForm](const RE::BGSKeyword* a_keyword) { return a_keyword && keywordForm->HasKeyword(a_keyword); });
    }

    bool MatchesForm(const RE::TESForm* a_form, const std::vector<RE::TESForm*>& a_forms)
    {
        return a_form && std::any_of(a_forms.begin(), a_forms.end(),
            [a_form](const RE::TESForm* a_candidate) { return a_candidate == a_form; });
    }

    bool LoadIniLists()
    {
        if (!Hooks::IsGameMainThread())
            return false;
        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data)
            return false;

        ResolveKeywordList(data, "ExtraFoodKeywords", g_foodKeywords);
        ResolveKeywordList(data, "ExtraThirstKeywords", g_thirstKeywords);
        ResolveEntries(data, "ExtraFoodItems", false, g_foodItems);
        ResolveEntries(data, "ExtraThirstItems", false, g_thirstItems);
        g_loaded.store(true, std::memory_order_release);
        return true;
    }

    // Constraint: Papyrus can call before the lifecycle hook, so the getters retry the same one-shot load.
    bool Ready()
    {
        if (g_loaded.load(std::memory_order_acquire))
            return true;
        return Hooks::IsGameMainThread() && LoadIniLists();
    }

    int PapyrusGetReliefFlags(std::monostate, RE::TESForm* a_form)
    {
        return RadReliefConfig::ReliefFlags(a_form);
    }

    bool F4SEAPI RegisterPapyrusFunctions(RE::BSScript::IVirtualMachine* a_vm)
    {
        if (!a_vm)
            return false;
        a_vm->BindNativeMethod("RadNatives", "GetReliefFlags", PapyrusGetReliefFlags, std::nullopt, false);
        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->info("[Relief] Papyrus bridge registered (GetReliefFlags).");
        return true;
    }
}

void RadReliefConfig::InstallPapyrusBridge()
{
    auto* papyrus = F4SE::GetPapyrusInterface();
    if (!papyrus || !papyrus->Register(RegisterPapyrusFunctions))
    {
        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->warn("[Relief] Papyrus bridge unavailable or registration failed; INI relief extras stay OFF.");
    }
}

void RadReliefConfig::ActivateOnGameThread()
{
    if (g_loaded.load(std::memory_order_acquire))
        return;
    if (!Ready())
    {
        if (auto logger = spdlog::get("FalloutAnomaly"))
            logger->warn("[Relief] INI relief lists could not be resolved yet; retrying on the next game-thread call.");
    }
}

int RadReliefConfig::ReliefFlags(const RE::TESForm* a_form)
{
    if (!Ready() || !a_form)
        return 0;
    int flags = 0;
    if (MatchesKeyword(a_form, g_foodKeywords) || MatchesForm(a_form, g_foodItems))
        flags |= 1;
    if (MatchesKeyword(a_form, g_thirstKeywords) || MatchesForm(a_form, g_thirstItems))
        flags |= 2;
    return flags;
}
