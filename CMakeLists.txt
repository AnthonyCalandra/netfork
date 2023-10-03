# netfork
# Copyright (C) 2023 Anthony Calandra
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU Affero General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option) any
# later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
# PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License along
# with this program. If not, see <https://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.16)

project(netfork VERSION 0.1 LANGUAGES C CXX)

include(FetchContent)
FetchContent_Declare(phnt
	GIT_REPOSITORY https://github.com/winsiderss/phnt.git)
FetchContent_MakeAvailable(phnt)

set(PHNT_VERSION "PHNT_THRESHOLD" CACHE STRING "Set the PHNT version (optional)")
message("PHNT_VERSION set to: ${PHNT_VERSION}")

add_library(netfork-shared STATIC
	${CMAKE_CURRENT_SOURCE_DIR}/netfork-shared/netfork-shared/net/sock.cpp)
target_compile_features(netfork-shared PUBLIC cxx_std_23)
# Help IDEs identify header files.
target_sources(netfork-shared PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/netfork-shared/netfork-shared/net/msg.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/netfork-shared/netfork-shared/net/sock.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/netfork-shared/netfork-shared/net/sock.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/netfork-shared/netfork-shared/auto.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/netfork-shared/netfork-shared/log.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/netfork-shared/netfork-shared/phnt_stub.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/netfork-shared/netfork-shared/utils.hpp)
target_include_directories(netfork-shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/netfork-shared)
target_link_libraries(netfork-shared PUBLIC phnt ws2_32)
target_compile_definitions(netfork-shared INTERFACE PHNT_VERSION=${PHNT_VERSION})

add_library(netfork-lib STATIC
	${CMAKE_CURRENT_SOURCE_DIR}/netfork-lib/netfork.cpp)
target_sources(netfork-lib PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/netfork-lib/netfork.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/netfork-lib/netfork.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/netfork-lib/vm.hpp)
target_link_libraries(netfork-lib PRIVATE netfork-shared)
target_compile_features(netfork-lib PUBLIC cxx_std_23)
target_compile_definitions(netfork-lib PRIVATE NOMINMAX)

add_executable(netfork-server ${CMAKE_CURRENT_SOURCE_DIR}/netfork-server/server.cpp)
target_sources(netfork-server PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/netfork-server/image.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/netfork-server/pe.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/netfork-server/proc.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/netfork-server/vm.hpp)
target_link_libraries(netfork-server PRIVATE mincore netfork-shared)
target_compile_features(netfork-server PUBLIC cxx_std_23)
target_compile_definitions(netfork-server PRIVATE NOMINMAX)

option(BUILD_EXAMPLE "Build example code." ON)
if(BUILD_EXAMPLE)
	add_executable(netfork-example-client ${CMAKE_CURRENT_SOURCE_DIR}/example/client.cpp)
	target_compile_features(netfork-example-client PRIVATE cxx_std_23)
	target_compile_definitions(netfork-example-client PRIVATE NOMINMAX)
	target_link_libraries(netfork-example-client PRIVATE netfork-lib netfork-shared)
	target_include_directories(netfork-example-client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/netfork-lib)
endif()
