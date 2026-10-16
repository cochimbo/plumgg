cmake_minimum_required(VERSION 3.16)
project(plumgg LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(PLUMGG_FETCH_SDL2 "Download and build SDL2 if it is not installed" ON)
option(PLUMGG_BUILD_TESTS "Build the test programs" ON)

if(MSVC)
  add_compile_options(/W4 /utf-8)
else()
  add_compile_options(-Wall -Wextra)
endif()

# Emulator core: no SDL, no OS dependencies.
add_library(plumgg_core STATIC
  src/core/z80.cpp
  src/core/vdp.cpp
  src/core/cartridge.cpp
  src/core/psg.cpp
  src/core/gamegear.cpp
)
target_include_directories(plumgg_core PUBLIC src)

# miniz (vendored, MIT) to read ROMs packed in .zip files.
add_library(plumgg_miniz STATIC third_party/miniz/miniz.c)
set_target_properties(plumgg_miniz PROPERTIES LINKER_LANGUAGE C)
target_include_directories(plumgg_miniz PUBLIC third_party/miniz)
target_compile_definitions(plumgg_miniz PUBLIC MINIZ_NO_STDIO MINIZ_NO_TIME)
if(MSVC)
  target_compile_options(plumgg_miniz PRIVATE /W0)
else()
  target_compile_options(plumgg_miniz PRIVATE -w)
endif()
target_link_libraries(plumgg_core PRIVATE plumgg_miniz)

# SDL2: use an installed copy (vcpkg, system package, SDL2_DIR) or fetch it.
find_package(SDL2 CONFIG QUIET)
if(NOT SDL2_FOUND AND PLUMGG_FETCH_SDL2)
  include(FetchContent)
  message(STATUS "SDL2 not found, fetching it")
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.9
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(SDL2)
endif()

if(TARGET SDL2::SDL2)
  set(PLUMGG_SDL2_TARGET SDL2::SDL2)
else()
  set(PLUMGG_SDL2_TARGET SDL2::SDL2-static)
endif()

# Dear ImGui for the menus and option windows, drawn through SDL_Renderer.
include(FetchContent)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.9b
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)
add_library(plumgg_imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)
target_include_directories(plumgg_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(plumgg_imgui PUBLIC ${PLUMGG_SDL2_TARGET})

add_executable(plumgg WIN32
  src/frontend/main.cpp
  src/frontend/input.cpp
  src/frontend/audio.cpp
  src/frontend/settings.cpp
  src/frontend/file_dialog.cpp
  src/frontend/ui.cpp
  src/render/sdl_renderers.cpp
)
target_link_libraries(plumgg PRIVATE plumgg_core plumgg_imgui)
if(TARGET SDL2::SDL2main)
  target_link_libraries(plumgg PRIVATE SDL2::SDL2main)
endif()
target_link_libraries(plumgg PRIVATE ${PLUMGG_SDL2_TARGET})
if(WIN32)
  target_link_libraries(plumgg PRIVATE comdlg32)
endif()

# Copy SDL2.dll next to plumgg.exe so it runs straight from the build folder.
if(WIN32 AND TARGET SDL2::SDL2)
  add_custom_command(TARGET plumgg POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:plumgg>)
endif()

if(PLUMGG_BUILD_TESTS)
  enable_testing()
  add_executable(core_tests tests/core_tests.cpp)
  target_link_libraries(core_tests PRIVATE plumgg_core plumgg_miniz)
  add_test(NAME core_tests COMMAND core_tests)

  # Runs the ZEXDOC/ZEXALL Z80 exercisers: zex_runner path/to/zexdoc.com
  add_executable(zex_runner tests/zex_runner.cpp)
  target_link_libraries(zex_runner PRIVATE plumgg_core)
endif()
