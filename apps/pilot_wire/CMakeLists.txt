# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pilot_wire)

target_sources(app PRIVATE src/main.c src/pilot_wire.c src/pilot_wire_logic.c)
target_sources_ifdef(CONFIG_SHELL app PRIVATE src/shell.c)
