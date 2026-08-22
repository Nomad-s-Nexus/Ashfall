-- Set minimum xmake version
set_xmakever("3.0.0")

-- Include CommonLibF4 from workspace root (project flattened up one level 2026-06-22:
-- was ../../../CommonLibF4 when nested in Rad Mod/Rad Mod/, now ../../CommonLibF4)
local commonlib_path = os.getenv("COMMONLIBF4_PATH") or "../../CommonLibF4"
includes(commonlib_path)

-- Set project
set_project("FalloutAnomalyRadiation")
set_version("1.0.0")
set_languages("c++23")

-- Set defaults
set_warnings("allextra")
set_encodings("utf-8")

-- Add rules
add_rules("mode.release")

-- Require dependencies
add_requires("spdlog v1.16.0", "fmt", "nlohmann_json")

-- Define target
target("FalloutAnomalyRadiation")
    set_kind("shared")
    set_arch("x64")
    set_filename("FalloutAnomalyRadiation.dll")

    -- Add dependencies
    add_deps("commonlibf4")

    -- Add packages
    add_packages("spdlog", "fmt", "nlohmann_json")

    -- Source files
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")

    -- Precompiled header
    set_pcxxheader("src/pch.h")

    -- Define common macros
    add_defines("COMMONLIB_RUNTIMECOUNT=3")

    -- Win32 APIs used directly by the DLL plus the standard CommonLibF4 link set.
    -- Keep these explicit so clang-cl/lld-link cross-builds do not depend on MSVC
    -- header pragma side effects or transitive links from another target.
    if is_plat("windows") then
        add_syslinks("bcrypt", "version", "User32", "Ole32", "OleAut32", "crypt32", "Shell32")
    end

    -- Set output directory
    set_targetdir("build/release")

    -- Auto-deploy the built DLL to the live modlist so testing never runs a stale copy.
    -- Honours XSE_FO4_MODS_PATH if set; otherwise falls back to the known live modlist path.
    -- Uses trycp + isdir so a missing folder or a file-locked DLL (game running) never fails the build.
    after_build(function (target)
        local dll = target:targetfile()
        local dests = {}
        local mods = os.getenv("XSE_FO4_MODS_PATH")
        if mods then
            table.insert(dests, path.join(mods, "Fallen World Rad Overhaul", "F4SE", "Plugins"))
        end
        table.insert(dests, "E:/Modlists/Fallen World Alpha 2/mods/Fallen World Rad Overhaul/F4SE/Plugins")
        for _, d in ipairs(dests) do
            if os.isdir(d) then
                if os.trycp(dll, path.join(d, "FalloutAnomalyRadiation.dll")) then
                    print("[deploy] FalloutAnomalyRadiation.dll -> " .. d)
                else
                    print("[deploy] SKIPPED (locked? close FO4) -> " .. d)
                end
            end
        end
    end)
