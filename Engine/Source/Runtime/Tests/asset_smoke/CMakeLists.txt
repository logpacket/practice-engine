add_engine_executable(
    NAME asset_smoke
    DEPS Engine::Core Engine::Asset
)

# Needs the built shader binaries on disk (loads one as the known file).
add_dependencies(asset_smoke Shaders)
