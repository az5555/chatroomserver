cmake_minimum_required(VERSION 3.16)
project(ChatRoomServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
	enable_testing()
endif()

add_subdirectory(src)
