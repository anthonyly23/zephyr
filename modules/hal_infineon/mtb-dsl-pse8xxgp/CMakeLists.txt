# Copyright (c) 2025 Infineon Technologies AG,
# or an affiliate of Infineon Technologies AG.
#
# SPDX-License-Identifier: Apache-2.0

set(pdl_dir ${ZEPHYR_HAL_INFINEON_MODULE_DIR}/mtb-dsl-pse8xxgp/pdl)
set(hal_dir ${ZEPHYR_HAL_INFINEON_MODULE_DIR}/mtb-dsl-pse8xxgp/hal)

set(pdl_drv_dir ${pdl_dir}/drivers)
set(pdl_dev_edge_dir ${pdl_dir}/devices)

if(CONFIG_CPU_CORTEX_M33 AND CONFIG_TRUSTED_EXECUTION_SECURE)
  set(sram_code M33SCODE)
elseif(CONFIG_CPU_CORTEX_M33)
  set(sram_code M33CODE)
else()
  set(sram_code ITCM)
endif()

# Generate PDL specific SOC defines
zephyr_library_compile_definitions($<UPPER_CASE:${CONFIG_SOC}>)

# Add mtb-pdl-cat1
zephyr_include_directories(${pdl_drv_dir}/include)
zephyr_include_directories(${pdl_drv_dir}/third_party/ethernet/include)

zephyr_include_directories(${pdl_dev_edge_dir}/include)
zephyr_include_directories(${pdl_dev_edge_dir}/include/ip)
zephyr_library_sources(${pdl_dev_edge_dir}/source/cy_device.c)

if(${ZEPHYR_TOOLCHAIN_VARIANT} STREQUAL "armclang")
  zephyr_library_sources(${pdl_drv_dir}/source/TOOLCHAIN_ARM/cy_syslib_ext.S)
  zephyr_code_relocate(FILES ${pdl_drv_dir}/source/TOOLCHAIN_ARM/cy_syslib_ext.S LOCATION ${sram_code})
else()
  zephyr_library_sources(${pdl_drv_dir}/source/TOOLCHAIN_GCC_ARM/cy_syslib_ext.S)
  zephyr_code_relocate(FILES ${pdl_drv_dir}/source/TOOLCHAIN_GCC_ARM/cy_syslib_ext.S LOCATION ${sram_code})
endif()

# Peripheral drivers
zephyr_library_sources_ifdef(CONFIG_USE_INFINEON_I2C ${pdl_drv_dir}/source/cy_scb_i2c.c)
zephyr_library_sources_ifdef(CONFIG_USE_INFINEON_I2S ${pdl_drv_dir}/source/cy_tdm.c)
zephyr_library_sources_ifdef(CONFIG_USE_INFINEON_I3C ${pdl_drv_dir}/source/cy_i3c.c)
zephyr_library_sources_ifdef(CONFIG_USE_INFINEON_LPTIMER ${pdl_drv_dir}/source/cy_mcwdt.c)
zephyr_library_sources_ifdef(CONFIG_USE_INFINEON_PDM_PCM ${pdl_drv_dir}/source/cy_pdm_pcm_v2.c)
zephyr_library_sources_ifdef(CONFIG_USE_INFINEON_PWM ${pdl_drv_dir}/source/cy_tcpwm_pwm.c)
zephyr_library_sources_ifdef(CONFIG_USE_INFINEON_RTC ${pdl_drv_dir}/source/cy_rtc.c)
zephyr_library_sources_ifdef(CONFIG_USE_INFINEON_SDIO ${pdl_drv_dir}/source/cy_sd_host.c)
zephyr_library_sources_ifdef(CONFIG_USE_INFINEON_SPI ${pdl_drv_dir}/source/cy_scb_spi.c)
zephyr_library_sources_ifdef(CONFIG_USE_INFINEON_TIMER ${pdl_drv_dir}/source/cy_tcpwm_counter.c)
zephyr_library_sources_ifdef(CONFIG_USE_INFINEON_UART ${pdl_drv_dir}/source/cy_scb_uart.c)
zephyr_library_sources_ifdef(CONFIG_USE_INFINEON_WDT ${pdl_drv_dir}/source/cy_wdt.c)

if(CONFIG_USE_INFINEON_AUTANALOG_SAR_ADC OR CONFIG_USE_INFINEON_LPCOMP)
zephyr_library_sources(${pdl_drv_dir}/source/cy_rram.c)
zephyr_library_sources(${pdl_drv_dir}/source/cy_systrimm.c)
endif()

zephyr_library_sources_ifdef(CONFIG_USE_INFINEON_LPCOMP ${pdl_drv_dir}/source/cy_lpcomp.c)

