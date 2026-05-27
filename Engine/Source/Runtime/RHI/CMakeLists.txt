# RHI is header-only in Stage 1 - INTERFACE library. Stage 2 may move to SHARED
# when enum-to-string helpers, type registries, or runtime debug metadata land.
add_engine_module(
    NAME        RHI
    TYPE        INTERFACE
    PUBLIC_DEPS Engine::Core
)
