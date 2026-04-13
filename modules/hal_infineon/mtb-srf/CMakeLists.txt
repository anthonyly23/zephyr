# Copyright (c) 2025 Infineon Technologies AG,
# or an affiliate of Infineon Technologies AG.
#
# SPDX-License-Identifier: Apache-2.0

set(mtb_srf_dir ${ZEPHYR_HAL_INFINEON_MODULE_DIR}/mtb-srf)

zephyr_include_directories(${mtb_srf_dir}/include)
zephyr_library_sources(${mtb_srf_dir}/source/mtb_srf.c)

# If using TF-M we need the custom handling for SRF requests of IPC type
zephyr_compile_definitions(MTB_SRF_IPC_CUSTOM_PACKET_TYPE)
zephyr_compile_definitions(MTB_SRF_CUSTOM_IPC_REQUEST_SUBMIT)

zephyr_compile_definitions(IFX_MTB_SRF)
zephyr_compile_definitions(MTB_SRF_USE_PSA)
zephyr_compile_definitions(CY_RTOS_AWARE)

if(CONFIG_CPU_CORTEX_M33)
  zephyr_compile_definitions(MTB_SRF_CUSTOM_PROCESS_REQUEST)
endif()

if(NOT DEFINED CONFIG_TRUSTED_EXECUTION_SECURE)
    zephyr_include_directories(${mtb_srf_dir}/include/COMPONENT_NON_SECURE_DEVICE)
    zephyr_include_directories(${mtb_srf_dir}/include/COMPONENT_NON_SECURE_DEVICE/COMPONENT_MW_MTB_IPC)
    zephyr_library_sources(${mtb_srf_dir}/source/COMPONENT_NON_SECURE_DEVICE/mtb_srf_pool.c)
    zephyr_library_sources(${mtb_srf_dir}/source/COMPONENT_NON_SECURE_DEVICE/COMPONENT_MW_MTB_IPC/mtb_srf_ipc.c)
endif()
