# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(BOARD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

# Board-level overlay and Kconfig must be injected before find_package
# so they are processed during DTS and Kconfig configuration.
list(APPEND EXTRA_DTC_OVERLAY_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/../boards/st/fahv3/fahv3.overlay")
list(APPEND EXTRA_CONF_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/../boards/st/fahv3/fahv3.conf")

set(DISPLAY_SAMPLE_DIR $ENV{ZEPHYR_BASE}/samples/drivers/display)
set(KCONFIG_ROOT ${DISPLAY_SAMPLE_DIR}/Kconfig)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(display)

target_sources(app PRIVATE
    ${DISPLAY_SAMPLE_DIR}/src/main.c
    ${DISPLAY_SAMPLE_DIR}/src/display.c
)
target_include_directories(app PRIVATE ${DISPLAY_SAMPLE_DIR}/src)
