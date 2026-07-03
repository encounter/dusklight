# add_dusk_mod(<target> SOURCES <file>... MOD_JSON <mod.json> [RES_DIR <res>] [OVERLAY_DIR <overlay>]
#              [TEXTURES_DIR <textures>] [OUTPUT_DIR <dir>])
set(DUSK_MODS_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/mods" CACHE PATH "Directory to write .dusk packages into")

# The loader matches libraries by platform extension + architecture suffix
# (_arm64/_x64/_x86); an unsuffixed name is treated as arch-neutral.
function(_dusk_mod_arch_suffix out_var)
    set(_arch "${CMAKE_SYSTEM_PROCESSOR}")
    if(APPLE AND CMAKE_OSX_ARCHITECTURES)
        list(LENGTH CMAKE_OSX_ARCHITECTURES _count)
        if(_count GREATER 1)
            # Universal binary: leave the name arch-neutral
            set(${out_var} "" PARENT_SCOPE)
            return()
        endif()
        set(_arch "${CMAKE_OSX_ARCHITECTURES}")
    endif()
    if(_arch MATCHES "^(arm64|aarch64|ARM64)$")
        set(${out_var} "_arm64" PARENT_SCOPE)
    elseif(_arch MATCHES "^(x86_64|AMD64|amd64)$")
        set(${out_var} "_x64" PARENT_SCOPE)
    elseif(_arch MATCHES "^(i[3-6]86|x86|X86)$")
        set(${out_var} "_x86" PARENT_SCOPE)
    else()
        set(${out_var} "" PARENT_SCOPE)
    endif()
endfunction()

function(_dusk_mod_resolve_source_path out_var path)
    if(IS_ABSOLUTE "${path}")
        set(_path "${path}")
    else()
        set(_path "${CMAKE_CURRENT_SOURCE_DIR}/${path}")
    endif()
    set(${out_var} "${_path}" PARENT_SCOPE)
endfunction()

function(_dusk_mod_collect_assets out_var dir)
    if(NOT IS_DIRECTORY "${dir}")
        message(FATAL_ERROR "add_dusk_mod: asset directory does not exist: ${dir}")
    endif()

    file(GLOB_RECURSE _files CONFIGURE_DEPENDS LIST_DIRECTORIES false "${dir}/*")
    set(${out_var} ${_files} PARENT_SCOPE)
endfunction()

function(add_dusk_mod target_name)
    cmake_parse_arguments(ARG "" "MOD_JSON;RES_DIR;OVERLAY_DIR;TEXTURES_DIR;OUTPUT_DIR" "SOURCES" ${ARGN})
    if(NOT ARG_MOD_JSON)
        message(FATAL_ERROR "add_dusk_mod: MOD_JSON is required")
    endif()
    _dusk_mod_resolve_source_path(_mod_json "${ARG_MOD_JSON}")
    if(NOT EXISTS "${_mod_json}")
        message(FATAL_ERROR "add_dusk_mod: MOD_JSON does not exist: ${_mod_json}")
    endif()

    add_library(${target_name} SHARED ${ARG_SOURCES})
    _dusk_mod_arch_suffix(_arch_suffix)
    set_target_properties(${target_name} PROPERTIES
        PREFIX "" OUTPUT_NAME "${target_name}${_arch_suffix}"
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        WINDOWS_EXPORT_ALL_SYMBOLS OFF)
    target_compile_features(${target_name} PRIVATE cxx_std_20)
    target_link_libraries(${target_name} PRIVATE dusklight_game_headers)

    # webgpu.h for the gfx service. Header-only on macOS/Linux: wgpu* symbols resolve against
    # the host executable's exports at load time (never link static Dawn into a mod — that would
    # create a second Dawn instance). Windows links the import lib for the DLL the host loads.
    if(TARGET dawn::webgpu_dawn)
        if(WIN32)
            target_link_libraries(${target_name} PRIVATE dawn::webgpu_dawn)
        else()
            target_include_directories(${target_name} PRIVATE
                $<TARGET_PROPERTY:dawn::webgpu_dawn,INTERFACE_INCLUDE_DIRECTORIES>)
        endif()
    endif()

    if(APPLE)
        target_link_options(${target_name} PRIVATE -undefined dynamic_lookup)
    elseif(UNIX)
        target_link_options(${target_name} PRIVATE -Wl,--allow-shlib-undefined)
    elseif(WIN32)
        target_link_libraries(${target_name} PRIVATE dusklight_game)
        if(MSVC)
            target_link_options(${target_name} PRIVATE /INCREMENTAL:NO)
            set_target_properties(${target_name} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        endif()
    endif()


    set(_output_dir "${DUSK_MODS_OUTPUT_DIR}")
    if(ARG_OUTPUT_DIR)
        set(_output_dir "${ARG_OUTPUT_DIR}")
    endif()
    set(_stage "${CMAKE_CURRENT_BINARY_DIR}/${target_name}_stage")
    set(_out   "${_output_dir}/${target_name}.dusk")

    set(_zip_args "$<TARGET_FILE_NAME:${target_name}>" mod.json)
    set(_package_deps "${_mod_json}")
    set(_package_inputs "${_mod_json}")
    set(_extra_cmds "")
    if(ARG_RES_DIR)
        _dusk_mod_resolve_source_path(_res_dir "${ARG_RES_DIR}")
        _dusk_mod_collect_assets(_res_deps "${_res_dir}")
        list(APPEND _package_deps ${_res_deps})
        list(APPEND _package_inputs "${_res_dir}" ${_res_deps})
        list(APPEND _zip_args res)
        list(APPEND _extra_cmds COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_res_dir}" "${_stage}/res")
    endif()
    if(ARG_OVERLAY_DIR)
        _dusk_mod_resolve_source_path(_overlay_dir "${ARG_OVERLAY_DIR}")
        _dusk_mod_collect_assets(_overlay_deps "${_overlay_dir}")
        list(APPEND _package_deps ${_overlay_deps})
        list(APPEND _package_inputs "${_overlay_dir}" ${_overlay_deps})
        list(APPEND _zip_args overlay)
        list(APPEND _extra_cmds COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_overlay_dir}" "${_stage}/overlay")
    endif()
    if(ARG_TEXTURES_DIR)
        _dusk_mod_resolve_source_path(_textures_dir "${ARG_TEXTURES_DIR}")
        _dusk_mod_collect_assets(_textures_deps "${_textures_dir}")
        list(APPEND _package_deps ${_textures_deps})
        list(APPEND _package_inputs "${_textures_dir}" ${_textures_deps})
        list(APPEND _zip_args textures)
        list(APPEND _extra_cmds COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_textures_dir}" "${_stage}/textures")
    endif()

    set(_package_target "${target_name}_package")
    set(_package_inputs_file "${CMAKE_CURRENT_BINARY_DIR}/${target_name}_package_inputs.txt")
    list(SORT _package_inputs)
    set(_package_inputs_text "")
    foreach(_package_input IN LISTS _package_inputs)
        string(APPEND _package_inputs_text "${_package_input}\n")
    endforeach()
    file(GENERATE OUTPUT "${_package_inputs_file}" CONTENT "${_package_inputs_text}")
    add_custom_command(OUTPUT "${_out}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${_stage}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_stage}" "${_output_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target_name}>" "${_stage}/$<TARGET_FILE_NAME:${target_name}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_mod_json}" "${_stage}/mod.json"
        ${_extra_cmds}
        COMMAND ${CMAKE_COMMAND} -E chdir "${_stage}" ${CMAKE_COMMAND} -E tar cvf "${_out}" --format=zip ${_zip_args}
        DEPENDS ${target_name} ${_package_deps} "${_package_inputs_file}"
        COMMENT "Packaging ${target_name} -> ${_out}"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
    add_custom_target(${_package_target} ALL DEPENDS "${_out}")
    add_dependencies(dusklight ${_package_target}) # Rebuild mod package on main target
endfunction()
