/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * TinyUSB custom OSAL implementation for the Original Xbox (nxdk / xboxkrnl).
 */

#ifndef TUSB_OS_CUSTOM_H_
#define TUSB_OS_CUSTOM_H_

#include <xboxkrnl/xboxkrnl.h>
#include "common/tusb_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*--------------------------------------------------------------------
 * TASK DELAY
 *--------------------------------------------------------------------*/

TU_ATTR_ALWAYS_INLINE static inline void osal_task_delay(uint32_t msec)
{
  LARGE_INTEGER interval;
  interval.QuadPart = -((LONGLONG)msec * 10000);
  KeDelayExecutionThread(KernelMode, FALSE, &interval);
}

/*--------------------------------------------------------------------
 * TIME / TASK
 *--------------------------------------------------------------------*/

TU_ATTR_ALWAYS_INLINE static inline uint32_t osal_time_millis(void)
{
  LARGE_INTEGER time;
  KeQuerySystemTime(&time);
  return (uint32_t)(time.QuadPart / 10000);
}

typedef PKTHREAD osal_task_handle_t;

TU_ATTR_ALWAYS_INLINE static inline osal_task_handle_t osal_task_get_current_handle(void)
{
  return KeGetCurrentThread();
}

/*--------------------------------------------------------------------
 * SPINLOCK
 *--------------------------------------------------------------------*/

/*
 * The Xbox has a single CPU: a critical section only has to keep the USB
 * interrupt (and its DPC) from running. The callback supplied by the host
 * stack masks/unmasks the controller interrupt.
 */
typedef struct {
  void (*interrupt_set)(bool enabled);
  uint32_t nested_count;
} osal_spinlock_t;

#define OSAL_SPINLOCK_DEF(_name, _int_set) \
  osal_spinlock_t _name = { .interrupt_set = _int_set, .nested_count = 0 }

TU_ATTR_ALWAYS_INLINE static inline void osal_spin_init(osal_spinlock_t *ctx)
{
  ctx->nested_count = 0;
}

TU_ATTR_ALWAYS_INLINE static inline void osal_spin_deinit(osal_spinlock_t *ctx)
{
  (void)ctx;
}

TU_ATTR_ALWAYS_INLINE static inline void osal_spin_lock(osal_spinlock_t *ctx, bool in_isr)
{
  if (!in_isr && ctx->nested_count == 0 && ctx->interrupt_set) {
    ctx->interrupt_set(false);
  }

  ctx->nested_count++;
}

TU_ATTR_ALWAYS_INLINE static inline void osal_spin_unlock(osal_spinlock_t *ctx, bool in_isr)
{
  if (ctx->nested_count == 0) {
    return;
  }

  ctx->nested_count--;

  if (!in_isr && ctx->nested_count == 0 && ctx->interrupt_set) {
    ctx->interrupt_set(true);
  }
}

/*--------------------------------------------------------------------
 * SEMAPHORE
 *--------------------------------------------------------------------*/

typedef KSEMAPHORE  osal_semaphore_def_t;
typedef PKSEMAPHORE osal_semaphore_t;

