cmake_minimum_required(VERSION 3.0)

project(pachromecastsink)


set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -Wall -Wextra -march=native")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -Og -D_GLIBCXX_DEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Ofast")

find_package(Boost 1.58 REQUIRED COMPONENTS thread system iostreams)

include_directories(${Boost_INCLUDE_DIRS})

set(SOURCE_FILES src/main.cpp)

add_executable(prog ${SOURCE_FILES})
target_link_libraries(prog ${Boost_LIBRARIES})

