/***********************************************************************************************//**
 * \file cyabs_rtos_types.h
 *
 * \brief
 * Internal type definitions for RTOS abstraction layer - Zephyr RTOS port
 *
 ***************************************************************************************************
 * \copyright
 * Copyright 2019-2024 Cypress Semiconductor Corporation (an Infineon company) or
 * an affiliate of Cypress Semiconductor Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 **************************************************************************************************/

#pragma once

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************
*                   Enumerations
******************************************************/

/** RTOS thread priority mapped to Zephyr cooperative/preemptive priority levels.
 *  Zephyr uses: negative = cooperative (higher), 0..CONFIG_NUM_PREEMPT_PRIORITIES-1 = preemptive.
 *  Higher numeric value = lower priority in preemptive range.
 */
typedef enum cy_thread_priority
{
    CY_RTOS_PRIORITY_MIN         = (CONFIG_NUM_PREEMPT_PRIORITIES - 1),
    CY_RTOS_PRIORITY_LOW         = (CONFIG_NUM_PREEMPT_PRIORITIES * 6 / 7),
    CY_RTOS_PRIORITY_BELOWNORMAL = (CONFIG_NUM_PREEMPT_PRIORITIES * 4 / 7),
    CY_RTOS_PRIORITY_NORMAL      = (CONFIG_NUM_PREEMPT_PRIORITIES * 3 / 7),
    CY_RTOS_PRIORITY_ABOVENORMAL = (CONFIG_NUM_PREEMPT_PRIORITIES * 2 / 7),
    CY_RTOS_PRIORITY_HIGH        = (CONFIG_NUM_PREEMPT_PRIORITIES * 1 / 7),
    CY_RTOS_PRIORITY_REALTIME    = 1,
    CY_RTOS_PRIORITY_MAX         = 0
} cy_thread_priority_t;

/******************************************************
*                 Type Definitions
******************************************************/

/** Internal structure for a Zephyr thread handle */
typedef struct cy_zephyr_thread
{
    struct k_thread thread;
    struct k_sem    join_sem;
    bool            thread_done;
} cy_zephyr_thread_t;

/** Internal structure for a Zephyr mutex */
typedef struct cy_zephyr_mutex
{
    struct k_mutex mutex;
    bool           is_recursive;
    int            recursive_count;
} cy_zephyr_mutex_t;

/** Internal structure for a Zephyr event (event flags via poll) */
typedef struct cy_zephyr_event
{
#if defined(CONFIG_EVENTS)
    struct k_event event;
#else
    struct k_sem  signal;
    uint32_t      bits;
#endif
} cy_zephyr_event_t;

/** Internal structure for a Zephyr timer */
typedef struct cy_zephyr_timer
{
    struct k_timer          timer;
    void                    (*cb)(uint32_t arg);
    uint32_t                arg;
    bool                    periodic;
    bool                    running;
    uint32_t                period_ms;
} cy_zephyr_timer_t;

typedef cy_zephyr_thread_t*  cy_thread_t;              /**< Zephyr thread handle */
typedef void*                cy_thread_arg_t;           /**< Argument to thread entry function */
typedef cy_zephyr_mutex_t*   cy_mutex_t;               /**< Zephyr mutex handle */
typedef struct k_sem*        cy_semaphore_t;            /**< Zephyr semaphore handle */
typedef cy_zephyr_event_t*   cy_event_t;               /**< Zephyr event handle */
typedef struct k_msgq*       cy_queue_t;               /**< Zephyr message queue handle */
typedef cy_zephyr_timer_t*   cy_timer_t;               /**< Zephyr timer handle */
typedef uint32_t             cy_timer_callback_arg_t;  /**< Argument to timer callback */
typedef uint32_t             cy_time_t;                /**< Time in milliseconds */
typedef int                  cy_rtos_error_t;          /**< Zephyr error code */

#ifdef __cplusplus
} // extern "C"
#endif
