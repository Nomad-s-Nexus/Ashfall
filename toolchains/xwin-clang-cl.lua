local xwin_root = os.getenv("XWIN_ROOT") or "/opt/xwin"

local include_flags = {
    "--target=x86_64-pc-windows-msvc",
    "/imsvc" .. path.join(xwin_root, "crt/include"),
    "/imsvc" .. path.join(xwin_root, "sdk/include/ucrt"),
    "/imsvc" .. path.join(xwin_root, "sdk/include/um"),
    "/imsvc" .. path.join(xwin_root, "sdk/include/shared"),
    "/imsvc" .. path.join(xwin_root, "sdk/include/winrt")
}

local library_flags = {
    "/libpath:" .. path.join(xwin_root, "crt/lib/x86_64"),
    "/libpath:" .. path.join(xwin_root, "sdk/lib/ucrt/x86_64"),
    "/libpath:" .. path.join(xwin_root, "sdk/lib/um/x86_64")
}

toolchain("xwin-clang-cl")
    set_kind("standalone")
    set_toolset("cc", "clang-cl")
    set_toolset("cxx", "clang-cl")
    set_toolset("ld", "lld-link")
    set_toolset("sh", "lld-link")
    set_toolset("ar", "llvm-ar")
    set_toolset("mrc", "llvm-rc")
    set_toolset("dlltool", "llvm-dlltool")

    on_load(function (toolchain)
        toolchain:add("mrcflags", "/I" .. path.join(xwin_root, "sdk/include/um"))
        toolchain:add("mrcflags", "/I" .. path.join(xwin_root, "sdk/include/shared"))
        toolchain:add("mrcflags", "/I" .. path.join(xwin_root, "sdk/include/ucrt"))
        toolchain:add("mrcflags", "/I" .. path.join(xwin_root, "crt/include"))
        toolchain:add("cxflags", table.unpack(include_flags))
        toolchain:add("ldflags", table.unpack(library_flags))
        toolchain:add("shflags", table.unpack(library_flags))
    end)
