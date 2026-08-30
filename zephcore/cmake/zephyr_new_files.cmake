# Copy patches/zephyr-new/* into the Zephyr tree.
#
# These are whole files ZephCore adds to Zephyr that have no upstream to patch
# against — today the LR11xx and LR20xx LoRa drivers and their devicetree
# bindings. They are copied rather than patched because there is nothing to
# apply a diff to.
#
# This lives in its own module because it has to run from TWO places, and the
# order matters:
#
#   - zephcore/CMakeLists.txt, for the application image.
#   - zephcore/sysbuild/CMakeLists.txt, BEFORE find_package(Sysbuild).
#
# Under --sysbuild, sysbuild configures the MCUboot image first, and MCUboot
# runs a full devicetree pass over the same board DTS the app uses. If the
# copy only happened in the app's CMakeLists, MCUboot's DTS pass would see
# whatever version of these bindings a previous build happened to leave behind
# — or, on a clean workspace, none at all. Editing a vendored binding then
# fails the MCUboot image with "not declared in 'properties:'" while the app
# image would have been perfectly happy, and the error points at the copy in
# the Zephyr tree rather than at the real source in patches/zephyr-new.
#
# configure_file(... COPYONLY) is a no-op when the contents already match, so
# running this twice per configure costs nothing and never churns timestamps.
#
# Expects ZEPHCORE_SOURCE_DIR and ZEPHCORE_ZEPHYR_DIR to be set by the caller.

if(EXISTS ${ZEPHCORE_SOURCE_DIR}/patches/zephyr-new)
    file(GLOB_RECURSE ZEPHCORE_NEW_FILES
         RELATIVE ${ZEPHCORE_SOURCE_DIR}/patches/zephyr-new
         ${ZEPHCORE_SOURCE_DIR}/patches/zephyr-new/*)
    foreach(REL_PATH ${ZEPHCORE_NEW_FILES})
        configure_file(${ZEPHCORE_SOURCE_DIR}/patches/zephyr-new/${REL_PATH}
                       ${ZEPHCORE_ZEPHYR_DIR}/${REL_PATH} COPYONLY)
        message(STATUS "  [zephyr-new] ${REL_PATH}")
    endforeach()
endif()
