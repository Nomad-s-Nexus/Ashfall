-- Set minimum xmake version
set_xmakever("3.0.0")

-- CommonLib authority: PRISMA-USER-INTERFACE-FRAMEWORK/commonlib_NonVR is the ONLY supported RE source.
-- CI/release clone it and set COMMONLIBF4_PATH; the local-dev default below must also be a
-- commonlib_NonVR checkout. Do NOT point this at Dear-Modding-FO4/CommonLibF4 or any other CommonLib.
if is_plat("windows") then
    local commonlib_path = os.getenv("COMMONLIBF4_PATH") or "../../commonlib_NonVR"
    -- Fail loud if the checkout is not commonlib_NonVR. RE-Work/ is a directory unique to our fork (absent
    -- from Dear-Modding/upstream CommonLibF4), so this stops an unrelated CommonLib silently becoming
    -- authoritative — including a stray ../../CommonLibF4 left over from an older layout.
    if not os.isdir(path.join(commonlib_path, "RE-Work")) then
        raise("CommonLib authority check FAILED: '" .. commonlib_path .. "' is not a commonlib_NonVR checkout " ..
            "(RE-Work/ marker missing). Set COMMONLIBF4_PATH to a PRISMA-USER-INTERFACE-FRAMEWORK/commonlib_NonVR clone.")
    end
    includes(commonlib_path)
end

-- Set project
set_project("FalloutAnomalyRadiation")
set_version("1.0.0")
set_languages("c++23")

-- Set defaults
set_warnings("allextra")
set_encodings("utf-8")

-- These packages are needed only by the Windows plugin build. Keep the standalone
-- policy test target independent on other hosts.
if is_plat("windows") then
    add_requires("spdlog v1.16.0", "fmt", "nlohmann_json")
end

-- Add rules
add_rules("mode.release")

-- Define target
target("FalloutAnomalyRadiation")
    set_kind("shared")
    set_arch("x64")
    set_filename("FalloutAnomalyRadiation.dll")

    -- Add dependencies
    if is_plat("windows") then
        add_deps("commonlibf4")
    end

    -- Add packages
    if is_plat("windows") then
        add_packages("spdlog", "fmt", "nlohmann_json")
    end

    -- Source files
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")

    -- Precompiled header
    set_pcxxheader("src/pch.h")

    -- COMMONLIB_RUNTIMECOUNT is intentionally NOT defined here: commonlib_NonVR exports it as a public
    -- define via add_deps("commonlibf4"), so the runtime count has a single authoritative owner and
    -- cannot drift from the CommonLib it is compiled against.

    -- Win32 APIs used directly by the DLL plus the standard CommonLibF4 link set.
    -- Keep these explicit so clang-cl/lld-link cross-builds do not depend on MSVC
    -- header pragma side effects or transitive links from another target.
    if is_plat("windows") then
        add_syslinks("bcrypt", "version", "User32", "Ole32", "OleAut32", "crypt32", "Shell32")
    end

    -- Set output directory
    set_targetdir("build/release")

    -- Auto-deploy the built DLL to the active live modlist so testing never runs a stale copy.
    -- Honours XSE_FO4_MODS_PATH if set; otherwise falls back to the known live modlist path.
    -- Uses trycp + isdir so a missing folder or a file-locked DLL (game running) never fails the build.
    after_build(function (target)
        local dll = target:targetfile()
        local dests = {}
        local mods = os.getenv("XSE_FO4_MODS_PATH")
        if mods then
            table.insert(dests, path.join(mods, "RadiationOverhaul", "F4SE", "Plugins"))
        end
        table.insert(dests, "E:/Modlists/Fallen World Alpha 2/mods/RadiationOverhaul/F4SE/Plugins")
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

target("rad_correctness_policy_tests")
    set_kind("binary")
    add_files("tests/correctness_policy_tests.cpp")
    add_includedirs("src")
    set_targetdir("build/tests")
