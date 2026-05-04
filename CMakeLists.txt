cmake_minimum_required(VERSION 3.20)

project(TimWinPatch NONE)

function(find_vs_x86_tool out_var tool_name)
    file(GLOB _tool_candidates
        "C:/Program Files/Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/bin/Hostx86/x86/${tool_name}"
        "C:/Program Files (x86)/Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/bin/Hostx86/x86/${tool_name}"
        "C:/Program Files/Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/bin/Hostx64/x86/${tool_name}"
        "C:/Program Files (x86)/Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/bin/Hostx64/x86/${tool_name}"
    )
    if(_tool_candidates)
        list(SORT _tool_candidates COMPARE NATURAL ORDER DESCENDING)
        list(GET _tool_candidates 0 _tool_path)
    else()
        find_program(_tool_path NAMES "${tool_name}")
    endif()
    set(${out_var} "${_tool_path}" CACHE FILEPATH "Path to 32-bit ${tool_name}" FORCE)
endfunction()

find_vs_x86_tool(TIMWIN_CL cl.exe)
find_vs_x86_tool(TIMWIN_ML ml.exe)
find_vs_x86_tool(TIMWIN_LINK link.exe)

if(NOT TIMWIN_CL)
    message(FATAL_ERROR "Could not find 32-bit MSVC cl.exe. Set TIMWIN_CL manually.")
endif()
if(NOT TIMWIN_ML)
    message(FATAL_ERROR "Could not find 32-bit MSVC ml.exe. Set TIMWIN_ML manually.")
endif()
if(NOT TIMWIN_LINK)
    message(FATAL_ERROR "Could not find 32-bit MSVC link.exe. Set TIMWIN_LINK manually.")
endif()

get_filename_component(TIMWIN_CL_DIR "${TIMWIN_CL}" DIRECTORY)
get_filename_component(_timwin_host_dir "${TIMWIN_CL_DIR}" DIRECTORY)
get_filename_component(_timwin_bin_dir "${_timwin_host_dir}" DIRECTORY)
get_filename_component(TIMWIN_MSVC_DIR "${_timwin_bin_dir}" DIRECTORY)
set(TIMWIN_MSVC_INCLUDE "${TIMWIN_MSVC_DIR}/include")
set(TIMWIN_MSVC_LIB "${TIMWIN_MSVC_DIR}/lib/x86")

file(GLOB _timwin_sdk_include_candidates
    "C:/Program Files (x86)/Windows Kits/10/Include/10.*"
)
if(NOT _timwin_sdk_include_candidates)
    message(FATAL_ERROR "Could not find Windows 10 SDK include directory.")
endif()
list(SORT _timwin_sdk_include_candidates COMPARE NATURAL ORDER DESCENDING)
list(GET _timwin_sdk_include_candidates 0 TIMWIN_SDK_INCLUDE_DIR)
get_filename_component(TIMWIN_SDK_VERSION "${TIMWIN_SDK_INCLUDE_DIR}" NAME)
set(TIMWIN_UCRT_INCLUDE "${TIMWIN_SDK_INCLUDE_DIR}/ucrt")
set(TIMWIN_UCRT_LIB "C:/Program Files (x86)/Windows Kits/10/Lib/${TIMWIN_SDK_VERSION}/ucrt/x86")
set(TIMWIN_UM_LIB "C:/Program Files (x86)/Windows Kits/10/Lib/${TIMWIN_SDK_VERSION}/um/x86")
set(TIMWIN_MT "C:/Program Files (x86)/Windows Kits/10/bin/${TIMWIN_SDK_VERSION}/x86/mt.exe")
if(NOT EXISTS "${TIMWIN_MT}")
    message(FATAL_ERROR "Could not find Windows SDK mt.exe at ${TIMWIN_MT}.")
endif()

