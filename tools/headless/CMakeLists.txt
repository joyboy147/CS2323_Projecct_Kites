# Headless (Qt-free) build of the Kites core + gtest suite + CLI tools. See README.md.
# A separate CMake project: the main build only globs src/, so it never sees this directory.
cmake_minimum_required(VERSION 3.19)
project(KitesHeadless LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

get_filename_component(KITES_REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(GTEST_SRC "" CACHE PATH "Local googletest source tree (empty: download v1.14.0)")
set(QTSTUB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/qtstub")
set(SRC "${KITES_REPO}/src")

# ---- Lua (vendored) ---------------------------------------------------------
file(GLOB LUA_SRC CONFIGURE_DEPENDS "${KITES_REPO}/external/lua-5.5.0/src/*.c")
list(FILTER LUA_SRC EXCLUDE REGEX ".*/lua[.]c$")
list(FILTER LUA_SRC EXCLUDE REGEX ".*/luac[.]c$")
add_library(kites_lua STATIC ${LUA_SRC})
target_include_directories(kites_lua PUBLIC "${KITES_REPO}/external/lua-5.5.0/src")
target_compile_options(kites_lua PRIVATE -w)
target_link_libraries(kites_lua PUBLIC m)

# ---- Core (non-UI) ------------------------------------------------------------
file(GLOB_RECURSE CORE_SRC CONFIGURE_DEPENDS
    "${SRC}/assembler/*.cpp"
    "${SRC}/common/*.cpp"
    "${SRC}/processor/*.cpp"
    "${SRC}/utils/*.cpp"
    "${SRC}/scheduler/*.cpp"
    "${SRC}/elf_util/*.cpp"
    "${SRC}/profiler/*.cpp"
)
list(APPEND CORE_SRC "${SRC}/config/config.cpp")
# Needs real Qt / the GUI (ProcessorManager drives the UI, owns CircuitScene, etc.).
list(FILTER CORE_SRC EXCLUDE REGEX ".*/processor/processor_manager[.]cpp$")

add_library(kites_core_headless STATIC ${CORE_SRC} "${CMAKE_CURRENT_SOURCE_DIR}/qt_signal_stubs.cpp")
# qtstub MUST precede src so "ui/processor_tab/..." resolves to the stubs.
target_include_directories(kites_core_headless PUBLIC "${QTSTUB_DIR}" "${SRC}")
target_compile_definitions(kites_core_headless PUBLIC DISABLE_GUI)
target_compile_options(kites_core_headless PRIVATE
    -Wall -Wextra -pedantic -frounding-math -ffloat-store)
find_package(Threads REQUIRED)
target_link_libraries(kites_core_headless PUBLIC kites_lua Threads::Threads)

# ---- googletest ---------------------------------------------------------------
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
if(GTEST_SRC)
    add_subdirectory("${GTEST_SRC}" "${CMAKE_BINARY_DIR}/googletest" EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip)
    FetchContent_MakeAvailable(googletest)
endif()

# ---- Tests --------------------------------------------------------------------
file(GLOB TEST_SRC CONFIGURE_DEPENDS "${KITES_REPO}/test/*.cpp")
# test_main.cpp: unconditionally #includes <windows.h> (+QLoggingCategory) -> gtest_main used instead.
# pseudo_inst_test.cpp: needs real Qt (QFile/QTemporaryDir/QJson via CustomPseudoManager).
list(FILTER TEST_SRC EXCLUDE REGEX ".*/test_main[.]cpp$")
list(FILTER TEST_SRC EXCLUDE REGEX ".*/pseudo_inst_test[.]cpp$")

add_executable(kites_headless_test ${TEST_SRC})
target_link_libraries(kites_headless_test PRIVATE kites_core_headless GTest::gtest GTest::gmock_main)

enable_testing()
include(GoogleTest)
gtest_discover_tests(kites_headless_test
    WORKING_DIRECTORY "${KITES_REPO}/test"
    DISCOVERY_MODE PRE_TEST)

# ---- kites_cycles CLI -------------------------------------------------------
add_executable(kites_cycles "${CMAKE_CURRENT_SOURCE_DIR}/kites_cycles.cpp")
target_link_libraries(kites_cycles PRIVATE kites_core_headless)

# ---- kites_suggest CLI (headless driver of the Suggestions flow) ------------
add_executable(kites_suggest "${CMAKE_CURRENT_SOURCE_DIR}/kites_suggest.cpp")
target_link_libraries(kites_suggest PRIVATE kites_core_headless)

# ---- kites_fuzz (review: differential fuzzer) --------------------------------
add_executable(kites_fuzz "${CMAKE_CURRENT_SOURCE_DIR}/kites_fuzz.cpp")
target_link_libraries(kites_fuzz PRIVATE kites_core_headless)

# ---- kites_profile CLI (stall profiler) ---------------------------------------
add_executable(kites_profile "${CMAKE_CURRENT_SOURCE_DIR}/kites_profile.cpp")
target_link_libraries(kites_profile PRIVATE kites_core_headless)
