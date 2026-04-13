/***********************************************************************************************//**
 * \file cyabs_rtos_zephyr.c
 *
 * \brief
 * Implementation of the RTOS abstraction layer for Zephyr RTOS.
 *
 ***************************************************************************************************
 * \copyright
 * Copyright 2026 Cypress Semiconductor Corporation (an Infineon company) or
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

#include <cyabs_rtos.h>
#include <cy_result.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#if !defined(CONFIG_HEAP_MEM_POOL_SIZE) || (CONFIG_HEAP_MEM_POOL_SIZE == 0)
#define k_malloc(sz) malloc(sz)
#define k_free(ptr)  free(ptr)
#endif

/* Last error recorded for cy_rtos_last_error() */
static int _cy_rtos_last_error = 0;

#ifndef CY_RTOS_STATIC_SEMAPHORE_POOL_SIZE
#define CY_RTOS_STATIC_SEMAPHORE_POOL_SIZE 64
#endif

#ifndef CY_RTOS_STATIC_EVENT_POOL_SIZE
#define CY_RTOS_STATIC_EVENT_POOL_SIZE 16
#endif

struct _cy_static_sem_slot {
    struct k_sem sem;
    bool used;
};

static struct _cy_static_sem_slot _cy_static_sem_pool[CY_RTOS_STATIC_SEMAPHORE_POOL_SIZE];

struct _cy_static_event_slot {
    cy_zephyr_event_t event;
    bool used;
};

static struct _cy_static_event_slot _cy_static_event_pool[CY_RTOS_STATIC_EVENT_POOL_SIZE];

static struct k_sem *_cy_alloc_static_sem(void)
{
    unsigned int key = irq_lock();
    for (size_t i = 0; i < CY_RTOS_STATIC_SEMAPHORE_POOL_SIZE; i++)
    {
        if (!_cy_static_sem_pool[i].used)
        {
            _cy_static_sem_pool[i].used = true;
            irq_unlock(key);
            return &_cy_static_sem_pool[i].sem;
        }
    }
    irq_unlock(key);
    return NULL;
}

static bool _cy_free_static_sem(struct k_sem *sem)
{
    unsigned int key = irq_lock();
    for (size_t i = 0; i < CY_RTOS_STATIC_SEMAPHORE_POOL_SIZE; i++)
    {
        if (&_cy_static_sem_pool[i].sem == sem)
        {
            _cy_static_sem_pool[i].used = false;
            irq_unlock(key);
            return true;
        }
    }
    irq_unlock(key);
    return false;
}

static cy_zephyr_event_t *_cy_alloc_static_event(void)
{
    unsigned int key = irq_lock();
    for (size_t i = 0; i < CY_RTOS_STATIC_EVENT_POOL_SIZE; i++)
    {
        if (!_cy_static_event_pool[i].used)
        {
            _cy_static_event_pool[i].used = true;
            irq_unlock(key);
            return &_cy_static_event_pool[i].event;
        }
    }
    irq_unlock(key);
    return NULL;
}

static bool _cy_free_static_event(cy_zephyr_event_t *event)
{
    unsigned int key = irq_lock();
    for (size_t i = 0; i < CY_RTOS_STATIC_EVENT_POOL_SIZE; i++)
    {
        if (&_cy_static_event_pool[i].event == event)
        {
            _cy_static_event_pool[i].used = false;
            irq_unlock(key);
            return true;
        }
    }
    irq_unlock(key);
    return false;
}

static inline k_timeout_t _ms_to_timeout(cy_time_t timeout_ms)
{
    if (timeout_ms == CY_RTOS_NEVER_TIMEOUT)
    {
        return K_FOREVER;
    }
    return K_MSEC(timeout_ms);
}


//==================================================================================================
// Error Converter
//==================================================================================================

cy_rtos_error_t cy_rtos_last_error(void)
{
    return _cy_rtos_last_error;
}


//==================================================================================================
// Threads
//==================================================================================================

/* Zephyr thread entry wrapper to match cy_thread_entry_fn_t signature */
static void _thread_entry_wrapper(void* arg1, void* arg2, void* arg3)
{
    cy_thread_entry_fn_t entry_fn = (cy_thread_entry_fn_t)arg1;
    cy_thread_arg_t      arg     = arg2;
    cy_zephyr_thread_t*  wrapper = (cy_zephyr_thread_t*)arg3;

    entry_fn(arg);

    /* Signal join semaphore so cy_rtos_thread_join() can unblock */
    wrapper->thread_done = true;
    k_sem_give(&wrapper->join_sem);
}


