cmake_minimum_required(VERSION 3.16)

project(heatpump-protocols VERSION 1.0.0 LANGUAGES CXX)

add_library(heatpump_protocols INTERFACE)
add_library(heatpump::protocols ALIAS heatpump_protocols)

target_compile_features(heatpump_protocols INTERFACE cxx_std_20)

target_include_directories(heatpump_protocols INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

option(HEATPUMP_BUILD_TESTS "Build tests and examples" ON)

if(HEATPUMP_BUILD_TESTS)
    enable_testing()

    add_executable(heatpump_integration_tests
        tests/integration/test_integration.cpp
    )
    target_link_libraries(heatpump_integration_tests PRIVATE heatpump_protocols)
    target_include_directories(heatpump_integration_tests PRIVATE tests)
    target_compile_options(heatpump_integration_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
    )

    add_test(NAME heatpump_integration COMMAND heatpump_integration_tests)

    add_executable(heatpump_command_example
        examples/command_example.cpp
    )
    target_link_libraries(heatpump_command_example PRIVATE heatpump_protocols)
    target_compile_options(heatpump_command_example PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
    )
endif()

include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
