cmake_minimum_required(VERSION 3.16)
project(HyperSubtitlesEditor LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
if(NOT EXISTS ${REPO_ROOT}/external/SDL/CMakeLists.txt OR NOT EXISTS ${REPO_ROOT}/imgui/imgui.cpp)
    message(FATAL_ERROR "Missing submodules. Run: git submodule update --init --recursive")
endif()

# SDL3 (static, from the submodule)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${REPO_ROOT}/external/SDL ${CMAKE_BINARY_DIR}/SDL EXCLUDE_FROM_ALL)

# Dear ImGui: the same submodule (same version) the DLL is built with
add_library(imgui STATIC
    ${REPO_ROOT}/imgui/imgui.cpp
    ${REPO_ROOT}/imgui/imgui_draw.cpp
    ${REPO_ROOT}/imgui/imgui_tables.cpp
    ${REPO_ROOT}/imgui/imgui_widgets.cpp
    ${REPO_ROOT}/imgui/imgui_demo.cpp
    ${REPO_ROOT}/imgui/misc/cpp/imgui_stdlib.cpp
    ${REPO_ROOT}/imgui/backends/imgui_impl_sdl3.cpp
    ${REPO_ROOT}/imgui/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${REPO_ROOT}/imgui ${REPO_ROOT}/imgui/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3-static)

# Code shared with the DLL + data model (no window, no GL): also used by the tests
add_library(editor_core STATIC
    ${REPO_ROOT}/shared/DialogueBoxCore.cpp
    src/Project.cpp
    src/Exporter.cpp)
target_include_directories(editor_core PUBLIC ${REPO_ROOT}/shared src)
target_link_libraries(editor_core PUBLIC imgui)

# Japanese font for the UI (ja transcriptions), embedded so the exe needs no extra files.
set(JP_FONT ${REPO_ROOT}/external/fonts/NotoSansJP-Regular.otf)
set(JP_FONT_CPP ${CMAKE_BINARY_DIR}/generated/noto_sans_jp.cpp)
add_custom_command(
    OUTPUT ${JP_FONT_CPP}
    COMMAND ${CMAKE_COMMAND} -DINPUT=${JP_FONT} -DOUTPUT=${JP_FONT_CPP} -DSYMBOL=g_notoSansJp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake
    DEPENDS ${JP_FONT} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake
    COMMENT "Embedding NotoSansJP-Regular.otf")

add_executable(HyperSubtitlesEditor WIN32
    src/main.cpp
    src/App.cpp
    src/Analysis.cpp
    src/Gl.cpp
    src/Preview.cpp
    src/Settings.cpp
    src/StbImpl.cpp
    src/XactAudio.cpp
    ${JP_FONT_CPP})
target_include_directories(HyperSubtitlesEditor PRIVATE ${REPO_ROOT}/external/stb)
target_link_libraries(HyperSubtitlesEditor PRIVATE editor_core imgui)
if(WIN32)
    target_link_libraries(HyperSubtitlesEditor PRIVATE opengl32)
elseif(APPLE)
    target_link_libraries(HyperSubtitlesEditor PRIVATE "-framework OpenGL")
else()
    target_link_libraries(HyperSubtitlesEditor PRIVATE ${CMAKE_DL_LIBS})
endif()

foreach(target imgui editor_core HyperSubtitlesEditor)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /W3)
    else()
        target_compile_options(${target} PRIVATE -Wall)
    endif()
endforeach()

# Tests: byte-identical save, export, text normalization, undo
enable_testing()
add_executable(editor_tests tests/editor_tests.cpp)
target_link_libraries(editor_tests PRIVATE editor_core)
if(MSVC)
    target_compile_options(editor_tests PRIVATE /utf-8)
endif()
add_test(NAME editor_tests
         COMMAND editor_tests ${REPO_ROOT}/data ${CMAKE_CURRENT_SOURCE_DIR}/tests/display_text_cases.json ${CMAKE_BINARY_DIR}/test_out)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    # The editor's export must match `lines.py export-subtitles` byte for byte.
    add_test(NAME export_matches_python
             COMMAND ${CMAKE_COMMAND}
                 -DPYTHON=${Python3_EXECUTABLE} -DREPO=${REPO_ROOT} -DOUT=${CMAKE_BINARY_DIR}/test_out
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_export.cmake)
    set_tests_properties(export_matches_python PROPERTIES DEPENDS editor_tests)
    add_test(NAME python_display_text
             COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/tools/lines/test_lines.py)
endif()