cy_rslt_t cy_rtos_thread_create(cy_thread_t* thread, cy_thread_entry_fn_t entry_function,
                                const char* name, void* stack, uint32_t stack_size,
                                cy_thread_priority_t priority, cy_thread_arg_t arg)
{
    if ((thread == NULL) || (entry_function == NULL) || (stack_size < CY_RTOS_MIN_STACK_SIZE))
    {
        return CY_RTOS_BAD_PARAM;
    }
    if ((stack != NULL) && (((uintptr_t)stack & CY_RTOS_ALIGNMENT_MASK) != 0U))
    {
        return CY_RTOS_ALIGNMENT_ERROR;
    }

    /* Allocate wrapper struct; allocate stack ourselves if caller did not provide one */
    bool     stack_allocated = (stack == NULL);
    uint8_t* stack_buf       = stack;

    if (stack_allocated)
    {
        stack_buf = (uint8_t*)k_malloc(stack_size);
        if (stack_buf == NULL)
        {
            return CY_RTOS_NO_MEMORY;
        }
    }

    cy_zephyr_thread_t* wrapper = (cy_zephyr_thread_t*)k_malloc(sizeof(cy_zephyr_thread_t));
    if (wrapper == NULL)
    {
        if (stack_allocated)
        {
            k_free(stack_buf);
        }
        return CY_RTOS_NO_MEMORY;
    }

    memset(wrapper, 0, sizeof(cy_zephyr_thread_t));
    k_sem_init(&wrapper->join_sem, 0, 1);
    wrapper->thread_done = false;

    k_tid_t tid = k_thread_create(
        &wrapper->thread,
        (k_thread_stack_t*)stack_buf,
        (size_t)stack_size,
        _thread_entry_wrapper,
        (void*)entry_function,
        arg,
        wrapper,
        (int)priority,
        0,
        K_NO_WAIT);

    if (tid == NULL)
    {
        if (stack_allocated)
        {
            k_free(stack_buf);
        }
        k_free(wrapper);
        return CY_RTOS_GENERAL_ERROR;
    }

    if (name != NULL)
    {
        k_thread_name_set(tid, name);
    }

    *thread = wrapper;
    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_thread_exit(void)
{
    /* In Zephyr, a thread simply returns from its entry function to exit.
     * The join_sem is signalled by the wrapper before returning. */
    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_thread_terminate(cy_thread_t* thread)
{
    if (thread == NULL || *thread == NULL)
    {
        return CY_RTOS_BAD_PARAM;
    }

    cy_zephyr_thread_t* wrapper = *thread;
    k_thread_abort(&wrapper->thread);
    wrapper->thread_done = true;
    k_sem_give(&wrapper->join_sem);

    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_thread_join(cy_thread_t* thread)
{
    if (thread == NULL || *thread == NULL)
    {
        return CY_RTOS_BAD_PARAM;
    }

    cy_zephyr_thread_t* wrapper = *thread;

    /* Wait until the thread signals completion */
    k_sem_take(&wrapper->join_sem, K_FOREVER);

    k_free(wrapper);
    *thread = NULL;

    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_thread_is_running(cy_thread_t* thread, bool* running)
{
    if ((thread == NULL) || (*thread == NULL) || (running == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

    cy_zephyr_thread_t* wrapper = *thread;
    *running = !wrapper->thread_done;
    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_thread_get_state(cy_thread_t* thread, cy_thread_state_t* state)
{
    if ((thread == NULL) || (*thread == NULL) || (state == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

    cy_zephyr_thread_t* wrapper = *thread;
    uint32_t thread_state = wrapper->thread.base.thread_state;

    if (thread_state & _THREAD_DEAD)
    {
        *state = CY_THREAD_STATE_TERMINATED;
    }
    else if (thread_state & _THREAD_SUSPENDED)
    {
        *state = CY_THREAD_STATE_INACTIVE;
    }
    else if (thread_state & _THREAD_PENDING)
    {
        *state = CY_THREAD_STATE_BLOCKED;
    }
    else if (thread_state & _THREAD_QUEUED)
    {
        *state = CY_THREAD_STATE_READY;
    }
    else if (&wrapper->thread == k_current_get())
    {
        *state = CY_THREAD_STATE_RUNNING;
    }
    else
    {
        *state = CY_THREAD_STATE_UNKNOWN;
    }

    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_thread_get_handle(cy_thread_t* thread)
{
    if (thread == NULL)
    {
        return CY_RTOS_BAD_PARAM;
    }

    /* Return the current Zephyr thread struct pointer cast to cy_zephyr_thread_t pointer.
     * Note: this is only safe when used on threads created via cy_rtos_thread_create(). */
    *thread = (cy_zephyr_thread_t*)k_current_get();
    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_thread_wait_notification(cy_time_t timeout_ms)
{
    int ret = k_sleep(_ms_to_timeout(timeout_ms));

    /* k_sleep returns remaining time; 0 means it slept the full duration (timeout) */
    return (ret == 0) ? CY_RTOS_TIMEOUT : CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_thread_set_notification(cy_thread_t* thread)
{
    if ((thread == NULL) || (*thread == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

    cy_zephyr_thread_t* wrapper = *thread;
    k_wakeup(&wrapper->thread);
    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_thread_get_name(cy_thread_t* thread, const char** thread_name)
{
    if ((thread == NULL) || (*thread == NULL) || (thread_name == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

    cy_zephyr_thread_t* wrapper = *thread;
    *thread_name = k_thread_name_get(&wrapper->thread);
    return CY_RSLT_SUCCESS;
}


//==================================================================================================
// Scheduler
//==================================================================================================

cy_rslt_t cy_rtos_scheduler_suspend(void)
{
    k_sched_lock();
    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_scheduler_resume(void)
{
    k_sched_unlock();
    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_scheduler_get_state(cy_scheduler_state_t* state)
{
    if (state == NULL)
    {
        return CY_RTOS_BAD_PARAM;
    }

    if (k_is_pre_kernel())
    {
        *state = CY_SCHEDULER_STATE_NOT_STARTED;
    }
    else if (k_is_in_isr())
    {
        *state = CY_SCHEDULER_STATE_RUNNING;
    }
    else
    {
        *state = CY_SCHEDULER_STATE_RUNNING;
    }

    return CY_RSLT_SUCCESS;
}


//==================================================================================================
// Mutexes
//==================================================================================================

cy_rslt_t cy_rtos_mutex_init(cy_mutex_t* mutex, bool recursive)
{
    if (mutex == NULL)
    {
        return CY_RTOS_BAD_PARAM;
    }

    cy_zephyr_mutex_t* m = (cy_zephyr_mutex_t*)k_malloc(sizeof(cy_zephyr_mutex_t));
    if (m == NULL)
    {
        return CY_RTOS_NO_MEMORY;
    }

    int ret = k_mutex_init(&m->mutex);
    if (ret != 0)
    {
        _cy_rtos_last_error = ret;
        k_free(m);
        return CY_RTOS_GENERAL_ERROR;
    }

    m->is_recursive    = recursive;
    m->recursive_count = 0;
    *mutex             = m;
    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_mutex_get(cy_mutex_t* mutex, cy_time_t timeout_ms)
{
    if ((mutex == NULL) || (*mutex == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

    cy_zephyr_mutex_t* m   = *mutex;
    int                ret = k_mutex_lock(&m->mutex, _ms_to_timeout(timeout_ms));

    if (ret == -EAGAIN || ret == -EBUSY)
    {
        return CY_RTOS_TIMEOUT;
    }
    else if (ret != 0)
    {
        _cy_rtos_last_error = ret;
        return CY_RTOS_GENERAL_ERROR;
    }

    if (m->is_recursive)
    {
        m->recursive_count++;
    }

    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_mutex_set(cy_mutex_t* mutex)
{
    if ((mutex == NULL) || (*mutex == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

    cy_zephyr_mutex_t* m = *mutex;

    if (m->is_recursive && m->recursive_count > 0)
    {
        m->recursive_count--;
    }

    int ret = k_mutex_unlock(&m->mutex);
    if (ret != 0)
    {
        _cy_rtos_last_error = ret;
        return CY_RTOS_GENERAL_ERROR;
    }

    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_mutex_deinit(cy_mutex_t* mutex)
{
    if ((mutex == NULL) || (*mutex == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

    k_free(*mutex);
    *mutex = NULL;
    return CY_RSLT_SUCCESS;
}


//==================================================================================================
// Semaphores
//==================================================================================================

cy_rslt_t cy_rtos_semaphore_init(cy_semaphore_t* semaphore, uint32_t maxcount, uint32_t initcount)
{
    if (semaphore == NULL)
    {
        return CY_RTOS_BAD_PARAM;
    }

    struct k_sem* s = _cy_alloc_static_sem();
    if (s == NULL)
    {
        s = (struct k_sem*)k_malloc(sizeof(struct k_sem));
    }
    if (s == NULL)
    {
        return CY_RTOS_NO_MEMORY;
    }

    int ret = k_sem_init(s, initcount, maxcount);
    if (ret != 0)
    {
        _cy_rtos_last_error = ret;
        if (!_cy_free_static_sem(s))
        {
            k_free(s);
        }
        return CY_RTOS_GENERAL_ERROR;
    }

    *semaphore = s;
    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_semaphore_get(cy_semaphore_t* semaphore, cy_time_t timeout_ms)
{
    if ((semaphore == NULL) || (*semaphore == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

    /* Before the Zephyr scheduler is running, k_sem_take() cannot block.
     * Return timeout on failure so the caller falls back to polling. */
    if (k_is_pre_kernel())
    {
        int ret = k_sem_take(*semaphore, K_NO_WAIT);
        return (ret == 0) ? CY_RSLT_SUCCESS : CY_RTOS_TIMEOUT;
    }

    int ret = k_sem_take(*semaphore, _ms_to_timeout(timeout_ms));

    if (ret == -EAGAIN || ret == -EBUSY)
    {
        return CY_RTOS_TIMEOUT;
    }
    else if (ret != 0)
    {
        _cy_rtos_last_error = ret;
        return CY_RTOS_GENERAL_ERROR;
    }

    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_semaphore_set(cy_semaphore_t* semaphore)
{
    if ((semaphore == NULL) || (*semaphore == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

    k_sem_give(*semaphore);
    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_semaphore_get_count(cy_semaphore_t* semaphore, size_t* count)
{
    if ((semaphore == NULL) || (*semaphore == NULL) || (count == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

    *count = (size_t)k_sem_count_get(*semaphore);
    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_semaphore_deinit(cy_semaphore_t* semaphore)
{
    if ((semaphore == NULL) || (*semaphore == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

    if (!_cy_free_static_sem(*semaphore))
    {
        k_free(*semaphore);
    }
    *semaphore = NULL;
    return CY_RSLT_SUCCESS;
}


//==================================================================================================
// Events
//==================================================================================================

cy_rslt_t cy_rtos_event_init(cy_event_t* event)
{
    if (event == NULL)
    {
        return CY_RTOS_BAD_PARAM;
    }

    cy_zephyr_event_t* e = _cy_alloc_static_event();
    if (e == NULL)
    {
        e = (cy_zephyr_event_t*)k_malloc(sizeof(cy_zephyr_event_t));
    }
    if (e == NULL)
    {
        return CY_RTOS_NO_MEMORY;
    }

#if defined(CONFIG_EVENTS)
    k_event_init(&e->event);
#else
    k_sem_init(&e->signal, 0, 1);
    e->bits = 0U;
#endif
    *event = e;
    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_event_setbits(cy_event_t* event, uint32_t bits)
{
    if ((event == NULL) || (*event == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

#if defined(CONFIG_EVENTS)
    k_event_post(&(*event)->event, bits);
#else
    unsigned int key = irq_lock();
    (*event)->bits |= bits;
    irq_unlock(key);
    k_sem_give(&(*event)->signal);
#endif
    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_event_clearbits(cy_event_t* event, uint32_t bits)
{
    if ((event == NULL) || (*event == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

#if defined(CONFIG_EVENTS)
    k_event_clear(&(*event)->event, bits);
#else
    unsigned int key = irq_lock();
    (*event)->bits &= ~bits;
    irq_unlock(key);
#endif
    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_event_getbits(cy_event_t* event, uint32_t* bits)
{
    if ((event == NULL) || (*event == NULL) || (bits == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

#if defined(CONFIG_EVENTS)
    *bits = k_event_test(&(*event)->event, 0xFFFFFFFFU);
#else
    unsigned int key = irq_lock();
    *bits = (*event)->bits;
    irq_unlock(key);
#endif
    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_event_waitbits(cy_event_t* event, uint32_t* bits, bool clear, bool all,
                                 cy_time_t timeout_ms)
{
    if ((event == NULL) || (*event == NULL) || (bits == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

    uint32_t requested = *bits;
    uint32_t received;

#if defined(CONFIG_EVENTS)
    if (all)
    {
        received = k_event_wait_all(&(*event)->event, requested, clear,
                                    _ms_to_timeout(timeout_ms));
    }
    else
    {
        received = k_event_wait(&(*event)->event, requested, clear,
                                _ms_to_timeout(timeout_ms));
    }

    *bits = received;

    if (received == 0U)
    {
        return CY_RTOS_TIMEOUT;
    }

    if (all && ((received & requested) != requested))
    {
        return CY_RTOS_TIMEOUT;
    }

    return CY_RSLT_SUCCESS;
#else
    int64_t end_ms = 0;
    if (timeout_ms != CY_RTOS_NEVER_TIMEOUT)
    {
        end_ms = k_uptime_get() + (int64_t)timeout_ms;
    }

    while (true)
    {
        unsigned int key = irq_lock();
        uint32_t current = (*event)->bits;
        bool matched = all ? ((current & requested) == requested)
                           : ((current & requested) != 0U);
        if (matched)
        {
            received = current & requested;
            if (clear)
            {
                (*event)->bits &= ~received;
            }
            irq_unlock(key);
            *bits = received;
            return CY_RSLT_SUCCESS;
        }
        irq_unlock(key);

        if (timeout_ms == 0U)
        {
            *bits = 0U;
            return CY_RTOS_TIMEOUT;
        }

        int ret;
        if (timeout_ms == CY_RTOS_NEVER_TIMEOUT)
        {
            ret = k_sem_take(&(*event)->signal, K_FOREVER);
        }
        else
        {
            int64_t now_ms = k_uptime_get();
            if (now_ms >= end_ms)
            {
                *bits = 0U;
                return CY_RTOS_TIMEOUT;
            }
            ret = k_sem_take(&(*event)->signal, K_MSEC(end_ms - now_ms));
        }

        if (ret != 0)
        {
            *bits = 0U;
            return CY_RTOS_TIMEOUT;
        }
    }
#endif
}


cy_rslt_t cy_rtos_event_deinit(cy_event_t* event)
{
    if ((event == NULL) || (*event == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

    if (!_cy_free_static_event(*event))
    {
        k_free(*event);
    }
    *event = NULL;
    return CY_RSLT_SUCCESS;
}


//==================================================================================================
// Queues
//==================================================================================================

cy_rslt_t cy_rtos_queue_init(cy_queue_t* queue, size_t length, size_t itemsize)
{
    if (queue == NULL)
    {
        return CY_RTOS_BAD_PARAM;
    }

    /* Allocate the k_msgq struct plus its backing buffer contiguously */
    size_t   buf_size = length * itemsize;
    uint8_t* mem      = (uint8_t*)k_malloc(sizeof(struct k_msgq) + buf_size);
    if (mem == NULL)
    {
        return CY_RTOS_NO_MEMORY;
    }

    struct k_msgq* q   = (struct k_msgq*)mem;
    char*          buf = (char*)(mem + sizeof(struct k_msgq));

    k_msgq_init(q, buf, itemsize, (uint32_t)length);
    *queue = q;
    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_queue_put(cy_queue_t* queue, const void* item_ptr, cy_time_t timeout_ms)
{
    if ((queue == NULL) || (*queue == NULL) || (item_ptr == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

    int ret = k_msgq_put(*queue, item_ptr, _ms_to_timeout(timeout_ms));

    if (ret == -EAGAIN || ret == -ENOMSG)
    {
        return CY_RTOS_QUEUE_FULL;
    }
    else if (ret != 0)
    {
        _cy_rtos_last_error = ret;
        return CY_RTOS_GENERAL_ERROR;
    }

    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_queue_get(cy_queue_t* queue, void* item_ptr, cy_time_t timeout_ms)
{
    if ((queue == NULL) || (*queue == NULL) || (item_ptr == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

    int ret = k_msgq_get(*queue, item_ptr, _ms_to_timeout(timeout_ms));

    if (ret == -EAGAIN || ret == -ENOMSG)
    {
        return CY_RTOS_QUEUE_EMPTY;
    }
    else if (ret != 0)
    {
        _cy_rtos_last_error = ret;
        return CY_RTOS_GENERAL_ERROR;
    }

    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_queue_count(cy_queue_t* queue, size_t* num_waiting)
{
    if ((queue == NULL) || (*queue == NULL) || (num_waiting == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

    *num_waiting = (size_t)k_msgq_num_used_get(*queue);
    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_queue_space(cy_queue_t* queue, size_t* num_spaces)
{
    if ((queue == NULL) || (*queue == NULL) || (num_spaces == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

    *num_spaces = (size_t)k_msgq_num_free_get(*queue);
    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_queue_reset(cy_queue_t* queue)
{
    if ((queue == NULL) || (*queue == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

    k_msgq_purge(*queue);
    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_queue_deinit(cy_queue_t* queue)
{
    if ((queue == NULL) || (*queue == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

    k_free(*queue);
    *queue = NULL;
    return CY_RSLT_SUCCESS;
}


//==================================================================================================
// Timers
//==================================================================================================

static void _timer_expiry_fn(struct k_timer* zephyr_timer)
{
    cy_zephyr_timer_t* wrapper =
        CONTAINER_OF(zephyr_timer, cy_zephyr_timer_t, timer);

    if (wrapper->cb != NULL)
    {
        wrapper->cb(wrapper->arg);
    }
}


cy_rslt_t cy_rtos_timer_init(cy_timer_t* timer, cy_timer_trigger_type_t type,
                             cy_timer_callback_t fun, cy_timer_callback_arg_t arg)
{
    if ((timer == NULL) || (fun == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

    cy_zephyr_timer_t* t = (cy_zephyr_timer_t*)k_malloc(sizeof(cy_zephyr_timer_t));
    if (t == NULL)
    {
        return CY_RTOS_NO_MEMORY;
    }

    k_timer_init(&t->timer, _timer_expiry_fn, NULL);
    t->cb        = fun;
    t->arg       = arg;
    t->periodic  = (type == CY_TIMER_TYPE_PERIODIC);
    t->running   = false;
    t->period_ms = 0U;

    *timer = t;
    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_timer_start(cy_timer_t* timer, cy_time_t num_ms)
{
    if ((timer == NULL) || (*timer == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

    cy_zephyr_timer_t* t = *timer;
    t->period_ms         = num_ms;
    t->running           = true;

    k_timeout_t duration = K_MSEC(num_ms);
    k_timeout_t period   = t->periodic ? K_MSEC(num_ms) : K_NO_WAIT;

    k_timer_start(&t->timer, duration, period);
    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_timer_stop(cy_timer_t* timer)
{
    if ((timer == NULL) || (*timer == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

    cy_zephyr_timer_t* t = *timer;
    k_timer_stop(&t->timer);
    t->running = false;
    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_timer_is_running(cy_timer_t* timer, bool* state)
{
    if ((timer == NULL) || (*timer == NULL) || (state == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

    cy_zephyr_timer_t* t = *timer;
    /* k_timer_remaining_get returns 0 if the timer is stopped */
    uint32_t remaining = k_timer_remaining_get(&t->timer);
    *state = (remaining > 0U) || t->running;
    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_timer_deinit(cy_timer_t* timer)
{
    if ((timer == NULL) || (*timer == NULL))
    {
        return CY_RTOS_BAD_PARAM;
    }

    cy_zephyr_timer_t* t = *timer;
    k_timer_stop(&t->timer);
    k_free(t);
    *timer = NULL;
    return CY_RSLT_SUCCESS;
}


//==================================================================================================
// Time
//==================================================================================================

cy_rslt_t cy_rtos_time_get(cy_time_t* tval)
{
    if (tval == NULL)
    {
        return CY_RTOS_BAD_PARAM;
    }

    *tval = (cy_time_t)k_uptime_get_32();
    return CY_RSLT_SUCCESS;
}


cy_rslt_t cy_rtos_delay_milliseconds(cy_time_t num_ms)
{
    k_sleep(K_MSEC(num_ms));
    return CY_RSLT_SUCCESS;
}
