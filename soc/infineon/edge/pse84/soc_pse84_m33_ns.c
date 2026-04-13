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

#include "cy_pdl.h"
#include <system_edge.h>

#define PSE84_CPU_FREQ_HZ DT_PROP(DT_PATH(cpus, cpu_0), clock_frequency)

#if defined(CONFIG_BUILD_WITH_TFM)
#include "mtb_srf_pool_init.h"
#include "cy_syslib.h"

#include "mtb_srf.h"
#include "mtb_ipc_config.h"
#include "mtb_srf_ipc_init.h"
#include "mtb_srf_ipc.h"
#include "cyabs_rtos.h"

#define MTB_SRF_IPC_SEMA_COUNT (MTB_IPC_SEMA_NUM_SRF_REQ_END - MTB_IPC_SEMA_NUM_SRF_REQ_START + 1)

mtb_srf_pool_t cy_pdl_srf_default_pool;
CY_SECTION_SHAREDMEM _MTB_SRF_DATA_ALIGN uint32_t
	cy_pdl_srf_default_pool_memory[(MTB_SRF_POOL_ENTRY_SIZE(MTB_SRF_MAX_ARG_IN_SIZE,
								MTB_SRF_MAX_ARG_OUT_SIZE) *
					MTB_SRF_POOL_SIZE) /
				       sizeof(uint32_t)];

mtb_ipc_t cybsp_cm33_ipc_instance;
CY_SECTION_SHAREDMEM mtb_ipc_shared_t cybsp_srf_ipc_shared_data _MTB_IPC_DATA_ALIGN;
CY_SECTION_SHAREDMEM mtb_ipc_mbox_data_t cybsp_srf_ipc_mbox_data _MTB_IPC_DATA_ALIGN;
CY_SECTION_SHAREDMEM mtb_ipc_semaphore_data_t
	cybsp_ipc_sem_data[MTB_SRF_IPC_SEMA_COUNT] _MTB_IPC_DATA_ALIGN;
mtb_ipc_semaphore_t cybsp_cm33_srf_ipc_semaphores[MTB_SRF_IPC_SEMA_COUNT];
mtb_ipc_mbox_receiver_t cybsp_srf_ipc_receiver;
#define MTB_IPC_IRQ_SEMA_SRF  (MTB_IPC_IRQ_SEMA_SRF_RELAY)
#define MTB_IPC_IRQ_QUEUE_SRF (MTB_IPC_IRQ_QUEUE_SRF_RELAY)

mtb_srf_ipc_relay_context_t cybsp_mtb_srf_relay_context;

void cybsp_srf_ipc_semaphore_interrupt_handler(const void *arg)
{
	ARG_UNUSED(arg);
	mtb_ipc_semaphore_process_interrupt(&cybsp_cm33_ipc_instance);
}

void cybsp_srf_ipc_queue_interrupt_handler(const void *arg)
{
	ARG_UNUSED(arg);
	mtb_ipc_queue_process_interrupt(&cybsp_cm33_ipc_instance);
}

#define THREAD_PRIO                5
#define SRF_THREAD_STACK_SIZE      4096
#define SRF_RELAY_READY_TIMEOUT_MS 500

K_MSGQ_DEFINE(cybsp_srf_request_queue_obj, sizeof(void *), MTB_SRF_POOL_SIZE, 4);
static cy_queue_t cybsp_srf_request_queue = &cybsp_srf_request_queue_obj;
K_SEM_DEFINE(cybsp_srf_relay_ready_sem, 0, 2);

K_THREAD_STACK_DEFINE(cybsp_srf_receive_stack, SRF_THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(cybsp_srf_process_stack, SRF_THREAD_STACK_SIZE);
static struct k_thread cybsp_srf_receive_thread;
static struct k_thread cybsp_srf_process_thread;

static void mtb_srf_receive_thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	k_sem_give(&cybsp_srf_relay_ready_sem);
	mtb_srf_ipc_receive_thread(p1);
}

static void mtb_srf_process_thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	k_sem_give(&cybsp_srf_relay_ready_sem);
	mtb_srf_ipc_process_thread(p1);
}