if(CONFIG_USE_INFINEON_TRNG)
  zephyr_library_sources(${pdl_drv_dir}/source/cy_crypto.c)
  zephyr_library_sources(${pdl_drv_dir}/source/cy_crypto_core_trng_v1.c)
  zephyr_library_sources(${pdl_drv_dir}/source/cy_crypto_core_trng_v2.c)
endif()

if(CONFIG_USE_INFINEON_UART OR CONFIG_USE_INFINEON_I2C OR CONFIG_USE_INFINEON_SPI)
  zephyr_library_sources(${pdl_drv_dir}/source/cy_scb_common.c)
endif()

zephyr_library_sources_ifdef(CONFIG_USE_INFINEON_DMA ${pdl_drv_dir}/source/cy_dma.c)

zephyr_library_sources(${pdl_drv_dir}/source/cy_syspm_v4.c)
zephyr_code_relocate(FILES ${pdl_drv_dir}/source/cy_syspm_v4.c LOCATION ${sram_code})

zephyr_library_sources_ifdef(CONFIG_USE_INFINEON_SMIF ${pdl_drv_dir}/source/cy_smif.c)
zephyr_code_relocate(FILES ${pdl_drv_dir}/source/cy_smif.c LOCATION ${sram_code})
zephyr_library_sources_ifdef(CONFIG_USE_INFINEON_SMIF ${pdl_drv_dir}/source/cy_smif_sfdp.c)
zephyr_code_relocate(FILES ${pdl_drv_dir}/source/cy_smif_sfdp.c LOCATION ${sram_code})
zephyr_library_sources_ifdef(CONFIG_USE_INFINEON_SMIF ${pdl_drv_dir}/source/cy_smif_memslot.c)
zephyr_code_relocate(FILES ${pdl_drv_dir}/source/cy_smif_memslot.c LOCATION ${sram_code})
zephyr_library_sources_ifdef(CONFIG_USE_INFINEON_SMIF ${pdl_drv_dir}/source/cy_smif_hb_flash.c)
zephyr_code_relocate(FILES ${pdl_drv_dir}/source/cy_smif_hb_flash.c LOCATION ${sram_code})

zephyr_library_sources_ifdef(CONFIG_USE_INFINEON_SMIF ${pdl_drv_dir}/source/cy_smif_memnum.c)
zephyr_code_relocate(FILES ${pdl_drv_dir}/source/cy_smif_memnum.c LOCATION ${sram_code})

zephyr_library_sources(${pdl_drv_dir}/source/cy_syslib.c)
zephyr_code_relocate(FILES ${pdl_drv_dir}/source/cy_syslib.c LOCATION ${sram_code})

zephyr_library_sources(${pdl_drv_dir}/source/cy_autanalog.c)
zephyr_library_sources(${pdl_drv_dir}/source/cy_autanalog_sar.c)
zephyr_library_sources(${pdl_drv_dir}/source/cy_autanalog_ac.c)
zephyr_library_sources(${pdl_drv_dir}/source/cy_autanalog_ctb.c)
zephyr_library_sources(${pdl_drv_dir}/source/cy_autanalog_ptc.c)
zephyr_library_sources(${pdl_drv_dir}/source/cy_autanalog_dac.c)

# Common part
zephyr_library_sources(${pdl_drv_dir}/source/cy_gpio.c)
zephyr_library_sources(${pdl_drv_dir}/source/cy_ipc_drv.c)
zephyr_library_sources(${pdl_drv_dir}/source/cy_ipc_pipe.c)
zephyr_library_sources(${pdl_drv_dir}/source/cy_ipc_sema.c)

zephyr_library_sources(${pdl_drv_dir}/source/cy_sysclk_v2.c)
zephyr_library_sources(${pdl_drv_dir}/source/cy_mpc.c)
zephyr_library_sources(${pdl_drv_dir}/source/cy_ppc.c)
zephyr_library_sources(${pdl_drv_dir}/source/cy_syspm_pdcm.c)
zephyr_library_sources(${pdl_drv_dir}/source/cy_syspm_ppu.c)
zephyr_library_sources(${pdl_drv_dir}/source/ppu_v1.c)

zephyr_library_sources(${pdl_drv_dir}/source/cy_trigmux.c)

zephyr_include_directories(${hal_dir}/include)
zephyr_library_sources(${hal_dir}/source/mtb_hal_clock.c)

zephyr_library_sources(${pdl_drv_dir}/source/cy_pdl_srf.c)
zephyr_library_sources(${pdl_drv_dir}/source/cy_pdl_srf_common.c)

