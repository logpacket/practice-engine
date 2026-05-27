# CMake/CompilerWarnings.cmake
#
# set_engine_warnings(<target>) - strict warning set for engine code.
# Do not call on third-party targets (use -w or SYSTEM include for those).

function(set_engine_warnings tgt)
    if(MSVC)
        target_compile_options(${tgt} PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8)
        if(ENGINE_WARNINGS_AS_ERRORS)
            target_compile_options(${tgt} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${tgt} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Woverloaded-virtual
            -Wnull-dereference
            -Wdouble-promotion
            -Wformat=2
            -Wimplicit-fallthrough)
        if(ENGINE_WARNINGS_AS_ERRORS)
            target_compile_options(${tgt} PRIVATE -Werror)
        endif()
    endif()
endfunction()
