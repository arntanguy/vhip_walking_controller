# Copyright (c) 2018-2019, CNRS-UM LIRMM
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
# this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

cmake_minimum_required(VERSION 3.1)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)

set(PROJECT_NAME vhip_walking_controller)
set(PROJECT_DESCRIPTION "Walking controller based on variable-height inverted pendulum tracking")
set(PROJECT_URL https://github.com/stephane-caron/vhip_walking_controller/)
set(PROJECT_VERSION 0.8.0)
set(PROJECT_USE_CMAKE_EXPORT TRUE)
set(INSTALL_DOCUMENTATION OFF CACHE BOOL "")
set(INSTALL_GENERATED_HEADERS OFF CACHE BOOL "" FORCE)
set(INSTALL_PKG_CONFIG_FILE OFF CACHE BOOL "" FORCE)
set(CXX_DISABLE_WERROR ON)

include(cmake/base.cmake)

project(${PROJECT_NAME} CXX)

find_package(copra REQUIRED)
find_package(mc_rtc REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(geometry_msgs REQUIRED IMPORTED_TARGET geometry_msgs)
pkg_check_modules(roscpp REQUIRED IMPORTED_TARGET roscpp)
pkg_check_modules(roslib REQUIRED IMPORTED_TARGET roslib)
pkg_check_modules(std_msgs REQUIRED IMPORTED_TARGET std_msgs)
pkg_check_modules(tf REQUIRED IMPORTED_TARGET tf)

add_subdirectory(src)
