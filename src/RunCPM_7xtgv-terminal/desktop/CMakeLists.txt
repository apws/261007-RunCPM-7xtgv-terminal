cmake_minimum_required(VERSION 3.19)
project(xtgv_desktop LANGUAGES CXX)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Integrated desktop transport is currently Linux-only")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(XTGV_OPTIMIZATION "O2" CACHE STRING "Source optimization: default=O2/O0/Og/O1/O2/O3/Os")
set_property(CACHE XTGV_OPTIMIZATION PROPERTY STRINGS default O0 Og O1 O2 O3 Os)
if(NOT XTGV_OPTIMIZATION MATCHES "^(default|O0|Og|O1|O2|O3|Os)$")
    message(FATAL_ERROR "Unsupported XTGV_OPTIMIZATION")
endif()
if(XTGV_OPTIMIZATION STREQUAL "default")
    add_compile_options(-O2)
else()
    add_compile_options("-${XTGV_OPTIMIZATION}")
endif()

# Use the real pinned library, never the test framebuffer replacement.
set(XTGV_GFX_SOURCE "" CACHE PATH "Arduino_GFX 1.6.4 library root (contains library.properties)")
if(NOT EXISTS "${XTGV_GFX_SOURCE}/library.properties")
    message(FATAL_ERROR "Set XTGV_GFX_SOURCE to the Arduino_GFX 1.6.4 library root")
endif()
file(STRINGS "${XTGV_GFX_SOURCE}/library.properties" gfx_version REGEX "^version=")
if(NOT gfx_version STREQUAL "version=1.6.4")
    message(FATAL_ERROR "Expected Arduino_GFX 1.6.4, found ${gfx_version}")
endif()
find_package(Threads REQUIRED)
set(XTGV_TARGET "jc8012p4a1c" CACHE STRING "Existing hardware target whose display is emulated")
if(NOT XTGV_TARGET MATCHES "^[a-z0-9-]+$")
    message(FATAL_ERROR "Invalid XTGV_TARGET")
endif()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../tools/targets/${XTGV_TARGET}.json" target_json)
string(JSON target_flags GET "${target_json}" build_properties build.extra_flags)
string(REGEX MATCH "-D(RUNCPM_TARGET_[A-Z0-9_]+)" target_match "${target_flags}")
if(NOT target_match)
    message(FATAL_ERROR "Missing board selection in target metadata")
endif()
set(target_define "${CMAKE_MATCH_1}")
string(TIMESTAMP xtgv_build_stamp "%y%m%d-%H%M")
add_compile_definitions(ARDUINO=10819 XTGV_DESKTOP=1 ${target_define}
    XTGV_RUNCPM=1 XTGV_TERMINAL=1 XTGV_CCP_7XF=1 HostOS=0x02
    XTGV_BUILD_STAMP=${xtgv_build_stamp})
add_library(xtgv_desktop_gfx STATIC
    platform.cpp serial_pty.cpp
    ${XTGV_GFX_SOURCE}/src/Arduino_G.cpp
    ${XTGV_GFX_SOURCE}/src/Arduino_GFX.cpp
    ${XTGV_GFX_SOURCE}/src/canvas/Arduino_Canvas.cpp)
target_include_directories(xtgv_desktop_gfx PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/shims
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${XTGV_GFX_SOURCE}/src)
target_link_libraries(xtgv_desktop_gfx PUBLIC Threads::Threads xtgv_host_storage)

enable_testing()
add_executable(xtgv_canvas_test tests/canvas_test.cpp)
target_link_libraries(xtgv_canvas_test PRIVATE xtgv_desktop_gfx)
add_test(NAME real_gfx_canvas COMMAND xtgv_canvas_test)
add_executable(xtgv_presentation_test tests/presentation_test.cpp)
target_link_libraries(xtgv_presentation_test PRIVATE xtgv_desktop_gfx)
add_test(NAME presentation_policy COMMAND xtgv_presentation_test)

add_executable(xtgv_terminal_framebuffer_test
    tests/terminal_framebuffer_test.cpp
    ../src/terminal/terminal.cpp
    ../src/terminal/history_profiles.cpp)
target_link_libraries(xtgv_terminal_framebuffer_test PRIVATE xtgv_desktop_gfx xtgv_host_storage)
add_test(NAME production_terminal_framebuffer COMMAND xtgv_terminal_framebuffer_test)

add_library(xtgv_host_storage STATIC storage.cpp)
target_include_directories(xtgv_host_storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(xtgv_storage_test tests/storage_test.cpp)
target_link_libraries(xtgv_storage_test PRIVATE xtgv_host_storage)
add_test(NAME host_storage_contract COMMAND xtgv_storage_test)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    string(TIMESTAMP xtgv_build_stamp "%y%m%d-%H%M")
    # Windows/macOS graphics/storage proofs are portable; integrated host
    # identity and process lifecycle still need their own validation.
    add_executable(xtgv_headless application.cpp ../src/terminal/terminal.cpp
        ../src/terminal/terminal_runtime.cpp ../src/terminal/history_profiles.cpp)
    target_link_libraries(xtgv_headless PRIVATE xtgv_desktop_gfx xtgv_host_storage)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME canonical_card_names COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_card_names.py)
    add_test(NAME integrated_sketch_smoke COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/headless_smoke.py $<TARGET_FILE:xtgv_headless>)
    set_tests_properties(integrated_sketch_smoke PROPERTIES TIMEOUT 15)
    add_test(NAME pty_lifecycle COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/pty_lifecycle.py $<TARGET_FILE:xtgv_headless>)
    set_tests_properties(pty_lifecycle PROPERTIES TIMEOUT 25)
    option(XTGV_WITH_SDL "Build output-only SDL presenter" ON)
    if(XTGV_WITH_SDL)
        find_package(SDL2 REQUIRED)
        add_executable(xtgv_desktop application.cpp presenter.cpp ../src/terminal/terminal.cpp
            ../src/terminal/terminal_runtime.cpp ../src/terminal/history_profiles.cpp)
        target_compile_definitions(xtgv_desktop PRIVATE XTGV_DESKTOP_SDL=1)
        target_link_libraries(xtgv_desktop PRIVATE xtgv_desktop_gfx xtgv_host_storage SDL2::SDL2)
        add_test(NAME sdl_sketch_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/headless_smoke.py $<TARGET_FILE:xtgv_desktop>)
        set_tests_properties(sdl_sketch_smoke PROPERTIES TIMEOUT 15 ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    endif()
endif()
