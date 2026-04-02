/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include "infineon_kconfig.h"
#include "cy_syslib.h"
#include "cy_pdl.h"
#include <zephyr/device.h>

int main(void)
{
	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);
	Cy_SysEnableCM55(MXCM55, DT_REG_ADDR(DT_NODELABEL(m55_xip)), 1000);
	// /* System Domain Idle Power Mode Configuration */
	// Cy_SysPm_SetDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);

	// /* SoCMEM Idle Power Mode Configuration */
	// Cy_SysPm_SetSOCMEMDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);

	return 0;
}
