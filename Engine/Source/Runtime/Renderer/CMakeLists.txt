add_engine_module(
    NAME        Renderer
    TYPE        SHARED
    PUBLIC_DEPS Engine::Core Engine::RHI Engine::RenderGraph
    PCH_HEADER  Private/RendererPCH.h
)

# Renderer consumes IWindow via its public header but MUST NOT link
# ApplicationCore at link time. Headers-only include preserves the dlopen
# architecture: ApplicationCore is loaded by Launch and accessed through vtable.
target_include_directories(Renderer PRIVATE
    ${CMAKE_SOURCE_DIR}/Engine/Source/Runtime/ApplicationCore/Public)

# --- Shader compilation: GLSL -> SPIR-V via glslc -------------------------
# Output goes next to the executable so FPaths::ShadersDir() resolves at runtime.
# build-time custom command (not configure-time), so editing a .glsl just
# re-runs glslc - no CMake reconfigure needed (Architecture.md §3.7).
set(SHADER_SRC_DIR "${CMAKE_SOURCE_DIR}/Engine/Shaders/Private")
set(SHADER_OUT_DIR "${ENGINE_BINARIES_ROOT}/$<CONFIG>/Shaders")

set(SHADER_SOURCES
    Triangle.vert.glsl
    Triangle.frag.glsl)

set(SHADER_BINARIES "")
foreach(SHADER_SRC IN LISTS SHADER_SOURCES)
    get_filename_component(SHADER_NAME ${SHADER_SRC} NAME_WE)  # "Triangle"
    get_filename_component(SHADER_EXT  ${SHADER_SRC} EXT)      # ".vert.glsl"
    string(REPLACE ".glsl" ".spv" SHADER_OUT_EXT ${SHADER_EXT})
    set(SHADER_OUT "${SHADER_OUT_DIR}/${SHADER_NAME}${SHADER_OUT_EXT}")

    # Extract stage from filename: "Triangle.vert.glsl" -> "vert".
    # glslc does not infer stage from a .glsl suffix; it requires -fshader-stage=.
    if(SHADER_EXT MATCHES "\\.(vert|frag|tesc|tese|geom|comp)\\.glsl$")
        set(SHADER_STAGE ${CMAKE_MATCH_1})
    else()
        message(FATAL_ERROR "Shader '${SHADER_SRC}' must be named *.{vert|frag|...}.glsl")
    endif()

    add_custom_command(
        OUTPUT  ${SHADER_OUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUT_DIR}
        COMMAND Vulkan::glslc -O -fshader-stage=${SHADER_STAGE}
                ${SHADER_SRC_DIR}/${SHADER_SRC} -o ${SHADER_OUT}
        DEPENDS ${SHADER_SRC_DIR}/${SHADER_SRC}
        COMMENT "Compiling shader ${SHADER_SRC} -> ${SHADER_NAME}${SHADER_OUT_EXT}"
        VERBATIM)
    list(APPEND SHADER_BINARIES ${SHADER_OUT})
endforeach()

add_custom_target(Shaders ALL DEPENDS ${SHADER_BINARIES})
add_dependencies(Renderer Shaders)
