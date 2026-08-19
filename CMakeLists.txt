cmake_minimum_required(VERSION 3.24)

project(CoreDesk
    VERSION 1.0.0
    DESCRIPTION "Cross-platform desktop file indexing and LAN transfer client"
    LANGUAGES CXX
)

option(COREDESK_BUILD_UI "Build the Qt Widgets desktop application" ON)
option(COREDESK_BUILD_TESTS "Build CoreDesk tests" ON)
option(COREDESK_BUILD_NETWORK "Build Qt Network transfer adapters" ON)
option(COREDESK_ENABLE_ASAN "Enable AddressSanitizer for supported compilers" OFF)
option(COREDESK_ENABLE_TSAN "Enable ThreadSanitizer for supported compilers" OFF)
option(COREDESK_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(COREDESK_FETCH_DEPS "Fetch approved dependencies when not installed" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)

add_library(coredesk_common
    src/common/Cancellation.cpp
    src/common/Error.cpp
)
add_library(coredesk::common ALIAS coredesk_common)
target_include_directories(coredesk_common
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(coredesk_common PUBLIC cxx_std_20)
coredesk_set_target_warnings(coredesk_common)
coredesk_enable_sanitizers(coredesk_common)

add_library(coredesk_concurrency
    src/concurrency/ThreadPool.cpp
)
target_link_libraries(coredesk_concurrency PUBLIC coredesk_common)
target_include_directories(coredesk_concurrency
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(coredesk_concurrency PUBLIC cxx_std_20)
coredesk_set_target_warnings(coredesk_concurrency)
coredesk_enable_sanitizers(coredesk_concurrency)

add_library(coredesk_filesystem
    src/filesystem/FileScanner.cpp
)
target_link_libraries(coredesk_filesystem PUBLIC coredesk_common coredesk_concurrency)
target_include_directories(coredesk_filesystem
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(coredesk_filesystem PUBLIC cxx_std_20)
coredesk_set_target_warnings(coredesk_filesystem)
coredesk_enable_sanitizers(coredesk_filesystem)

add_library(coredesk_index
    src/index/IndexBuilder.cpp
    src/index/SearchEngine.cpp
    src/index/Tokenizer.cpp
)
target_link_libraries(coredesk_index PUBLIC coredesk_common coredesk_filesystem)
target_include_directories(coredesk_index
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(coredesk_index PUBLIC cxx_std_20)
coredesk_set_target_warnings(coredesk_index)
coredesk_enable_sanitizers(coredesk_index)

add_library(coredesk_protocol INTERFACE)
target_link_libraries(coredesk_protocol INTERFACE coredesk_common)
target_compile_features(coredesk_protocol INTERFACE cxx_std_20)

add_library(coredesk_service_lib INTERFACE)
target_link_libraries(coredesk_service_lib
    INTERFACE
        coredesk_common
        coredesk_concurrency
        coredesk_filesystem
        coredesk_index
        coredesk_protocol
)
target_compile_features(coredesk_service_lib INTERFACE cxx_std_20)

find_package(nlohmann_json CONFIG QUIET)
if(nlohmann_json_FOUND)
    target_link_libraries(coredesk_protocol INTERFACE nlohmann_json::nlohmann_json)
elseif(COREDESK_FETCH_DEPS)
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    )
    FetchContent_MakeAvailable(nlohmann_json)
    target_link_libraries(coredesk_protocol INTERFACE nlohmann_json::nlohmann_json)
endif()

add_executable(coredesk_cli
    apps/cli/main.cpp
)
target_link_libraries(coredesk_cli PRIVATE coredesk_common)
target_link_libraries(coredesk_cli PRIVATE coredesk_filesystem)
target_link_libraries(coredesk_cli PRIVATE coredesk_index)
coredesk_set_target_warnings(coredesk_cli)
coredesk_enable_sanitizers(coredesk_cli)

add_executable(coredesk_service
    apps/service/main.cpp
)
target_link_libraries(coredesk_service PRIVATE coredesk_service_lib)
coredesk_set_target_warnings(coredesk_service)
coredesk_enable_sanitizers(coredesk_service)

if(COREDESK_BUILD_UI OR COREDESK_BUILD_NETWORK)
    find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
endif()

if(COREDESK_BUILD_UI)
    add_library(coredesk_qt_ipc INTERFACE)
    target_link_libraries(coredesk_qt_ipc INTERFACE coredesk_protocol Qt6::Core)
    target_compile_features(coredesk_qt_ipc INTERFACE cxx_std_20)

    add_executable(coredesk_desktop
        apps/desktop/main.cpp
    )
    target_link_libraries(coredesk_desktop PRIVATE coredesk_qt_ipc Qt6::Widgets)
    coredesk_set_target_warnings(coredesk_desktop)
    coredesk_enable_sanitizers(coredesk_desktop)
else()
    add_library(coredesk_qt_ipc INTERFACE)
    target_link_libraries(coredesk_qt_ipc INTERFACE coredesk_protocol)
endif()

if(COREDESK_BUILD_NETWORK)
    add_library(coredesk_qt_network INTERFACE)
    target_link_libraries(coredesk_qt_network INTERFACE coredesk_protocol Qt6::Core Qt6::Network)
    target_compile_features(coredesk_qt_network INTERFACE cxx_std_20)
else()
    add_library(coredesk_qt_network INTERFACE)
    target_link_libraries(coredesk_qt_network INTERFACE coredesk_protocol)
endif()

add_executable(coredesk_bench_search
    benchmarks/bench_search.cpp
)
target_link_libraries(coredesk_bench_search PRIVATE coredesk_common)
target_link_libraries(coredesk_bench_search PRIVATE coredesk_index)
coredesk_set_target_warnings(coredesk_bench_search)
coredesk_enable_sanitizers(coredesk_bench_search)

if(COREDESK_BUILD_TESTS)
    include(CTest)
    enable_testing()

    find_package(GTest CONFIG QUIET)
    if(NOT GTest_FOUND AND COREDESK_FETCH_DEPS)
        include(FetchContent)
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    add_executable(coredesk_tests
        tests/unit/test_index_builder.cpp
        tests/unit/test_file_scanner.cpp
        tests/unit/test_lru_cache.cpp
        tests/unit/test_result.cpp
        tests/unit/test_search_engine.cpp
        tests/unit/test_thread_pool.cpp
        tests/unit/test_tokenizer.cpp
    )
    target_link_libraries(coredesk_tests PRIVATE coredesk_common coredesk_filesystem coredesk_index GTest::gtest_main)
    target_compile_features(coredesk_tests PRIVATE cxx_std_20)
    coredesk_set_target_warnings(coredesk_tests)
    coredesk_enable_sanitizers(coredesk_tests)

    include(GoogleTest)
    gtest_discover_tests(coredesk_tests)
endif()
