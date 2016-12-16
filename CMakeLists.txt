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

cmake_minimum_required(VERSION 3.2 FATAL_ERROR)

project(pachromecastsink VERSION 0.0.1 LANGUAGES CXX)

# Clear default flags used by cmake
set(CMAKE_CXX_FLAGS_RELEASE "")
set(CMAKE_CXX_FLAGS_DEBUG "")

if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "No build type selected, default to Release")
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Release Debug)

find_package(Protobuf REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(spdlog REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(websocketpp REQUIRED)
find_package(gflags REQUIRED)
pkg_search_module(avahi-client REQUIRED avahi-client)
pkg_search_module(libpulse REQUIRED libpulse)

add_subdirectory(proto)

add_executable(pachsink
  src/main.cpp
  src/defer.cpp
  src/asio_avahi_poll.cpp
  src/chromecast_finder.cpp
  src/util.cpp
  src/asio_pa_mainloop_api.cpp
  src/audio_sinks_manager.cpp
  src/chromecast_connection.cpp
  src/chromecasts_manager.cpp
  src/websocket_broadcaster.cpp
  src/chromecast_channel.cpp
  src/network_address.cpp)
target_include_directories(pachsink
  PRIVATE
    ${PROTOBUF_INCLUDE_DIR}
    ${avahi-client_INCLUDE_DIRS}
    ${libpulse_INCLUDE_DIRS}
    ${spdlog_INCLUDE_DIRS}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${websocketpp_INCLUDE_DIR}
    ${GFLAGS_INCLUDE_DIR}
)
target_compile_definitions(pachsink
  PRIVATE
    ASIO_STANDALONE
    PROJECT_VERSION="${PROJECT_VERSION}")
target_link_libraries(pachsink
  pthread
  ${avahi-client_LIBRARIES}
  ${libpulse_LIBRARIES}
  ${PROTOBUF_LIBRARY}
  cast_channel
  ${OPENSSL_LIBRARIES}
  gflags)
target_compile_options(pachsink
  PRIVATE
    ${avahi-client_CFLAGS_OTHER})
set_property(TARGET pachsink PROPERTY CXX_STANDARD 14)

if(CMAKE_CXX_COMPILER_ID MATCHES "(Clang|GNU|Intel)")
  target_compile_options(pachsink PRIVATE -Wall -Wextra)
endif()

if(CMAKE_BUILD_TYPE STREQUAL Debug)
  target_compile_definitions(pachsink
    PRIVATE
      ASIO_ENABLE_BUFFER_DEBUGGING)

  if(CMAKE_CXX_COMPILER_ID MATCHES "(Clang|GNU)")
    target_compile_options(pachsink PRIVATE -g3 -O0)
    target_compile_definitions(pachsink
      PRIVATE
        _GLIBCXX_DEBUG)
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Intel")
    target_compile_options(pachsink PRIVATE -debug all -O0)
  endif()
elseif(CMAKE_BUILD_TYPE STREQUAL Release)
  if(CMAKE_CXX_COMPILER_ID MATCHES "(Clang|GNU|Intel)")
    target_compile_options(pachsink PRIVATE -O2)
  endif()

  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(pachsink PRIVATE -g1)
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(pachsink PRIVATE -gline-tables-only)
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Intel")
    target_compile_options(pachsink PRIVATE -debug minimal)
  endif()
endif()

add_executable(pa_test
  src/pa_test.cpp
  src/defer.cpp
  src/asio_pa_mainloop_api.cpp)
target_link_libraries(pa_test
  ${libpulse_LIBRARIES})
set_property(TARGET pa_test PROPERTY CXX_STANDARD 14)
