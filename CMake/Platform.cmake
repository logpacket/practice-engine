# CMake/Platform.cmake
#
# Platform detection. Sets ENGINE_PLATFORM_DIR (Win64 | Linux | Mac) for use in
# output directories and per-platform backend directory selection.

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(ENGINE_PLATFORM_DIR "Win64" CACHE INTERNAL "")
    set(ENGINE_PLATFORM_WINDOWS TRUE CACHE INTERNAL "")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(ENGINE_PLATFORM_DIR "Linux" CACHE INTERNAL "")
    set(ENGINE_PLATFORM_LINUX TRUE CACHE INTERNAL "")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(ENGINE_PLATFORM_DIR "Mac" CACHE INTERNAL "")
    set(ENGINE_PLATFORM_MAC TRUE CACHE INTERNAL "")
else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

message(STATUS "Engine platform: ${ENGINE_PLATFORM_DIR}")
