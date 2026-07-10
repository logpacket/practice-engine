# CMake/EngineModule.cmake
#
# add_engine_module() - single entry point for declaring every engine module.
#
# Usage:
#   add_engine_module(
#       NAME           <ModuleName>                # required
#       TYPE           SHARED|STATIC|INTERFACE     # default SHARED
#       PUBLIC_DEPS    <targets...>                # propagated to dependents
#       PRIVATE_DEPS   <targets...>                # link-only
#       PUBLIC_DEFS    <FOO=1 BAR>                 # propagated compile defs
#       PRIVATE_DEFS   <...>
#       BACKEND_DIR    <BackendSubdir>             # build Private/<dir>/ only (for PAL backends)
#   )
#
# Conventions:
#   - Public/<ModuleName>/*.h is exposed on dependents' include path
#   - Private/ is visible only to the module itself
#   - generate_export_header emits a <MODULE>_API macro
#   - Inside a module's .cpp files, MODULE_API can be used (injected as private compile def)

include(GenerateExportHeader)

function(add_engine_module)
    cmake_parse_arguments(ARG
        ""
        "NAME;TYPE;BACKEND_DIR;PCH_HEADER"
        "PUBLIC_DEPS;PRIVATE_DEPS;PUBLIC_DEFS;PRIVATE_DEFS"
        ${ARGN})

    if(NOT ARG_NAME)
        message(FATAL_ERROR "add_engine_module: NAME is required")
    endif()
    if(NOT ARG_TYPE)
        set(ARG_TYPE SHARED)
    endif()

    # --- Source collection -------------------------------------------------
    # Glob (no CONFIGURE_DEPENDS - avoids VS generator trigger misses; covered by convention)
    file(GLOB_RECURSE PUB_HDRS Public/*.h Public/*.hpp)
    if(ARG_BACKEND_DIR)
        file(GLOB_RECURSE PRV_SRCS
            Private/${ARG_BACKEND_DIR}/*.cpp
            Private/${ARG_BACKEND_DIR}/*.c
            Private/${ARG_BACKEND_DIR}/*.h)
    else()
        file(GLOB_RECURSE PRV_SRCS Private/*.cpp Private/*.c Private/*.h)
    endif()

    # --- Target creation ---------------------------------------------------
    if(ARG_TYPE STREQUAL "INTERFACE")
        add_library(${ARG_NAME} INTERFACE)
        add_library(Engine::${ARG_NAME} ALIAS ${ARG_NAME})
        target_include_directories(${ARG_NAME} INTERFACE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public>
            $<INSTALL_INTERFACE:include>)
        if(ARG_PUBLIC_DEPS)
            target_link_libraries(${ARG_NAME} INTERFACE ${ARG_PUBLIC_DEPS})
        endif()
        return()
    endif()

    add_library(${ARG_NAME} ${ARG_TYPE} ${PUB_HDRS} ${PRV_SRCS})
    add_library(Engine::${ARG_NAME} ALIAS ${ARG_NAME})

    target_include_directories(${ARG_NAME}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/Private)

    # --- Symbol visibility + export macro ---------------------------------
    string(TOUPPER ${ARG_NAME} NAME_UPPER)
    generate_export_header(${ARG_NAME}
        BASE_NAME         ${NAME_UPPER}
        EXPORT_MACRO_NAME ${NAME_UPPER}_API
        EXPORT_FILE_NAME  ${CMAKE_CURRENT_BINARY_DIR}/Generated/${ARG_NAME}/${ARG_NAME}API.h)
    target_include_directories(${ARG_NAME}
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/Generated>)
    target_compile_definitions(${ARG_NAME}
        PRIVATE
            MODULE_API=${NAME_UPPER}_API
            MODULE_NAME="${ARG_NAME}")
    if(ARG_TYPE STREQUAL "STATIC")
        target_compile_definitions(${ARG_NAME} PUBLIC ${NAME_UPPER}_STATIC_DEFINE)
    endif()

    set_target_properties(${ARG_NAME} PROPERTIES
        CXX_VISIBILITY_PRESET     hidden
        VISIBILITY_INLINES_HIDDEN ON
        POSITION_INDEPENDENT_CODE ON)

    # --- Dependencies -----------------------------------------------------
    if(ARG_PUBLIC_DEPS)
        target_link_libraries(${ARG_NAME} PUBLIC ${ARG_PUBLIC_DEPS})
    endif()
    if(ARG_PRIVATE_DEPS)
        target_link_libraries(${ARG_NAME} PRIVATE ${ARG_PRIVATE_DEPS})
    endif()
    if(ARG_PUBLIC_DEFS)
        target_compile_definitions(${ARG_NAME} PUBLIC ${ARG_PUBLIC_DEFS})
    endif()
    if(ARG_PRIVATE_DEFS)
        target_compile_definitions(${ARG_NAME} PRIVATE ${ARG_PRIVATE_DEFS})
    endif()

    # --- Warnings ---------------------------------------------------------
    set_engine_warnings(${ARG_NAME})

    # --- Precompiled header (framework pattern) ---------------------------
    # Each module precompiles its own PCH that pre-includes the most-used
    # headers (stdlib + commonly-reached engine deps). Cuts repeat compilation
    # cost across the module's .cpp files. PRIVATE so it does not propagate to
    # consumers - they apply their own PCH if desired.
    if(ARG_PCH_HEADER)
        target_precompile_headers(${ARG_NAME} PRIVATE ${ARG_PCH_HEADER})
    endif()

    # --- Build-type macros ------------------------------------------------
    target_compile_definitions(${ARG_NAME} PRIVATE
        $<$<CONFIG:Debug>:ENGINE_BUILD_DEBUG=1>
        $<$<CONFIG:Release>:ENGINE_BUILD_RELEASE=1>
        $<$<CONFIG:RelWithDebInfo>:ENGINE_BUILD_RELEASE=1>)

    # --- Install ----------------------------------------------------------
    install(TARGETS ${ARG_NAME}
        EXPORT  EngineTargets
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION bin   # .so colocated with .exe
        ARCHIVE DESTINATION lib)
    install(DIRECTORY Public/ DESTINATION include)
endfunction()


# add_engine_executable() - executable target (Stage 1: one)
function(add_engine_executable)
    cmake_parse_arguments(ARG "" "NAME" "DEPS" ${ARGN})

    file(GLOB_RECURSE SRCS Private/*.cpp Private/*.c Private/*.h)
    add_executable(${ARG_NAME} ${SRCS})

    target_include_directories(${ARG_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Private)

    if(ARG_DEPS)
        target_link_libraries(${ARG_NAME} PRIVATE ${ARG_DEPS})
    endif()

    set_target_properties(${ARG_NAME} PROPERTIES
        POSITION_INDEPENDENT_CODE ON)

    # Same build-type macros as add_engine_module - executables (tests) that
    # instantiate engine templates rely on ENGINE_CHECK being active in Debug.
    target_compile_definitions(${ARG_NAME} PRIVATE
        $<$<CONFIG:Debug>:ENGINE_BUILD_DEBUG=1>
        $<$<CONFIG:Release>:ENGINE_BUILD_RELEASE=1>
        $<$<CONFIG:RelWithDebInfo>:ENGINE_BUILD_RELEASE=1>)

    # Linux: resolve .so from the executable directory via RPATH=$ORIGIN
    if(UNIX AND NOT APPLE)
        set_target_properties(${ARG_NAME} PROPERTIES
            BUILD_RPATH              "\$ORIGIN"
            INSTALL_RPATH            "\$ORIGIN"
            BUILD_WITH_INSTALL_RPATH FALSE)
    elseif(APPLE)
        set_target_properties(${ARG_NAME} PROPERTIES
            BUILD_RPATH              "@executable_path"
            INSTALL_RPATH            "@executable_path"
            BUILD_WITH_INSTALL_RPATH FALSE)
    endif()

    set_engine_warnings(${ARG_NAME})

    install(TARGETS ${ARG_NAME} RUNTIME DESTINATION bin)
endfunction()
