#pragma once

#include "RadIniParse.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace RadReliefPolicy
{
    struct FormRef
    {
        std::string   plugin;
        std::uint32_t localID{ 0 };
    };

    inline std::string_view Trim(std::string_view a_text)
    {
        const auto first = a_text.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos)
            return {};
        const auto last = a_text.find_last_not_of(" \t\r\n");
        return a_text.substr(first, last - first + 1);
    }

    // Constraint: one raw token per comma; empty tokens are dropped so a trailing comma is not an error.
    inline std::vector<std::string_view> SplitEntries(std::string_view a_raw)
    {
        std::vector<std::string_view> out;
        std::size_t                   start = 0;
        for (;;)
        {
            const auto comma = a_raw.find(',', start);
            const auto piece = comma == std::string_view::npos ? a_raw.substr(start) : a_raw.substr(start, comma - start);
            const auto trimmed = Trim(piece);
            if (!trimmed.empty())
                out.push_back(trimmed);
            if (comma == std::string_view::npos)
                break;
            start = comma + 1;
        }
        return out;
    }

    // Constraint: "Plugin.esp:0xLOCALID" only, and the local id must stay below 0x01000000.
    inline bool ParseFormRef(std::string_view a_entry, FormRef& a_out)
    {
        const auto text = Trim(a_entry);
        const auto colon = text.rfind(':');
        if (colon == std::string_view::npos || colon == 0 || colon + 1 >= text.size())
            return false;

        const auto plugin = Trim(text.substr(0, colon));
        auto       id = Trim(text.substr(colon + 1));
        if (plugin.empty() || id.empty())
            return false;
        std::uint32_t value = 0;
        if (!RadIniParse::ParseNumber(id, 0xFFFFFFu, value))
            return false;
        if (value == 0)
            return false;

        a_out.plugin.assign(plugin);
        a_out.localID = value;
        return true;
    }

    // Constraint: a malformed entry is skipped and counted, never guessed.
    inline std::vector<FormRef> ParseList(std::string_view a_raw, std::size_t* a_invalidCount = nullptr)
    {
        std::vector<FormRef> out;
        std::size_t          invalid = 0;
        for (const auto piece : SplitEntries(a_raw))
        {
            FormRef ref;
            if (ParseFormRef(piece, ref))
                out.push_back(std::move(ref));
            else
                ++invalid;
        }
        if (a_invalidCount)
            *a_invalidCount = invalid;
        return out;
    }
}
