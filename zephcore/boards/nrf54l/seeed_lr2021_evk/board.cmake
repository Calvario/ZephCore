# Copyright (c) 2026 ZephCore
# SPDX-License-Identifier: MIT
#
# The nRF54L15 has no USB peripheral and this kit has no bootloader — the
# expansion board's USB-C goes to a SAMD11 that presents CMSIS-DAP for flashing
# and a USB CDC for the uart20 console. So openocd over CMSIS-DAP is the default
# path and is listed first; J-Link/nrfutil work through the SWD header (J7_SWD).
#
# support/openocd.cfg is Zephyr's XIAO nRF54L15 config, copied verbatim. It
# defaults the interface to cmsis-dap and carries the nRF54L CTRL-AP recovery
# procedure, which is what un-bricks a board whose APPROTECT got engaged.

board_runner_args(openocd "--cmd-load=nrf54l-load" -c "targets nrf54l.cpu")
board_runner_args(jlink "--device=nRF54L15_M33" "--speed=4000")

include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/nrfutil.board.cmake)
include(${ZEPHYR_BASE}/boards/common/nrfjprog.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
