/*
 * SPDX-License-Identifier: MIT
 * ZephyrFsFormat - shared factory-format of every ZephCore storage region.
 *
 * Lives outside ZephyrDataStore because the repeater, room server and observer
 * build RepeaterDataStore instead (see CMakeLists role blocks) and must not
 * each grow their own half-implementation of this.
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Erase every ZephCore storage region and remount the filesystems.
 *
 * Unmounts /lfs (and /ext when the board has QSPI), flattens lfs_partition,
 * storage_partition (BLE bonds NVS) and qspi_storage_partition where each
 * exists, then remounts.  A blank LittleFS partition is auto-formatted by
 * fs_mount(), so the volume comes back empty rather than corrupt.
 *
 * This is the only path that erases the LittleFS *volume*.  Deleting files
 * cannot recover a volume another firmware has written into — on nRF52840 the
 * Adafruit core's InternalFileSystem lives at 0xED000, inside our 0xD4000
 * lfs_partition, and its format() scribbles our top 7 blocks.
 *
 * @param out_ext_mounted  optional; receives whether /ext came back mounted.
 *                         Set to false on boards with no QSPI.
 * @return true if /lfs is mounted afterwards.
 */
bool zephcore_fs_format_all(bool *out_ext_mounted);

#ifdef __cplusplus
}
#endif
