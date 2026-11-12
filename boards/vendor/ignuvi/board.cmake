# SPDX-License-Identifier: Apache-2.0

board_runner_args(openocd "--cmd-post-verify=ignuvi_run_app")
board_runner_args(stm32cubeprogrammer "--port=swd" "--reset-mode=sw")

# keep first
include(${ZEPHYR_BASE}/boards/common/openocd-stm32.board.cmake)
include(${ZEPHYR_BASE}/boards/common/stm32cubeprogrammer.board.cmake)