TU_ATTR_ALWAYS_INLINE static inline osal_semaphore_t osal_semaphore_create(osal_semaphore_def_t *semdef)
{
  KeInitializeSemaphore(semdef, 0, 0x7FFFFFFF);
  return semdef;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_semaphore_delete(osal_semaphore_t sem_hdl)
{
  (void)sem_hdl;
  return true;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_semaphore_post(osal_semaphore_t sem_hdl, bool in_isr)
{
  (void)in_isr;
  KeReleaseSemaphore(sem_hdl, IO_NO_INCREMENT, 1, FALSE);
  return true;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_semaphore_wait(osal_semaphore_t sem_hdl, uint32_t msec)
{
  LARGE_INTEGER timeout;
  PLARGE_INTEGER ptimeout = NULL;

  if (msec != OSAL_TIMEOUT_WAIT_FOREVER) {
    timeout.QuadPart = -((LONGLONG)msec * 10000);
    ptimeout = &timeout;
  }

  NTSTATUS status = KeWaitForSingleObject(sem_hdl, Executive, KernelMode, FALSE, ptimeout);
  return status == STATUS_SUCCESS;
}

TU_ATTR_ALWAYS_INLINE static inline void osal_semaphore_reset(osal_semaphore_t sem_hdl)
{
  KIRQL old_irql = KfRaiseIrql(DISPATCH_LEVEL);
  sem_hdl->Header.SignalState = 0;
  KfLowerIrql(old_irql);
}

/*--------------------------------------------------------------------
 * MUTEX (RTL_CRITICAL_SECTION)
 *--------------------------------------------------------------------*/

typedef RTL_CRITICAL_SECTION  osal_mutex_def_t;
typedef PRTL_CRITICAL_SECTION osal_mutex_t;

TU_ATTR_ALWAYS_INLINE static inline osal_mutex_t osal_mutex_create(osal_mutex_def_t *mdef)
{
  RtlInitializeCriticalSection(mdef);
  return mdef;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_mutex_delete(osal_mutex_t mutex_hdl)
{
  (void)mutex_hdl;
  return true;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_mutex_lock(osal_mutex_t mutex_hdl, uint32_t msec)
{
  (void)msec;
  RtlEnterCriticalSection(mutex_hdl);
  return true;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_mutex_unlock(osal_mutex_t mutex_hdl)
{
  RtlLeaveCriticalSection(mutex_hdl);
  return true;
}

/*--------------------------------------------------------------------
 * QUEUE
 *--------------------------------------------------------------------*/

typedef struct {
  KSEMAPHORE sem;
  uint8_t   *buffer;
  uint16_t   depth;
  uint16_t   item_size;
  uint16_t   wr_idx;
  uint16_t   rd_idx;
  uint16_t   count;
} osal_queue_def_t;

typedef osal_queue_def_t *osal_queue_t;

#define OSAL_QUEUE_DEF(_int_set, _name, _depth, _type) \
  static _type _name##_buf[_depth];                    \
  osal_queue_def_t _name = {                           \
    .buffer    = (uint8_t *)_name##_buf,               \
    .depth     = (_depth),                             \
    .item_size = sizeof(_type),                        \
  }

TU_ATTR_ALWAYS_INLINE static inline osal_queue_t osal_queue_create(osal_queue_def_t *qdef)
{
  qdef->wr_idx = 0;
  qdef->rd_idx = 0;
  qdef->count  = 0;
  KeInitializeSemaphore(&qdef->sem, 0, qdef->depth);
  return qdef;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_queue_delete(osal_queue_t qhdl)
{
  (void)qhdl;
  return true;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_queue_receive(osal_queue_t qhdl, void *data, uint32_t msec)
{
  if (!osal_semaphore_wait(&qhdl->sem, msec)) {
    return false;
  }

  KIRQL old_irql = KfRaiseIrql(DISPATCH_LEVEL);
  memcpy(data, qhdl->buffer + (qhdl->rd_idx * qhdl->item_size), qhdl->item_size);
  qhdl->rd_idx = (uint16_t)((qhdl->rd_idx + 1) % qhdl->depth);
  qhdl->count--;
  KfLowerIrql(old_irql);

  return true;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_queue_send(osal_queue_t qhdl, void const *data, bool in_isr)
{
  (void)in_isr;

  KIRQL old_irql = KfRaiseIrql(DISPATCH_LEVEL);
  if (qhdl->count >= qhdl->depth) {
    KfLowerIrql(old_irql);
    return false;
  }

  memcpy(qhdl->buffer + (qhdl->wr_idx * qhdl->item_size), data, qhdl->item_size);
  qhdl->wr_idx = (uint16_t)((qhdl->wr_idx + 1) % qhdl->depth);
  qhdl->count++;
  KfLowerIrql(old_irql);

  KeReleaseSemaphore(&qhdl->sem, IO_NO_INCREMENT, 1, FALSE);
  return true;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_queue_empty(osal_queue_t qhdl)
{
  return qhdl->count == 0;
}

#ifdef __cplusplus
}
#endif

#endif /* TUSB_OS_CUSTOM_H_ */
