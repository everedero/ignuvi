# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wifi)

target_sources(app PRIVATE src/main.c src/whd_resources.c)

# Wi-Fi firmware and NVRAM, kept untracked in fw/ (see the README)
set(fw_bin ${CMAKE_CURRENT_SOURCE_DIR}/${CONFIG_IGNUVI_WIFI_FW})
set(nvram_txt ${CMAKE_CURRENT_SOURCE_DIR}/${CONFIG_IGNUVI_WIFI_NVRAM})
set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated)
if(NOT EXISTS ${fw_bin} OR NOT EXISTS ${nvram_txt})
  message(FATAL_ERROR "Missing ${fw_bin} or ${nvram_txt}, see the README Wi-Fi section")
endif()
generate_inc_file_for_target(app ${fw_bin} ${gen_dir}/wifi_fw.inc)

# NVRAM text -> "key=value\0" C string, the format WHD downloads
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${nvram_txt})
file(STRINGS ${nvram_txt} nvram_lines)
set(nvram_c "")
foreach(line IN LISTS nvram_lines)
  string(STRIP "${line}" line)
  if(line STREQUAL "" OR line MATCHES "^#")
    continue()
  endif()
  string(REPLACE "\\" "\\\\" line "${line}")
  string(REPLACE "\"" "\\\"" line "${line}")
  string(APPEND nvram_c "\t\"${line}\\0\"\n")
endforeach()
file(CONFIGURE OUTPUT ${gen_dir}/wifi_nvram_image_data.h CONTENT
  "/* Generated from ${CONFIG_IGNUVI_WIFI_NVRAM} */\nstatic const char wifi_nvram_image[] =\n${nvram_c}\t\"\";\n")
