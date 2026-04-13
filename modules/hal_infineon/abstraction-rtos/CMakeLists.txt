# Copyright (c) 2022 Cypress Semiconductor Corporation.
# SPDX-License-Identifier: Apache-2.0

# Add abstraction-rtos
zephyr_include_directories(${ZEPHYR_HAL_INFINEON_MODULE_DIR}/abstraction-rtos/include)

if(CONFIG_SOC_FAMILY_INFINEON_EDGE)
zephyr_library_sources(${ZEPHYR_HAL_INFINEON_MODULE_DIR}/abstraction-rtos/source/cy_worker_thread.c)

zephyr_include_directories( include/EDGE_ZEPHYR)
zephyr_library_sources(source/EDGE_ZEPHYR/cyabs_rtos_zephyr.c)

else()
# Enable Zephyr support
zephyr_include_directories_ifdef(CONFIG_USE_INFINEON_ABSTRACTION_RTOS
        include/COMPONENT_ZEPHYR)
zephyr_library_sources_ifdef(CONFIG_USE_INFINEON_ABSTRACTION_RTOS
        source/COMPONENT_ZEPHYR/cyabs_rtos_zephyr.c)
endif()
