# Copyright (c) 2026 ZephCore
# SPDX-License-Identifier: MIT
#
# The nRF54L15 has no USB peripheral and this board has no bootloader — the
# Type-C port is a CH340x UART bridge only. Flashing is SWD, via a J-Link or
# any probe openocd/nrfutil can drive. `west flash` picks nrfutil first.

board_runner_args(jlink "--device=nRF54L15_M33" "--speed=4000")
board_runner_args(openocd "--cmd-load=nrf54l-load" -c "targets nrf54l.cpu")

include(${ZEPHYR_BASE}/boards/common/nrfutil.board.cmake)
include(${ZEPHYR_BASE}/boards/common/nrfjprog.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
