include_guard(GLOBAL)

get_filename_component(_DUSK_WINDOWS_EXPORTS_CMAKE_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)

# Windows mod linking: generate the curated export surface for the game executable and the
# import library mods link against. dusk-symgen (tools/symgen) scans the built objects with
# provenance filtering and writes the .def consumed by the executable's link; the implib is
# derived from the same .def as a tool-owned artifact. Design + measurements: MODS_LINKING.md.
function(dusk_setup_windows_exports target)
    if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(WARNING "dusk: Windows code-mod exports are x64-only for now; skipping")
        return()
    endif ()

    include("${_DUSK_WINDOWS_EXPORTS_CMAKE_DIR}/DuskSymbolManifest.cmake")
    dusk_ensure_symgen(TRUE)
    set(_symgen "${DUSK_SYMGEN_EXE}")
    add_dependencies(${target} dusk_symgen)

    # Inputs: the target's own objects (src/ + libs/ + rel) and the JSystem archives.
    # A .def export of an archive symbol acts as a reference and pulls the member, so the
    # archives don't need /WHOLEARCHIVE (verified against link.exe and lld-link).
    set(_rsp_lines "$<TARGET_OBJECTS:${target}>")
    foreach (_lib IN LISTS JSYSTEM_LIBRARIES)
        list(APPEND _rsp_lines "$<TARGET_FILE:${_lib}>")
    endforeach ()
    list(JOIN _rsp_lines "\n" _rsp_content)
    set(_rsp "${CMAKE_BINARY_DIR}/dusklight_exports_input.rsp")
    file(GENERATE OUTPUT "${_rsp}" CONTENT "${_rsp_content}")

    # The Dolphin SDK C surface comes from Aurora's implementation libraries (C symbols only —
    # Aurora's own C++/C APIs are not part of the mod surface; services are).
    # aurora_ms is deliberately absent: it isn't in the game's link closure on any platform,
    # and a .def export with no linked definition is a hard link error.
    set(_sdk_args)
    foreach (_lib aurora_card aurora_core aurora_dvd aurora_gd aurora_gx aurora_mtx
             aurora_os aurora_pad aurora_si aurora_vi)
        if (TARGET ${_lib})
            list(APPEND _sdk_args --sdk-lib "$<TARGET_FILE:${_lib}>")
        endif ()
    endforeach ()

    set(_def "${CMAKE_BINARY_DIR}/dusklight_exports.def")
    add_custom_command(TARGET ${target} PRE_LINK
            # src/dusk/ is deliberately NOT excluded: TARGET_PC inline code in game headers
            # calls into it (e.g. dusk::frame_interp::lookup_replacement), and macOS exports
            # everything anyway. The services boundary is policy, not symbol visibility.
            COMMAND "${_symgen}" emit-def
                --rsp "${_rsp}"
                --out "${_def}"
                --report "${CMAKE_BINARY_DIR}/dusklight_exports_report.txt"
                --exclude cmake_pch
                --exclude miniz
                --exclude asan_options
                --max-exports 58000
                ${_sdk_args}
            COMMENT "Generating dusklight export surface (dusk-symgen emit-def)"
            VERBATIM)
    target_link_options(${target} PRIVATE "/DEF:${_def}")

    # Import library mods link against. Prefer llvm-dlltool (next to clang-cl); lib.exe-style
    # CMAKE_AR works too. Named after the executable module so the OS loader binds mod imports
    # against the running dusklight.exe.
    set(_implib "${CMAKE_BINARY_DIR}/dusklight_imports.lib")
    get_filename_component(_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(DUSK_LLVM_DLLTOOL llvm-dlltool HINTS "${_compiler_dir}")
    if (DUSK_LLVM_DLLTOOL)
        set(_implib_cmd "${DUSK_LLVM_DLLTOOL}" -d "${_def}" -D dusklight.exe -m i386:x86-64
                -l "${_implib}")
    else ()
        set(_implib_cmd "${CMAKE_AR}" /nologo "/def:${_def}" /machine:x64 /name:dusklight.exe
                "/out:${_implib}")
    endif ()
    add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${_implib_cmd}
            BYPRODUCTS "${_implib}"
            COMMENT "Generating dusklight import library"
            VERBATIM)
    set(DUSK_GAME_IMPLIB "${_implib}" CACHE INTERNAL "Import library for Windows mod linking")
    set(DUSK_GAME_DEF "${_def}" CACHE INTERNAL "Curated export .def for the game executable")
endfunction()