set(TIMWIN_HOOK_BUILD_DIR "${CMAKE_BINARY_DIR}/hooks" CACHE PATH "Hook object output directory")
set(TIMWIN_HOOK_C "${CMAKE_SOURCE_DIR}/hooks/timwin_hooks.c")
set(TIMWIN_TRAMPOLINES_ASM "${CMAKE_SOURCE_DIR}/hooks/timwin_trampolines.asm")
set(TIMWIN_HOOK_C_OBJ "${TIMWIN_HOOK_BUILD_DIR}/timwin_hooks.obj")
set(TIMWIN_HOOK_C_LISTING "${TIMWIN_HOOK_BUILD_DIR}/timwin_hooks.asm")
set(TIMWIN_TRAMPOLINES_OBJ "${TIMWIN_HOOK_BUILD_DIR}/timwin_trampolines.obj")
set(TIMWIN_TRAMPOLINES_LISTING "${TIMWIN_HOOK_BUILD_DIR}/timwin_trampolines.lst")
set(SOS9502_HOOKS_ASM "${CMAKE_SOURCE_DIR}/hooks/sos9502_hooks.asm")
set(SOS9502_HOOKS_OBJ "${TIMWIN_HOOK_BUILD_DIR}/sos9502_hooks.obj")
set(SOS9502_HOOKS_LISTING "${TIMWIN_HOOK_BUILD_DIR}/sos9502_hooks.lst")

add_custom_command(
    OUTPUT "${TIMWIN_HOOK_C_OBJ}" "${TIMWIN_HOOK_C_LISTING}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TIMWIN_HOOK_BUILD_DIR}"
    COMMAND "${TIMWIN_CL}"
        /nologo
        /c
        /TC
        /GS-
        /GR-
        /O1
        /Os
        /Oi-
        /Gy-
        "/Fo${TIMWIN_HOOK_C_OBJ}"
        "/Fa${TIMWIN_HOOK_C_LISTING}"
        /FAcs
        "${TIMWIN_HOOK_C}"
    DEPENDS "${TIMWIN_HOOK_C}"
    COMMENT "Compiling TIMWIN hook C code"
    VERBATIM
)

add_custom_command(
    OUTPUT "${TIMWIN_TRAMPOLINES_OBJ}" "${TIMWIN_TRAMPOLINES_LISTING}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TIMWIN_HOOK_BUILD_DIR}"
    COMMAND "${TIMWIN_ML}"
        /nologo
        /c
        /coff
        "/Fo${TIMWIN_TRAMPOLINES_OBJ}"
        "/Fl${TIMWIN_TRAMPOLINES_LISTING}"
        "${TIMWIN_TRAMPOLINES_ASM}"
    DEPENDS "${TIMWIN_TRAMPOLINES_ASM}"
    COMMENT "Assembling TIMWIN hook trampolines"
    VERBATIM
)

add_custom_command(
    OUTPUT "${SOS9502_HOOKS_OBJ}" "${SOS9502_HOOKS_LISTING}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TIMWIN_HOOK_BUILD_DIR}"
    COMMAND "${TIMWIN_ML}"
        /nologo
        /c
        /coff
        "/Fo${SOS9502_HOOKS_OBJ}"
        "/Fl${SOS9502_HOOKS_LISTING}"
        "${SOS9502_HOOKS_ASM}"
    DEPENDS "${SOS9502_HOOKS_ASM}"
    COMMENT "Assembling SOS9502 hook caves"
    VERBATIM
)

add_custom_target(hook-code ALL
    DEPENDS
        "${TIMWIN_HOOK_C_OBJ}"
        "${TIMWIN_TRAMPOLINES_OBJ}"
        "${SOS9502_HOOKS_OBJ}"
)

set(TIMWIN_TOOLS_DIR "${CMAKE_BINARY_DIR}/tools")
set(TIMWIN_HOOKLINK_C "${CMAKE_SOURCE_DIR}/tools/hooklink.c")
set(TIMWIN_HOOKLINK_OBJ "${TIMWIN_TOOLS_DIR}/hooklink.obj")
set(TIMWIN_HOOKLINK_EXE "${TIMWIN_TOOLS_DIR}/hooklink.exe")

add_custom_command(
    OUTPUT "${TIMWIN_HOOKLINK_EXE}" "${TIMWIN_HOOKLINK_OBJ}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TIMWIN_TOOLS_DIR}"
    COMMAND "${TIMWIN_CL}"
        /nologo
        /c
        /TC
        /W3
        /O2
        /MT
        "/I${TIMWIN_MSVC_INCLUDE}"
        "/I${TIMWIN_UCRT_INCLUDE}"
        "/Fo${TIMWIN_HOOKLINK_OBJ}"
        "${TIMWIN_HOOKLINK_C}"
    COMMAND "${TIMWIN_LINK}"
        /nologo
        /MACHINE:X86
        /SUBSYSTEM:CONSOLE
        /MANIFEST:NO
        "/OUT:${TIMWIN_HOOKLINK_EXE}"
        "${TIMWIN_HOOKLINK_OBJ}"
        "/LIBPATH:${TIMWIN_MSVC_LIB}"
        "/LIBPATH:${TIMWIN_UCRT_LIB}"
        "/LIBPATH:${TIMWIN_UM_LIB}"
    DEPENDS "${TIMWIN_HOOKLINK_C}"
    COMMENT "Building native hook linker"
    VERBATIM
)

