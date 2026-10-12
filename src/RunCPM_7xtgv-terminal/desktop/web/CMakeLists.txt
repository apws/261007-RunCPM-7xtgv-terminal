cmake_minimum_required(VERSION 3.19)
project(xtgv_web LANGUAGES CXX)
if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "Use tools-web/desktop-rebuild.sh with Emscripten")
endif()
set(CMAKE_CXX_STANDARD 17)
set(XTGV_GFX_SOURCE "" CACHE PATH "Pinned Arduino_GFX 1.6.4 root")
file(STRINGS "${XTGV_GFX_SOURCE}/library.properties" gfx_version REGEX "^version=")
if(NOT gfx_version STREQUAL "version=1.6.4")
    message(FATAL_ERROR "Arduino_GFX 1.6.4 is required")
endif()
string(TIMESTAMP xtgv_build_stamp "%y%m%d-%H%M")
# One runtime, with startup-selected descriptors compiled from board headers.
add_compile_definitions(ARDUINO=10819 XTGV_DESKTOP=1 XTGV_WEB=1
    XTGV_RUNCPM=1 XTGV_TERMINAL=1 XTGV_CCP_7XF=1
    HostOS=0x02 XTGV_BUILD_STAMP=${xtgv_build_stamp})
add_compile_options(-O2 -pthread -fexceptions)
add_executable(xtgv_web application.cpp serial.cpp board_profiles.cpp
    ../platform.cpp ../storage.cpp
    ../../src/terminal/terminal.cpp ../../src/terminal/terminal_runtime.cpp
    ../../src/terminal/history_profiles.cpp
    ${XTGV_GFX_SOURCE}/src/Arduino_G.cpp
    ${XTGV_GFX_SOURCE}/src/Arduino_GFX.cpp
    ${XTGV_GFX_SOURCE}/src/canvas/Arduino_Canvas.cpp)
target_include_directories(xtgv_web PRIVATE ../shims .. ${XTGV_GFX_SOURCE}/src)
target_link_options(xtgv_web PRIVATE -O2 -pthread -fexceptions
    "-sMODULARIZE=1" "-sEXPORT_NAME=createXtgv" "-sPTHREAD_POOL_SIZE=1"
    "-sINITIAL_MEMORY=134217728" "-sALLOW_MEMORY_GROWTH=1"
    "-sSTACK_SIZE=2097152" "-sDEFAULT_PTHREAD_STACK_SIZE=2097152"
    "-sFORCE_FILESYSTEM=1" "-sEXIT_RUNTIME=0" "-sENVIRONMENT=web,worker"
    "-sEXPORTED_RUNTIME_METHODS=['FS','callMain','ccall','HEAPU8','PThread']"
    "-sEXPORTED_FUNCTIONS=['_main','_web_receive','_web_receive_event','_web_connect','_web_stop','_web_snapshot','_web_profiles','_web_select_profile']")
set_target_properties(xtgv_web PROPERTIES SUFFIX ".js")
