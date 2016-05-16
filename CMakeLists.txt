cmake_minimum_required(VERSION 3.0)

project(pachromecastsink)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -Wall -Wextra -march=native")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -Og -D_GLIBCXX_DEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Ofast")

find_package(Boost 1.58 REQUIRED COMPONENTS thread system iostreams)
find_package(PkgConfig REQUIRED)
pkg_search_module(avahi-client REQUIRED avahi-client)
pkg_search_module(libpulse REQUIRED libpulse)

include_directories(${Boost_INCLUDE_DIRS}
                    ${avahi-client_INCLUDE_DIRS}
                    ${libpulse_INCLUDE_DIRS})

add_compile_options(${avahi-client_CFLAGS_OTHER})

set(SOURCE_FILES src/main.cpp
                 src/defer.cpp
                 src/asio_avahi_poll.cpp
                 src/chromecast_finder.cpp
                 src/util.cpp)

add_executable(prog ${SOURCE_FILES})
target_link_libraries(prog ${Boost_LIBRARIES} ${avahi-client_LIBRARIES})

add_executable(pa_test src/pa_test.cpp src/defer.cpp)
target_link_libraries(pa_test ${Boost_LIBRARIES} ${libpulse_LIBRARIES})
