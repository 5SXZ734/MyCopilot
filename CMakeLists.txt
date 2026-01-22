cmake_minimum_required(VERSION 3.21)
project(OverlayText LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

# ---- whisper.cpp options (disable junk you don't need)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)

# Put everything under build/bin/<CONFIG> and build/lib/<CONFIG>
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER ${cfg} CFG)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/bin/${cfg})
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/bin/${cfg})
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/lib/${cfg})
endforeach()


# ggml options (optional tuning)
set(GGML_OPENMP ON CACHE BOOL "" FORCE)

# ---- add whisper.cpp
add_subdirectory(whisper.cpp)

qt_add_executable(OverlayText
    main.cpp
    speech2text.cpp
    SpeechWorker.cpp 
    SpeechWorker.h
)

target_link_libraries(OverlayText
    PRIVATE
        Qt6::Widgets
        whisper
)

# whisper headers
target_include_directories(OverlayText
    PRIVATE
        whisper.cpp/include
)

target_compile_definitions(OverlayText
    PRIVATE NOMINMAX
)

# Optional: on Windows, avoid console window for GUI apps
if (WIN32)
    set_target_properties(OverlayText PROPERTIES WIN32_EXECUTABLE TRUE)
endif()


if (WIN32)
    foreach(lib whisper ggml ggml-cpu ggml-base)
        add_custom_command(TARGET OverlayText POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${lib}>
                $<TARGET_FILE_DIR:OverlayText>
        )
    endforeach()


    add_custom_command(TARGET OverlayText POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:OverlayText>/models
    )

    add_custom_command(TARGET OverlayText POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/whisper.cpp/models/ggml-base.en.bin
            $<TARGET_FILE_DIR:OverlayText>/models/ggml-base.en.bin
    )

endif()
