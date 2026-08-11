# Copyright (c) 2025 ZephCore
# SPDX-License-Identifier: MIT

zephyr_library()
zephyr_library_compile_definitions(LR20XX_DISABLE_WARNINGS)
# The two LR20XX_WORKAROUNDS_DISABLE_AUTOMATIC_DCDC_* defines that used to live
# here are gone: SDK v2.0.2 deleted lr20xx_workarounds_dcdc_reset/_configure and
# the macros that injected them after set_pkt_type / set_rx_path /
# set_modulation_params. That workaround now lives in the Patch RAM, which this
# driver loads on every reset. Nothing references the defines any more.

zephyr_library_sources(lr20xx_lora.c)

set(LR20XX_SDK_DIR ${APPLICATION_SOURCE_DIR}/adapters/radio/lr20xx)
zephyr_library_sources(${LR20XX_SDK_DIR}/lr20xx_hal_zephyr.c)
zephyr_library_sources(${LR20XX_SDK_DIR}/lr20xx_radio_common.c)
zephyr_library_sources(${LR20XX_SDK_DIR}/lr20xx_radio_lora.c)
zephyr_library_sources(${LR20XX_SDK_DIR}/lr20xx_radio_fifo.c)
zephyr_library_sources(${LR20XX_SDK_DIR}/lr20xx_system.c)
zephyr_library_sources(${LR20XX_SDK_DIR}/lr20xx_regmem.c)
zephyr_library_sources(${LR20XX_SDK_DIR}/lr20xx_workarounds.c)
zephyr_library_sources(${LR20XX_SDK_DIR}/lr20xx_driver_version.c)
# Firmware Patch RAM loader (DS rev 2.1 section 22.3).  The image itself lives in
# lr20xx_pram_lr2021.h, included by lr20xx_lora.c.  lr20xx_pram_load.c is
# deliberately not vendored: it only dispatches between the LR2021 and
# LR2012/LR2022 images, and pulling it in would drag 2 KB of LR20x2 patch data
# into a driver that binds to semtech,lr2021 only.
zephyr_library_sources(${LR20XX_SDK_DIR}/lr20xx_patch.c)

zephyr_library_include_directories(${LR20XX_SDK_DIR})
zephyr_library_add_dependencies(offsets_h)
