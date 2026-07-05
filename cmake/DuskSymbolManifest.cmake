include_guard(GLOBAL)

# Shared builder for dusk-symgen (tools/symgen). Creates the dusk_symgen target and sets
# DUSK_SYMGEN_EXE. With required=FALSE, missing cargo just skips (callers must check the
# target exists).
function(dusk_ensure_symgen required)
    if (TARGET dusk_symgen)
        return()
    endif ()
    find_program(DUSK_CARGO cargo)
    if (NOT DUSK_CARGO)
        if (required)
            message(FATAL_ERROR "dusk: cargo is required to build dusk-symgen for Windows code "
                    "mods (install Rust, or configure with -DDUSK_ENABLE_CODE_MODS=OFF)")
        endif ()
        message(STATUS "dusk: cargo not found — symbol manifest generation skipped "
                "(by-name hook resolution will be unavailable)")
        return()
    endif ()

    set(_symgen_dir "${CMAKE_BINARY_DIR}/symgen")
    set(_symgen "${_symgen_dir}/release/dusk-symgen${CMAKE_EXECUTABLE_SUFFIX}")
    add_custom_command(
            OUTPUT "${_symgen}"
            COMMAND "${DUSK_CARGO}" build --release --quiet --target-dir "${_symgen_dir}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/tools/symgen"
            DEPENDS "${CMAKE_SOURCE_DIR}/tools/symgen/Cargo.toml"
                    "${CMAKE_SOURCE_DIR}/tools/symgen/src/main.rs"
                    "${CMAKE_SOURCE_DIR}/tools/symgen/src/manifest.rs"
            COMMENT "Building dusk-symgen"
            VERBATIM)
    add_custom_target(dusk_symgen DEPENDS "${_symgen}")
    set(DUSK_SYMGEN_EXE "${_symgen}" CACHE INTERNAL "dusk-symgen executable")
endfunction()

# Post-link symbol manifest for the game binary (MODS_LINKING.md §3.C): the full
# hookable surface (including statics) as name → RVA, keyed to the exact build
# (PDB GUID+age / Mach-O UUID / GNU build-id). The runtime loads it from
# SDL_GetBasePath(): the executable directory on Windows/Linux, Contents/Resources
# in the macOS bundle. Regenerates whenever the target relinks.
function(dusk_setup_symbol_manifest target)
    dusk_ensure_symgen(FALSE)
    if (NOT TARGET dusk_symgen)
        return()
    endif ()
    add_dependencies(${target} dusk_symgen)

    if (WIN32)
        set(_input --pdb "$<TARGET_PDB_FILE:${target}>")
        if (DUSK_GAME_DEF)
            # emit-def and emit-manifest share a symbol model; cross-check that every
            # curated export is present in the manifest.
            list(APPEND _input --verify-def "${DUSK_GAME_DEF}")
        endif ()
        set(_out "$<TARGET_FILE_DIR:${target}>/dusklight.symdb")
    else ()
        set(_input --binary "$<TARGET_FILE:${target}>")
        if (APPLE)
            # Generator expression, not get_target_property: the bundle properties are
            # set later in CMakeLists, but this must be attached before the codesign
            # POST_BUILD so the manifest lands inside the sealed bundle.
            set(_out "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources/dusklight.symdb")
        else ()
            set(_out "$<TARGET_FILE_DIR:${target}>/dusklight.symdb")
        endif ()
    endif ()

    get_filename_component(_out_dir "${_out}" DIRECTORY) # temp
    add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E rm -f "${_out_dir}/dusklight.manifest"
            COMMAND "${DUSK_SYMGEN_EXE}" emit-manifest ${_input} --out "${_out}"
            COMMENT "Generating symbol manifest (dusk-symgen emit-manifest)"
            VERBATIM)
endfunction()