static cy_rslt_t _cybsp_ipc_srf_init()
{
	cy_rslt_t result = CY_RSLT_SUCCESS;
	mtb_ipc_config_t ipc_config = {.internal_channel_index = MTB_IPC_CHANNEL_SRF,
				       .semaphore_irq = MTB_IPC_IRQ_SEMA_SRF,
				       .queue_irq = MTB_IPC_IRQ_QUEUE_SRF,
				       .semaphore_num = MTB_IPC_SEMA_NUM_SRF};
	uint32_t mtb_srf_ipc_semaphore_idx_list[MTB_SRF_IPC_SEMA_COUNT];

	IRQ_CONNECT(CY_IPC_INTR_MUX(MTB_IPC_IRQ_SEMA_SRF), IRQ_PRIO_LOWEST,
		    cybsp_srf_ipc_semaphore_interrupt_handler, NULL, 0);
	IRQ_CONNECT(CY_IPC_INTR_MUX(MTB_IPC_IRQ_QUEUE_SRF), IRQ_PRIO_LOWEST,
		    cybsp_srf_ipc_queue_interrupt_handler, NULL, 0);
	irq_enable(CY_IPC_INTR_MUX(MTB_IPC_IRQ_SEMA_SRF));
	irq_enable(CY_IPC_INTR_MUX(MTB_IPC_IRQ_QUEUE_SRF));

	memset(&cybsp_srf_ipc_shared_data, 0x00, sizeof(cybsp_srf_ipc_shared_data));
	memset(&cybsp_srf_ipc_mbox_data, 0x00, sizeof(cybsp_srf_ipc_mbox_data));
	memset(&cybsp_ipc_sem_data, 0x00,
	       (sizeof(mtb_ipc_semaphore_data_t) * MTB_SRF_IPC_SEMA_COUNT));
	memset(&cybsp_cm33_srf_ipc_semaphores, 0x00,
	       (sizeof(mtb_ipc_semaphore_t) * MTB_SRF_IPC_SEMA_COUNT));

	for (uint32_t idx = 0; idx < MTB_SRF_IPC_SEMA_COUNT; idx++) {
		mtb_srf_ipc_semaphore_idx_list[idx] = idx + MTB_IPC_SEMA_NUM_SRF_REQ_START;
	}

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
			.ipc_req_queue = &cybsp_srf_request_queue};
		result = mtb_srf_ipc_relay_init(&cybsp_mtb_srf_relay_context, &relay_init_cfg);
	}

	return result;
}
#endif /* defined(CONFIG_BUILD_WITH_TFM) */

CY_SECTION_SHAREDMEM cy_stc_ipc_sema_t cybsp_ipc_sema;
CY_SECTION_SHAREDMEM static uint32_t cybsp_ipc_sema_array[CY_IPC_SEMA_COUNT / CY_IPC_SEMA_PER_WORD];
void soc_early_init_hook(void)
{
	/* Initialize SystemCoreClock variable. */
	SystemCoreClockSetup(PSE84_CPU_FREQ_HZ, PSE84_CPU_FREQ_HZ);

	cybsp_ipc_sema.maxSema = CY_IPC_SEMA_COUNT;
	cybsp_ipc_sema.arrayPtr = cybsp_ipc_sema_array;
	cybsp_ipc_sema.arrayPtr_sec = NULL;

	Cy_IPC_Sema_InitExt(IPC0_SEMA_CH_NUM, &cybsp_ipc_sema);

	// TODO_LC Clean up the #if inclusions of code
#ifdef CONFIG_BUILD_WITH_TFM
	mtb_srf_pool_init(&cy_pdl_srf_default_pool, &cy_pdl_srf_default_pool_memory[0],
			  MTB_SRF_POOL_SIZE, MTB_SRF_MAX_ARG_IN_SIZE, MTB_SRF_MAX_ARG_OUT_SIZE);

	_cybsp_ipc_srf_init();
#endif /* CONFIG_BUILD_WITH_TFM */
}

void soc_late_init_hook(void)
{
	k_thread_create(&cybsp_srf_receive_thread, cybsp_srf_receive_stack, SRF_THREAD_STACK_SIZE,
			mtb_srf_receive_thread_entry, &cybsp_mtb_srf_relay_context, NULL, NULL,
			THREAD_PRIO, 0, K_NO_WAIT);

	k_thread_create(&cybsp_srf_process_thread, cybsp_srf_process_stack, SRF_THREAD_STACK_SIZE,
			mtb_srf_process_thread_entry, &cybsp_mtb_srf_relay_context, NULL, NULL,
			THREAD_PRIO, 0, K_NO_WAIT);

	/* Ensure relay threads have started before CM55 can submit first request. */
	if ((k_sem_take(&cybsp_srf_relay_ready_sem, K_MSEC(SRF_RELAY_READY_TIMEOUT_MS)) != 0) ||
	    (k_sem_take(&cybsp_srf_relay_ready_sem, K_MSEC(SRF_RELAY_READY_TIMEOUT_MS)) != 0)) {
		CY_ASSERT(false);
		return;
	}
	// Long enough wait time to hopefully finish up cm33 app before cm55 boots TODO_LC
	Cy_SysEnableCM55(MXCM55, DT_REG_ADDR(DT_NODELABEL(m55_xip)), 5000);
#if defined(CONFIG_SOC_PSE84_M55_ENABLE)
	// Move the enable of cm55 in here so it is dependent on the m55_enable TODO_LC
#endif
}