if(CONFIG_BUILD_WITH_TFM)
  zephyr_library_sources(${hal_dir}/source/mtb_hal_system.c)
  zephyr_library_sources(${hal_dir}/source/mtb_hal_syspm.c)


  set(tfm_ifx_dir ${ZEPHYR_TRUSTED_FIRMWARE_M_MODULE_DIR}/platform/ext/target/infineon/common)

  # Minimum set of folders required for simple cm33s-cm33ns usage 
  zephyr_include_directories(${tfm_ifx_dir}/interface/include)
  zephyr_include_directories(${tfm_ifx_dir}/spe/services/ifx_ext_sp)
  zephyr_include_directories(${tfm_ifx_dir}/spe/svc)
  # Minimum set of files required for simple cm33s-cm33ns usage 
  zephyr_library_sources(${tfm_ifx_dir}/interface/src/ifx_mtb_srf.c)
  zephyr_library_sources(${tfm_ifx_dir}/spe/services/ifx_ext_sp/ifx_ext_sp_api.c)

  # Additional includes and sources for mailbox support
  zephyr_include_directories(${tfm_ifx_dir}/shared/mailbox)
  zephyr_include_directories(${tfm_ifx_dir}/utils)
  zephyr_include_directories(${ZEPHYR_TRUSTED_FIRMWARE_M_MODULE_DIR}/platform/include)
  zephyr_include_directories(${tfm_ifx_dir}/interface/include/ifx_mtb_mailbox)
  zephyr_include_directories(${tfm_ifx_dir}/../pse84/shared)
  zephyr_include_directories(${tfm_ifx_dir}/../pse84/board/KIT_PSOCE84_EVK/shared)

  zephyr_library_sources(${tfm_ifx_dir}/utils/ifx_regions.c)  
  zephyr_library_sources(${tfm_ifx_dir}/interface/src/ifx_mtb_srf_relay.c)
  zephyr_library_sources(${tfm_ifx_dir}/spe/services/mailbox/platform_hal_multi_core_cm55.c)

  # Additional defines for mailbox support
  zephyr_compile_definitions(IFX_MTB_MAILBOX=1) # Needed to ensure we compile the mailbox code in TF-M
  zephyr_compile_definitions(IFX_NS_INTERFACE_TZ=1) # Needed to ensure we compile the non secure TZ interface code in TF-M
  zephyr_compile_definitions(CONFIG_TFM_PSA_CALL_ADDRESS_REMAP=1) # Needed to ensure we use the PSA call address remap defined in TF-M
  
endif()

if(CONFIG_CPU_CORTEX_M55)

# TODO_LC this whole section is for cm55 AND secure aware so the if needs to reflect that
  zephyr_library_sources(${hal_dir}/source/mtb_hal_system.c)
  zephyr_library_sources(${hal_dir}/source/mtb_hal_syspm.c)

  #TODO_LC possibly insteas of selecting all these we could be able to reuse the cm33 folder? 
  zephyr_include_directories(${ZEPHYR_TRUSTED_FIRMWARE_M_MODULE_DIR}/interface/include)
  zephyr_include_directories(${ZEPHYR_TRUSTED_FIRMWARE_M_MODULE_DIR}/secure_fw/include)
  zephyr_include_directories(${ZEPHYR_TRUSTED_FIRMWARE_M_MODULE_DIR}/config)
  zephyr_include_directories(${ZEPHYR_TRUSTED_FIRMWARE_M_MODULE_DIR}/secure_fw/spm/include)

  # Paired-build setup: consume generated PSA manifest headers from cm33_ns build
  # TODO_LC We need to find a way to specify the cm33 build folder instead of hardcoding
  zephyr_include_directories(${ZEPHYR_BASE}/build/tfm/generated/interface/include)
  zephyr_include_directories(${ZEPHYR_BASE}/build/tfm/api_ns/interface/include)
  zephyr_include_directories(${ZEPHYR_BASE}/build/tfm/generated/interface/include)
  zephyr_include_directories(${ZEPHYR_BASE}/build/tfm/api_ns/interface/include)

  set(tfm_ifx_dir ${ZEPHYR_TRUSTED_FIRMWARE_M_MODULE_DIR}/platform/ext/target/infineon/common)
  
  #TODO_LC possibly insteas of selecting all these we could be able to reuse the cm33 folder? 
  zephyr_include_directories(${tfm_ifx_dir}/interface/include)
  zephyr_include_directories(${tfm_ifx_dir}/spe/services/ifx_ext_sp)
  zephyr_include_directories(${tfm_ifx_dir}/spe/svc)
  zephyr_include_directories(${tfm_ifx_dir}/interface/include/ifx_mtb_mailbox)

  zephyr_library_sources(${tfm_ifx_dir}/interface/src/ifx_mtb_srf.c)
  zephyr_library_sources(${tfm_ifx_dir}/spe/services/ifx_ext_sp/ifx_ext_sp_api.c)
  zephyr_library_sources(${tfm_ifx_dir}/interface/src/ifx_mtb_mailbox/ifx_mtb_mailbox_psa_ns_api.c)
  zephyr_library_sources(${tfm_ifx_dir}/interface/src/ifx_mtb_mailbox/ifx_mtb_mailbox.c)
endif()