add_custom_target(hooklink
    DEPENDS "${TIMWIN_HOOKLINK_EXE}"
)

set(TIMWIN_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(TIMWIN_HOOK_BLOB_HEADER "${TIMWIN_GENERATED_DIR}/timwin_hook_blob.h")
set(TIMWIN_PATCH_C "${CMAKE_SOURCE_DIR}/src/timwin_patch.c")
set(TIMWIN_PATCH_MANIFEST "${CMAKE_SOURCE_DIR}/src/timwin_patch.exe.manifest")
set(TIMWIN_PATCH_OBJ "${CMAKE_BINARY_DIR}/timwin_patch.obj")
set(TIMWIN_PATCH_EXE "${CMAKE_BINARY_DIR}/timwin-patch.exe")

add_custom_command(
    OUTPUT "${TIMWIN_HOOK_BLOB_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TIMWIN_GENERATED_DIR}"
    COMMAND "${TIMWIN_HOOKLINK_EXE}"
        --header "${TIMWIN_HOOK_BLOB_HEADER}"
        --timwin-base-va 0x00500000
        --sos9502-base-va 0x00410F20
        --timwin-obj "${TIMWIN_HOOK_C_OBJ}"
        --timwin-obj "${TIMWIN_TRAMPOLINES_OBJ}"
        --sos9502-obj "${SOS9502_HOOKS_OBJ}"
    DEPENDS
        "${TIMWIN_HOOKLINK_EXE}"
        "${TIMWIN_HOOK_C_OBJ}"
        "${TIMWIN_TRAMPOLINES_OBJ}"
        "${SOS9502_HOOKS_OBJ}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Generating native patcher hook blob"
    VERBATIM
)

add_custom_command(
    OUTPUT "${TIMWIN_PATCH_EXE}" "${TIMWIN_PATCH_OBJ}"
    COMMAND "${TIMWIN_CL}"
        /nologo
        /c
        /TC
        /W3
        /O2
        /MT
        "/I${TIMWIN_GENERATED_DIR}"
        "/I${TIMWIN_MSVC_INCLUDE}"
        "/I${TIMWIN_UCRT_INCLUDE}"
        "/Fo${TIMWIN_PATCH_OBJ}"
        "${TIMWIN_PATCH_C}"
    COMMAND "${TIMWIN_LINK}"
        /nologo
        /MACHINE:X86
        /SUBSYSTEM:CONSOLE
        /MANIFEST:NO
        "/OUT:${TIMWIN_PATCH_EXE}"
        "${TIMWIN_PATCH_OBJ}"
        "/LIBPATH:${TIMWIN_MSVC_LIB}"
        "/LIBPATH:${TIMWIN_UCRT_LIB}"
        "/LIBPATH:${TIMWIN_UM_LIB}"
    COMMAND "${TIMWIN_MT}"
        -nologo
        -manifest "${TIMWIN_PATCH_MANIFEST}"
        "-outputresource:${TIMWIN_PATCH_EXE}$<SEMICOLON>#1"
    DEPENDS "${TIMWIN_PATCH_C}" "${TIMWIN_PATCH_MANIFEST}" "${TIMWIN_HOOK_BLOB_HEADER}"
    COMMENT "Building native TIMWIN patcher"
    VERBATIM
)

add_custom_target(native-patcher
    DEPENDS "${TIMWIN_PATCH_EXE}"
)

add_custom_target(verify
    COMMAND "${TIMWIN_PATCH_EXE}" --help
    DEPENDS native-patcher
    COMMENT "Checking native patcher starts"
    VERBATIM
)

add_custom_target(patch
    COMMAND "${TIMWIN_PATCH_EXE}"
    DEPENDS native-patcher
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Applying TIMWIN patch"
    VERBATIM
)
