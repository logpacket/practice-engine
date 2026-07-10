add_engine_module(
    NAME        RenderGraph
    TYPE        SHARED
    PUBLIC_DEPS Engine::Core Engine::RHI
    PCH_HEADER  Private/RenderGraphPCH.h
)
