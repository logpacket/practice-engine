add_engine_module(
    NAME        Core
    TYPE        SHARED
    PUBLIC_DEPS spdlog::spdlog glm::glm
    PCH_HEADER  Public/Core/CorePCH.h
)

# Linux: dl (for dlopen), pthread (for std::mutex on some toolchains).
if(UNIX AND NOT APPLE)
    target_link_libraries(Core PRIVATE dl pthread)
endif()
