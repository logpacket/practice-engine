add_engine_module(
    NAME        Asset
    TYPE        SHARED
    PUBLIC_DEPS Engine::Core
    PCH_HEADER  Private/AssetPCH.h
)
