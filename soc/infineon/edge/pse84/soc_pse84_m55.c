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
#include <ifx_cycfg_init.h>

#include "cy_pdl.h"

#include "mtb_srf_pool_init.h"
#include "cy_syslib.h"


#include "mtb_srf.h"
#include "mtb_ipc_config.h"
#include "mtb_srf_ipc_init.h"

#define CY_IPC_MAX_ENDPOINTS (8UL)

#define CM55_STARTUP_WAIT_MS 50u

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
    #if defined(MTB_SRF_SUBMIT_USE_IPC)
    mtb_ipc_semaphore_process_interrupt(&cybsp_cm55_ipc_instance);
    #else
    mtb_ipc_semaphore_process_interrupt(&cybsp_cm33_ipc_instance);
    #endif
}

void cybsp_srf_ipc_queue_interrupt_handler(void)
{
    #if defined(MTB_SRF_SUBMIT_USE_IPC)
    mtb_ipc_queue_process_interrupt(&cybsp_cm55_ipc_instance);
    #else
    mtb_ipc_queue_process_interrupt(&cybsp_cm33_ipc_instance);
    #endif
}

mtb_srf_pool_t cy_pdl_srf_default_pool;

cy_rslt_t mtb_srf_request_submit(
    mtb_srf_invec_ns_t* inVec_ns, uint8_t inVec_cnt_ns,
    mtb_srf_outvec_ns_t* outVec_ns, uint8_t outVec_cnt_ns)
{
    #if defined(MTB_SRF_SUBMIT_USE_IPC) && defined(COMPONENT_MW_MTB_IPC)
    return mtb_srf_ipc_request_submit(&cybsp_mtb_srf_client_context, inVec_ns, inVec_cnt_ns, outVec_ns, outVec_cnt_ns);
    #endif /* if defined(MTB_SRF_SUBMIT_USE_IPC) && defined(COMPONENT_MW_MTB_IPC) */
}

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

//     cy_stc_sysint_t intr_cfg_sema = {.intrSrc = (IRQn_Type)CY_IPC_INTR_MUX(ipc_config.semaphore_irq), .intrPriority = 7u};
//     Cy_SysInt_Init(&intr_cfg_sema, cybsp_srf_ipc_semaphore_interrupt_handler);
//     cy_stc_sysint_t intr_cfg_queue = {.intrSrc = (IRQn_Type)CY_IPC_INTR_MUX(ipc_config.queue_irq), .intrPriority = 7u};
//     Cy_SysInt_Init(&intr_cfg_queue, cybsp_srf_ipc_queue_interrupt_handler);
//     NVIC_EnableIRQ((IRQn_Type)CY_IPC_INTR_MUX(ipc_config.semaphore_irq));
//     NVIC_EnableIRQ((IRQn_Type)CY_IPC_INTR_MUX(ipc_config.queue_irq));
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
	ifx_cycfg_init();

	/* Initialize SystemCoreClock variable. */
//	SystemCoreClockUpdate();
	_cybsp_ipc_srf_init();

	static cy_stc_ipc_pipe_ep_t systemIpcPipeEpArray[CY_IPC_MAX_ENDPOINTS];

	Cy_IPC_Pipe_Config(systemIpcPipeEpArray);

	/* This time is needed for m55 core to wait for the m33 to finish
	 * configuring peripherals.
	 */
	Cy_SysLib_Delay(CM55_STARTUP_WAIT_MS);
}

cy_israddress Cy_SysInt_SetVector(IRQn_Type IRQn, cy_israddress userIsr)
{
	return 0;
}
