/*
 * Copyright (c) 2026 Infineon Technologies AG,
 * or an affiliate of Infineon Technologies AG.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief Infineon PSOC EDGE84 soc.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#define PSE84_CPU_FREQ_HZ DT_PROP(DT_PATH(cpus, cpu_0), clock_frequency)

#if defined(CONFIG_BUILD_WITH_TFM)
#include "mtb_srf_pool_init.h"
#include "cy_syslib.h"

#include "mtb_srf.h"
#include "mtb_ipc_config.h"
#include "mtb_srf_ipc_init.h"

#define MTB_SRF_IPC_SEMA_COUNT (MTB_IPC_SEMA_NUM_SRF_REQ_END - MTB_IPC_SEMA_NUM_SRF_REQ_START + 1)

mtb_srf_pool_t cy_pdl_srf_default_pool;
CY_SECTION_SHAREDMEM _MTB_SRF_DATA_ALIGN uint32_t cy_pdl_srf_default_pool_memory[
								(MTB_SRF_POOL_ENTRY_SIZE(
								MTB_SRF_MAX_ARG_IN_SIZE,
								MTB_SRF_MAX_ARG_OUT_SIZE)
								* MTB_SRF_POOL_SIZE) /
								sizeof(uint32_t)];

mtb_ipc_t cybsp_cm33_ipc_instance;
CY_SECTION_SHAREDMEM mtb_ipc_shared_t cybsp_srf_ipc_shared_data _MTB_IPC_DATA_ALIGN;
CY_SECTION_SHAREDMEM mtb_ipc_mbox_data_t cybsp_srf_ipc_mbox_data _MTB_IPC_DATA_ALIGN;
CY_SECTION_SHAREDMEM mtb_ipc_semaphore_data_t cybsp_ipc_sem_data[MTB_SRF_IPC_SEMA_COUNT] _MTB_IPC_DATA_ALIGN;
mtb_ipc_semaphore_t cybsp_cm33_srf_ipc_semaphores[MTB_SRF_IPC_SEMA_COUNT];
mtb_ipc_mbox_receiver_t cybsp_srf_ipc_receiver;
#define MTB_IPC_IRQ_SEMA_SRF                        (MTB_IPC_IRQ_SEMA_SRF_RELAY)
#define MTB_IPC_IRQ_QUEUE_SRF                       (MTB_IPC_IRQ_QUEUE_SRF_RELAY)
mtb_srf_ipc_packet_t *cybsp_srf_ring_buffer[MTB_SRF_POOL_SIZE];
mtb_srf_ipc_relay_context_t cybsp_mtb_srf_relay_context;

static cy_rslt_t _cybsp_ipc_srf_init()
{
	cy_rslt_t result = CY_RSLT_SUCCESS;
	mtb_ipc_config_t ipc_config = {.internal_channel_index = MTB_IPC_CHANNEL_SRF,
				       .semaphore_irq = MTB_IPC_IRQ_SEMA_SRF,
				       .queue_irq = MTB_IPC_IRQ_QUEUE_SRF,
				       .semaphore_num = MTB_IPC_SEMA_NUM_SRF};
	uint32_t mtb_srf_ipc_semaphore_idx_list[MTB_SRF_IPC_SEMA_COUNT];

	result = mtb_ipc_init(&cybsp_cm33_ipc_instance, &cybsp_srf_ipc_shared_data, &ipc_config);
	if (result == CY_RSLT_SUCCESS) {
		mtb_srf_ipc_relay_init_cfg_t relay_init_cfg = {
			.ipc_instance = &cybsp_cm33_ipc_instance,
			.mailbox_handle = &cybsp_srf_ipc_receiver,
			.ipc_sem_indexes = mtb_srf_ipc_semaphore_idx_list,
			.mailbox_data = &cybsp_srf_ipc_mbox_data,
			.semaphore_data = cybsp_ipc_sem_data,
			.semaphore_handles = cybsp_cm33_srf_ipc_semaphores,
			.mbox_idx = MTB_IPC_MBOX_IDX_SRF,
			.mbox_read_sema_idx = MTB_IPC_SEMA_NUM_SRF_MBOX_READ,
			.mbox_write_sema_idx = MTB_IPC_SEMA_NUM_SRF_MBOX_WRITE,
			.num_semaphores = MTB_SRF_IPC_SEMA_COUNT,
			.num_requests = MTB_SRF_POOL_SIZE,
			.buffer = cybsp_srf_ring_buffer};
		result = mtb_srf_ipc_relay_init(&cybsp_mtb_srf_relay_context, &relay_init_cfg);
	}
}
#endif /* defined(CONFIG_BUILD_WITH_TFM) */

void soc_early_init_hook(void)
{
	/* Initialize SystemCoreClock variable. */
	SystemCoreClockSetup(PSE84_CPU_FREQ_HZ, PSE84_CPU_FREQ_HZ);

#ifdef CONFIG_BUILD_WITH_TFM
	mtb_srf_pool_init(&cy_pdl_srf_default_pool, &cy_pdl_srf_default_pool_memory[0],
			  MTB_SRF_POOL_SIZE, MTB_SRF_MAX_ARG_IN_SIZE, MTB_SRF_MAX_ARG_OUT_SIZE);

	_cybsp_ipc_srf_init();
#endif /* CONFIG_BUILD_WITH_TFM */

	/* Initialize SystemCoreClock variable. */
	SystemCoreClockUpdate();

	/* Enables PD1 power domain */
	// Cy_System_EnablePD1();

	/* Enables APP_MMIO_TCM memory for CM55 core */
	Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_CM55_TCM_512K_PERI_NR, CY_MMIO_CM55_TCM_512K_GROUP_NR,
				     CY_MMIO_CM55_TCM_512K_SLAVE_NR,
				     CY_MMIO_CM55_TCM_512K_CLK_HF_NR);

	Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_SMIF0_PERI_NR, CY_MMIO_SMIF0_GROUP_NR,
				     CY_MMIO_SMIF0_SLAVE_NR, CY_MMIO_SMIF0_CLK_HF_NR);

	Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_SMIF01_PERI_NR, CY_MMIO_SMIF01_GROUP_NR,
				     CY_MMIO_SMIF01_SLAVE_NR, CY_MMIO_SMIF01_CLK_HF_NR);

	/* Enable SOCMEM */
	// Cy_SysEnableSOCMEM(true);
}

void soc_late_init_hook(void)
{
#if defined(CONFIG_SOC_PSE84_M55_ENABLE)
	ifx_pse84_cm55_startup();
#endif
}
