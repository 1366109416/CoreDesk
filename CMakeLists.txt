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

find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND AND COREDESK_FETCH_DEPS)
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

add_library(coredesk_protocol
    src/protocol/FrameCodec.cpp
    src/protocol/JsonPayload.cpp
)
target_link_libraries(coredesk_protocol PUBLIC coredesk_common)
target_link_libraries(coredesk_protocol PRIVATE nlohmann_json::nlohmann_json)
target_include_directories(coredesk_protocol
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(coredesk_protocol PUBLIC cxx_std_20)
coredesk_set_target_warnings(coredesk_protocol)
coredesk_enable_sanitizers(coredesk_protocol)

add_library(coredesk_service_lib
    src/service/ServiceController.cpp
)
target_link_libraries(coredesk_service_lib
    PUBLIC
        coredesk_common
        coredesk_concurrency
        coredesk_filesystem
        coredesk_index
        coredesk_protocol
)
target_include_directories(coredesk_service_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(coredesk_service_lib PUBLIC cxx_std_20)
coredesk_set_target_warnings(coredesk_service_lib)
coredesk_enable_sanitizers(coredesk_service_lib)

add_executable(coredesk_cli
    apps/cli/main.cpp
)
target_link_libraries(coredesk_cli PRIVATE coredesk_common)
target_link_libraries(coredesk_cli PRIVATE coredesk_filesystem)
target_link_libraries(coredesk_cli PRIVATE coredesk_index)
coredesk_set_target_warnings(coredesk_cli)
coredesk_enable_sanitizers(coredesk_cli)

find_package(Qt6 REQUIRED COMPONENTS Core Network)
if(COREDESK_BUILD_UI)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
endif()

add_library(coredesk_qt_ipc
    adapters/qt_ipc/LocalIpcClient.cpp
)
target_include_directories(coredesk_qt_ipc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/adapters/qt_ipc>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(coredesk_qt_ipc PUBLIC coredesk_protocol Qt6::Core Qt6::Network)
target_compile_features(coredesk_qt_ipc PUBLIC cxx_std_20)
coredesk_set_target_warnings(coredesk_qt_ipc)
coredesk_enable_sanitizers(coredesk_qt_ipc)

add_library(coredesk_qt_ipc_server
    adapters/qt_ipc/LocalIpcServer.cpp
)
target_include_directories(coredesk_qt_ipc_server
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/adapters/qt_ipc>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(coredesk_qt_ipc_server PUBLIC coredesk_protocol coredesk_service_lib Qt6::Core Qt6::Network)
target_compile_features(coredesk_qt_ipc_server PUBLIC cxx_std_20)
coredesk_set_target_warnings(coredesk_qt_ipc_server)
coredesk_enable_sanitizers(coredesk_qt_ipc_server)

add_executable(coredesk_service
    apps/service/main.cpp
)
target_link_libraries(coredesk_service PRIVATE coredesk_service_lib coredesk_qt_ipc_server Qt6::Core)
coredesk_set_target_warnings(coredesk_service)
coredesk_enable_sanitizers(coredesk_service)

if(COREDESK_BUILD_UI)
    add_executable(coredesk_desktop
        apps/desktop/main.cpp
        ui/MainWindow.cpp
        ui/SearchWidget.cpp
    )
    target_include_directories(coredesk_desktop PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui)
    target_link_libraries(coredesk_desktop PRIVATE coredesk_qt_ipc Qt6::Widgets)
    coredesk_set_target_warnings(coredesk_desktop)
    coredesk_enable_sanitizers(coredesk_desktop)
endif()

if(COREDESK_BUILD_NETWORK)
    add_library(coredesk_qt_network
        adapters/qt_network/TcpTransferClient.cpp
        adapters/qt_network/TcpTransferServer.cpp
    )
    target_include_directories(coredesk_qt_network
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/adapters/qt_network>
            $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(coredesk_qt_network PUBLIC coredesk_protocol Qt6::Core Qt6::Network)
    target_compile_features(coredesk_qt_network PUBLIC cxx_std_20)
    coredesk_set_target_warnings(coredesk_qt_network)
    coredesk_enable_sanitizers(coredesk_qt_network)
else()
    add_library(coredesk_qt_network INTERFACE)
    target_link_libraries(coredesk_qt_network INTERFACE coredesk_protocol)
endif()

if(COREDESK_BUILD_NETWORK)
    target_link_libraries(coredesk_service PRIVATE coredesk_qt_network)
    target_compile_definitions(coredesk_service PRIVATE COREDESK_BUILD_NETWORK=1)
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
        tests/unit/test_frame_protocol.cpp
        tests/unit/test_index_builder.cpp
        tests/unit/test_json_payload.cpp
        tests/unit/test_service_controller.cpp
        tests/unit/test_file_scanner.cpp
        tests/unit/test_lru_cache.cpp
        tests/unit/test_result.cpp
        tests/unit/test_search_engine.cpp
        tests/unit/test_thread_pool.cpp
        tests/unit/test_tokenizer.cpp
    )
    target_link_libraries(coredesk_tests PRIVATE coredesk_common coredesk_filesystem coredesk_index coredesk_protocol coredesk_service_lib GTest::gtest_main)
    target_compile_features(coredesk_tests PRIVATE cxx_std_20)
    coredesk_set_target_warnings(coredesk_tests)
    coredesk_enable_sanitizers(coredesk_tests)

    include(GoogleTest)
    gtest_discover_tests(coredesk_tests)

    set(COREDESK_INTEGRATION_TEST_SOURCES
        tests/integration/test_local_ipc.cpp
    )
    if(COREDESK_BUILD_NETWORK)
        list(APPEND COREDESK_INTEGRATION_TEST_SOURCES
            tests/integration/test_service_tcp_integration.cpp
            tests/integration/test_tcp_transfer.cpp
        )
    endif()

    add_executable(coredesk_integration_tests
        ${COREDESK_INTEGRATION_TEST_SOURCES}
    )
    target_link_libraries(coredesk_integration_tests PRIVATE coredesk_common coredesk_protocol coredesk_service_lib coredesk_qt_ipc coredesk_qt_ipc_server Qt6::Core Qt6::Network GTest::gtest_main)
    if(COREDESK_BUILD_NETWORK)
        target_link_libraries(coredesk_integration_tests PRIVATE coredesk_qt_network)
    endif()
    target_compile_features(coredesk_integration_tests PRIVATE cxx_std_20)
    coredesk_set_target_warnings(coredesk_integration_tests)
    coredesk_enable_sanitizers(coredesk_integration_tests)
    add_test(NAME coredesk_integration_tests COMMAND coredesk_integration_tests)
    if(WIN32)
        get_filename_component(COREDESK_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
        set_tests_properties(coredesk_integration_tests
            PROPERTIES
                ENVIRONMENT "PATH=${COREDESK_QT_PREFIX}/bin"
        )
    endif()

    if(COREDESK_BUILD_UI)
        add_executable(coredesk_desktop_tests
            tests/unit/test_desktop_ui.cpp
            ui/MainWindow.cpp
            ui/SearchWidget.cpp
        )
        target_include_directories(coredesk_desktop_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui)
        target_link_libraries(coredesk_desktop_tests PRIVATE coredesk_common coredesk_protocol coredesk_qt_ipc Qt6::Widgets GTest::gtest_main)
        target_compile_features(coredesk_desktop_tests PRIVATE cxx_std_20)
        coredesk_set_target_warnings(coredesk_desktop_tests)
        coredesk_enable_sanitizers(coredesk_desktop_tests)
        add_test(NAME coredesk_desktop_tests COMMAND coredesk_desktop_tests)
        if(WIN32)
            set_tests_properties(coredesk_desktop_tests
                PROPERTIES
                    ENVIRONMENT "PATH=${COREDESK_QT_PREFIX}/bin;QT_QPA_PLATFORM=offscreen"
            )
        else()
            set_tests_properties(coredesk_desktop_tests
                PROPERTIES
                    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            )
        endif()
    endif()
endif()
