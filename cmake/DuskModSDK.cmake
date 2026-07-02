# add_dusk_mod(<target> SOURCES <file>... MOD_JSON <mod.json> [RES_DIR <res>] [OVERLAY_DIR <overlay>]
#              [OUTPUT_DIR <dir>])
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

function(add_dusk_mod target_name)
    cmake_parse_arguments(ARG "" "MOD_JSON;RES_DIR;OVERLAY_DIR;OUTPUT_DIR" "SOURCES" ${ARGN})
    if(NOT ARG_MOD_JSON)
        message(FATAL_ERROR "add_dusk_mod: MOD_JSON is required")
    endif()

    add_library(${target_name} SHARED ${ARG_SOURCES})
    _dusk_mod_arch_suffix(_arch_suffix)
    set_target_properties(${target_name} PROPERTIES
        PREFIX "" OUTPUT_NAME "${target_name}${_arch_suffix}" WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_compile_features(${target_name} PRIVATE cxx_std_20)
    target_link_libraries(${target_name} PRIVATE dusklight_game_headers)
    add_dependencies(dusklight ${target_name}) # Rebuild mod on main target

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
    file(MAKE_DIRECTORY "${_stage}")  # must exist before POST_BUILD on Windows

    set(_zip_args "$<TARGET_FILE_NAME:${target_name}>" mod.json)
    set(_extra_cmds "")
    if(ARG_RES_DIR)
        list(APPEND _zip_args res)
        list(APPEND _extra_cmds COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/${ARG_RES_DIR}" "${_stage}/res")
    endif()
    if(ARG_OVERLAY_DIR)
        list(APPEND _zip_args overlay)
        list(APPEND _extra_cmds COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/${ARG_OVERLAY_DIR}" "${_stage}/overlay")
    endif()

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_stage}" "${_output_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target_name}>" "${_stage}/$<TARGET_FILE_NAME:${target_name}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/${ARG_MOD_JSON}" "${_stage}/mod.json"
        ${_extra_cmds}
        COMMAND ${CMAKE_COMMAND} -E tar cvf "${_out}" --format=zip ${_zip_args}
        WORKING_DIRECTORY "${_stage}"
        COMMENT "Packaging ${target_name} -> ${_out}"
    )
endfunction()
