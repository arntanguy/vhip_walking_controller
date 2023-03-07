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

set(CONTROLLER_SRC
    Controller.cpp
    FloatingBaseObserver.cpp
    FootstepPlan.cpp
    HRP4ForceCalibrator.cpp
    ModelPredictiveControl.cpp
    NetWrenchObserver.cpp
    Pendulum.cpp
    Stabilizer.cpp
    SwingFoot.cpp
    gui/Controller.cpp)

set(CONTROLLER_HDR
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/vhip_walking/Contact.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/vhip_walking/Controller.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/vhip_walking/FloatingBaseObserver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/vhip_walking/FootstepPlan.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/vhip_walking/HRP4ForceCalibrator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/vhip_walking/ModelPredictiveControl.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/vhip_walking/NetWrenchObserver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/vhip_walking/Pendulum.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/vhip_walking/Preview.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/vhip_walking/Sole.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/vhip_walking/Stabilizer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/vhip_walking/State.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/vhip_walking/SwingFoot.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/vhip_walking/defs.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/vhip_walking/utils/LeakyIntegrator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/vhip_walking/utils/LowPassVelocityFilter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/vhip_walking/utils/clamp.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/vhip_walking/utils/polynomials.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/vhip_walking/utils/rotations.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/vhip_walking/utils/stats.h)

add_library(${PROJECT_NAME} SHARED ${CONTROLLER_SRC} ${CONTROLLER_HDR})
target_include_directories(${PROJECT_NAME} PUBLIC $<INSTALL_INTERFACE:include> $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)
target_link_libraries(${PROJECT_NAME} PUBLIC copra::copra eigen-lssol::eigen-lssol mc_rtc::mc_control_fsm PkgConfig::geometry_msgs PkgConfig::roscpp PkgConfig::roslib PkgConfig::std_msgs PkgConfig::tf)
install(TARGETS ${PROJECT_NAME} DESTINATION "${MC_RTC_LIBDIR}")

add_controller(${PROJECT_NAME}_controller lib.cpp "")
set_target_properties(${PROJECT_NAME}_controller PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")
target_link_libraries(${PROJECT_NAME}_controller PUBLIC ${PROJECT_NAME})

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../etc/VHIPWalking.conf.cmake" "${CMAKE_CURRENT_BINARY_DIR}/etc/VHIPWalking.conf")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/etc/VHIPWalking.conf" DESTINATION "${MC_RTC_LIBDIR}/mc_controller/etc/")

add_subdirectory(states)

install(TARGETS ${PROJECT_NAME}
  EXPORT "${TARGETS_EXPORT_NAME}"
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib)

install(
  DIRECTORY ${CMAKE_SOURCE_DIR}/include/ DESTINATION include)
