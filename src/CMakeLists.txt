cmake_minimum_required(VERSION 3.16)

option(BUILD_TESTS "Build unit tests" ON)

file(GLOB_RECURSE SRC_ALL CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

list(FILTER SRC_ALL EXCLUDE REGEX ".*/main\\.cpp$")

add_library(core STATIC ${SRC_ALL})

target_include_directories(core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

find_library(MYSQLCLIENT_LIB NAMES mysqlclient PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib)

if(NOT MYSQLCLIENT_LIB)
    message(FATAL_ERROR "mysqlclient library not found. Install libmysqlclient-dev or set MYSQLCLIENT_LIB.")
endif()

target_link_libraries(core
    PUBLIC OpenSSL::SSL OpenSSL::Crypto Threads::Threads ${MYSQLCLIENT_LIB}
)

add_executable(server ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
target_link_libraries(server PRIVATE core)

target_compile_options(core PRIVATE -Wall -Wextra -Wpedantic)
target_compile_options(server PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTS)
    enable_testing()
    add_executable(test_all ${CMAKE_CURRENT_SOURCE_DIR}/test/test_all.cpp)
    target_link_libraries(test_all PRIVATE core)
    target_include_directories(test_all PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(test_all PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME unit_tests COMMAND test_all)
    option(BUILD_ASAN_TESTS "Build AddressSanitizer tests" OFF)
    if(BUILD_ASAN_TESTS)
        add_executable(test_all_asan ${CMAKE_CURRENT_SOURCE_DIR}/test/test_all.cpp)
        target_link_libraries(test_all_asan PRIVATE core)
        target_compile_options(test_all_asan PRIVATE -Wall -Wextra -Wpedantic -fsanitize=address -fsanitize=undefined)
        target_link_options(test_all_asan PRIVATE -fsanitize=address -fsanitize=undefined)
        add_test(NAME unit_tests_asan COMMAND test_all_asan)
    endif()
endif()
