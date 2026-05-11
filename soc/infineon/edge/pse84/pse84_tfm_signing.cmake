# Copyright (c) 2026 Infineon Technologies AG,
# or an affiliate of Infineon Technologies AG.
#
# SPDX-License-Identifier: Apache-2.0

# PSE84 TF-M + MCUboot signing script.
#
# Replaces the default cmake/mcuboot.cmake flow for PSE84 NS images.
# The upstream script always reads slot0_partition for slot size and
# hex address, but on PSE84 the Non-Secure image lives in
# slot0_ns_partition (the Secure half is slot0_s_partition). Signing
# the NSPE against slot0's size would place the MCUboot trailer past
# the end of the actual NS slot, breaking validation.
#
# This script:
#   1. Derives slot-size from slot0_ns_partition (0x1C0000) so the
#      MCUboot trailer lands at the correct offset for image 1.
#   2. Produces zephyr.signed.hex / zephyr.signed.bin with --header-size
#      ROM_START_OFFSET and --overwrite-only --align 1 to match the
#      MCUboot bootloader's BOOT_UPGRADE_ONLY mode.
#   3. Overrides the runner hex_file to tfm_merged.hex so `west flash`
#      programs the combined signed SPE + signed NSPE image.

if(NOT DEFINED IMGTOOL)
  message(FATAL_ERROR "PSE84 TF-M signing requires imgtool (set IMGTOOL).")
endif()

# Resolve NS slot geometry.
dt_nodelabel(ns_slot_node NODELABEL "slot0_ns_partition" REQUIRED)
dt_prop(ns_slot_size PATH "${ns_slot_node}" PROPERTY "reg" INDEX 1 REQUIRED)

set(pse84_imgtool_sign
  ${PYTHON_EXECUTABLE} ${IMGTOOL} sign
    --version ${CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION}
    --header-size ${CONFIG_ROM_START_OFFSET}
    --slot-size ${ns_slot_size}
    --overwrite-only
    --align 1
)

set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
  COMMAND ${pse84_imgtool_sign}
    ${CMAKE_BINARY_DIR}/zephyr/${KERNEL_BIN_NAME}
    ${CMAKE_BINARY_DIR}/zephyr/zephyr.signed.bin
  COMMAND ${pse84_imgtool_sign}
    ${CMAKE_BINARY_DIR}/zephyr/${KERNEL_HEX_NAME}
    ${CMAKE_BINARY_DIR}/zephyr/zephyr.signed.hex
)

set_property(GLOBAL APPEND PROPERTY extra_post_build_byproducts
  ${CMAKE_BINARY_DIR}/zephyr/zephyr.signed.bin
  ${CMAKE_BINARY_DIR}/zephyr/zephyr.signed.hex
)

# The TF-M module CMakeLists deferred the SPE+NSPE mergehex to here so
# that the signed NSPE (zephyr.signed.hex, produced just above) is
# included in tfm_merged.hex instead of the raw NS image. This lets
# MCUboot validate image 1 (NSPE) at slot2 in addition to image 0
# (SPE) at slot0.
get_property(pse84_tfm_s_merge_hex_file GLOBAL
             PROPERTY pse84_tfm_s_merge_hex_file)
get_property(pse84_merged_hex_file GLOBAL
             PROPERTY pse84_merged_hex_file)

if(pse84_merged_hex_file)
  set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
    COMMAND ${PYTHON_EXECUTABLE} ${ZEPHYR_BASE}/scripts/build/mergehex.py
      -o ${pse84_merged_hex_file}
      ${pse84_tfm_s_merge_hex_file}
      ${CMAKE_BINARY_DIR}/zephyr/zephyr.signed.hex
  )
  set_property(GLOBAL APPEND PROPERTY extra_post_build_byproducts
    ${pse84_merged_hex_file}
  )
endif()

set_target_properties(runners_yaml_props_target PROPERTIES
  hex_file ${CMAKE_BINARY_DIR}/zephyr/tfm_merged.hex)
