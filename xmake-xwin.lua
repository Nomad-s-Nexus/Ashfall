-- Cross-compile Rad Overhaul for Windows x64 from Linux with clang-cl + xwin's
-- MSVC CRT/SDK. This mirrors the working CombatMod self-hosted CI path.
includes("toolchains/xwin-clang-cl.lua")
set_toolchains("xwin-clang-cl")
set_runtimes("MD")

-- Searched before xwin's SDK libpath so lld-link can resolve the normal
-- Windows-conventional import-lib casing on this case-sensitive Linux host.
local libshim = os.getenv("XWIN_LIBSHIM")
if libshim then
    add_linkdirs(libshim)
end

-- CommonLibF4 cross-build requirements already proven in CombatMod/Prisma-Matrix.
add_requireconfs("spdlog", {configs = {header_only = true, wchar = true, std_format = true}, override = true})
add_requireconfs("fmt", {configs = {header_only = true}, override = true})

add_defines("_CRT_USE_BUILTIN_OFFSETOF")
add_defines("NOGDI", "NOMINMAX", "WIN32_LEAN_AND_MEAN")
add_defines("SPDLOG_COMPILED_LIB", "SPDLOG_USE_STD_FORMAT", "SPDLOG_WCHAR_TO_UTF8_SUPPORT")

-- Prebuilt Windows spdlog package staged on the org self-hosted runner.
local pkgroot = os.getenv("PRISMA_WIN_PACKAGES") or "/opt/prisma-toolchain/win-packages"

local function die(msg)
    print("error: " .. msg)
    return STAGED_WINDOWS_TOOLCHAIN_NOT_FOUND()
end

local function staged_lib(letter, name, libname)
    for _, dir in ipairs(os.dirs(path.join(pkgroot, letter, name, "*", "*", "lib"))) do
        if os.isfile(path.join(dir, libname .. ".lib")) then
            return dir
        end
    end
    die("staged " .. libname .. ".lib not found under " .. pkgroot ..
        " -- set PRISMA_WIN_PACKAGES")
end

add_linkdirs(staged_lib("s", "spdlog", "spdlog"))
add_links("spdlog")

includes("xmake.lua")
