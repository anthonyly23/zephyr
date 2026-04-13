/*
 * Copyright (c) 2025 Infineon Technologies AG,
 * or an affiliate of Infineon Technologies AG.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief Infineon PSOC EDGE84 soc.
 */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include "soc.h"
#include <cy_sysint.h>
#include <system_edge.h>
// TODO_LC This is probably still needed in the non TF-M interlinked case
// #include <ifx_cycfg_init.h> 

#include "cy_pdl.h"

#include "mtb_srf_pool_init.h"
#include "cy_syslib.h"


#include "mtb_srf.h"
#include "mtb_ipc_config.h"
#include "mtb_srf_ipc_init.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#define CY_IPC_MAX_ENDPOINTS (8UL)

#define CM55_STARTUP_WAIT_MS 50u


#define PSE84_CPU_FREQ_HZ DT_PROP(DT_PATH(cpus, cpu_1), clock_frequency)
#define MTB_IPC_IRQ_SEMA_SRF                        (MTB_IPC_IRQ_SEMA_SRF_CLIENT)
#define MTB_IPC_IRQ_QUEUE_SRF                       (MTB_IPC_IRQ_QUEUE_SRF_CLIENT)

#define MTB_SRF_IPC_SEMA_COUNT (MTB_IPC_SEMA_NUM_SRF_REQ_END - MTB_IPC_SEMA_NUM_SRF_REQ_START + 1)

/** Context for the SRF client */
mtb_srf_ipc_client_context_t cybsp_mtb_srf_client_context;

CY_SECTION_SHAREDMEM _MTB_SRF_DATA_ALIGN mtb_srf_ipc_packet_t cybsp_srf_ipc_requests[MTB_SRF_POOL_SIZE];
mtb_ipc_semaphore_t cybsp_cm55_srf_ipc_semaphores[MTB_SRF_IPC_SEMA_COUNT];
mtb_ipc_mbox_sender_t cybsp_srf_ipc_sender;
mtb_srf_ipc_pool_t cybsp_srf_ipc_pool;

mtb_ipc_t cybsp_cm55_ipc_instance;

void cybsp_srf_ipc_semaphore_interrupt_handler(void)
{
    mtb_ipc_semaphore_process_interrupt(&cybsp_cm55_ipc_instance);
}

void cybsp_srf_ipc_queue_interrupt_handler(void)
{
    mtb_ipc_queue_process_interrupt(&cybsp_cm55_ipc_instance);
}

mtb_srf_pool_t cy_pdl_srf_default_pool;
CY_SECTION_SHAREDMEM _MTB_SRF_DATA_ALIGN uint32_t cy_pdl_srf_default_pool_memory[(MTB_SRF_POOL_ENTRY_SIZE(
                                                                         MTB_SRF_MAX_ARG_IN_SIZE,
                                                                         MTB_SRF_MAX_ARG_OUT_SIZE)
                                                                     * MTB_SRF_POOL_SIZE) /
                                                                    sizeof(uint32_t)];

static cy_rslt_t _cybsp_ipc_srf_init()
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    mtb_ipc_config_t ipc_config =
    {
        .internal_channel_index = MTB_IPC_CHANNEL_SRF,
        .semaphore_irq          = MTB_IPC_IRQ_SEMA_SRF,
        .queue_irq              = MTB_IPC_IRQ_QUEUE_SRF,
        .semaphore_num          = MTB_IPC_SEMA_NUM_SRF
    };


    IRQ_CONNECT(CY_IPC_INTR_MUX(MTB_IPC_IRQ_SEMA_SRF), IRQ_PRIO_LOWEST,
                cybsp_srf_ipc_semaphore_interrupt_handler, NULL, 0);
    IRQ_CONNECT(CY_IPC_INTR_MUX(MTB_IPC_IRQ_QUEUE_SRF), IRQ_PRIO_LOWEST,
                cybsp_srf_ipc_queue_interrupt_handler, NULL, 0);
    irq_enable(CY_IPC_INTR_MUX(MTB_IPC_IRQ_SEMA_SRF));
    irq_enable(CY_IPC_INTR_MUX(MTB_IPC_IRQ_QUEUE_SRF));

    /** Setup IPC for use in secure requests */
    uint32_t mtb_srf_ipc_semaphore_idx_list[MTB_SRF_IPC_SEMA_COUNT];
    /* Initialize semaphore index list */
    for(uint32_t idx = 0; idx < MTB_SRF_IPC_SEMA_COUNT; idx++)
    {
        mtb_srf_ipc_semaphore_idx_list[idx] = idx + MTB_IPC_SEMA_NUM_SRF_REQ_START;
    }

    result = mtb_ipc_get_handle(&cybsp_cm55_ipc_instance, &ipc_config, 1000UL);
    if (result == CY_RSLT_SUCCESS)
    {
        mtb_srf_ipc_client_init_cfg_t client_init_cfg =
        {
            .ipc_instance       = &cybsp_cm55_ipc_instance,
            .mailbox_handle     = &cybsp_srf_ipc_sender,
            .ipc_sem_indexes    = mtb_srf_ipc_semaphore_idx_list,
            .srf_ipc_requests   = cybsp_srf_ipc_requests,
            .srf_ipc_pool       = &cybsp_srf_ipc_pool,
            .semaphore_handles  = cybsp_cm55_srf_ipc_semaphores,
            .mbox_idx           = MTB_IPC_MBOX_IDX_SRF,
            .num_semaphores     = MTB_SRF_IPC_SEMA_COUNT,
            .num_requests       = MTB_SRF_POOL_SIZE
        };
        result = mtb_srf_ipc_client_init(&cybsp_mtb_srf_client_context, &client_init_cfg);
    }

    return result;
}

void soc_early_init_hook(void)
{

	/* Enable Loop and branch info cache */
	__DMB();
	__ISB();
	SCB_EnableICache();
	SCB_EnableDCache();

	/* Initializes the system */
    //This is proably still needed in the non TF-M interlinked case TODO_LC
	// ifx_cycfg_init();

	/* Initialize SystemCoreClock variable. */
	SystemCoreClockSetup(PSE84_CPU_FREQ_HZ, PSE84_CPU_FREQ_HZ);

	/* This time is needed for m55 core to wait for the m33 to finish
	 * configuring peripherals.
	 */
	Cy_SysLib_Delay(CM55_STARTUP_WAIT_MS);

    // The init is done on cm33ns this call allowas to retrieve the configuration
    Cy_IPC_Sema_Init(IPC0_SEMA_CH_NUM, 0, NULL);

	mtb_srf_pool_init(&cy_pdl_srf_default_pool, &cy_pdl_srf_default_pool_memory[0],
			  MTB_SRF_POOL_SIZE, MTB_SRF_MAX_ARG_IN_SIZE, MTB_SRF_MAX_ARG_OUT_SIZE);

	_cybsp_ipc_srf_init();

	static cy_stc_ipc_pipe_ep_t systemIpcPipeEpArray[CY_IPC_MAX_ENDPOINTS];

	Cy_IPC_Pipe_Config(systemIpcPipeEpArray);

}

cy_israddress Cy_SysInt_SetVector(IRQn_Type IRQn, cy_israddress userIsr)
{
	return 0;
}
