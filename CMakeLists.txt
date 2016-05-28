# CMakeLists.txt -- This file is part of pulseaudio-chromecast-sink
# Copyright (C) 2016  Marek Rusinowski
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.0)

project(pachromecastsink)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -Wall -Wextra -march=native")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O1 -D_GLIBCXX_DEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native")

set(CMAKE_CXX_STANDARD_REQUIRED on)
set(CMAKE_CXX_STANDARD 14)

find_package(Boost 1.58 REQUIRED COMPONENTS thread system iostreams)
find_package(PkgConfig REQUIRED)
pkg_search_module(avahi-client REQUIRED avahi-client)
pkg_search_module(libpulse REQUIRED libpulse)

include_directories(${Boost_INCLUDE_DIRS}
                    ${avahi-client_INCLUDE_DIRS}
                    ${libpulse_INCLUDE_DIRS}
                    third-party/spdlog/include)

add_compile_options(${avahi-client_CFLAGS_OTHER})

set(SOURCE_FILES src/main.cpp
                 src/defer.cpp
                 src/asio_avahi_poll.cpp
                 src/chromecast_finder.cpp
                 src/util.cpp
                 src/asio_pa_mainloop_api.cpp
                 src/audio_sinks_manager.cpp)

add_executable(prog ${SOURCE_FILES})
target_link_libraries(prog pthread ${Boost_LIBRARIES} ${avahi-client_LIBRARIES} ${libpulse_LIBRARIES})

add_executable(pa_test src/pa_test.cpp src/defer.cpp src/asio_pa_mainloop_api.cpp)
target_link_libraries(pa_test ${Boost_LIBRARIES} ${libpulse_LIBRARIES})
