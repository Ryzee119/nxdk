#ifndef _TUSB_OSAL_WIN32_H_
#define _TUSB_OSAL_WIN32_H_

#include <windows.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef HANDLE osal_mutex_def_t;
typedef HANDLE osal_mutex_t;

TU_ATTR_ALWAYS_INLINE static inline void osal_task_delay(uint32_t msec)
{
    Sleep(msec);
}

TU_ATTR_ALWAYS_INLINE static osal_mutex_t osal_mutex_create(osal_mutex_def_t *mdef)
{
    *mdef = CreateMutex(NULL, FALSE, NULL);
    return *mdef;
}

TU_ATTR_ALWAYS_INLINE static bool osal_mutex_lock(osal_mutex_t mutex_hdl, uint32_t msec)
{
    DWORD dwWaitResult = WaitForSingleObject(mutex_hdl, msec);
    return (dwWaitResult == WAIT_OBJECT_0);
}

TU_ATTR_ALWAYS_INLINE static bool osal_mutex_unlock(osal_mutex_t mutex_hdl)
{
    ReleaseMutex(mutex_hdl);
    return true;
}

#include "common/tusb_fifo.h"
typedef struct
{
    tu_fifo_t ff;
    HANDLE semaphore;
} osal_queue_def_t;
typedef osal_queue_def_t *osal_queue_t;

#define OSAL_QUEUE_DEF(_role, _name, _depth, _type)           \
    uint8_t _name##_buf[_depth*sizeof(_type)];                \
    osal_queue_def_t _name = {                                \
        .ff = TU_FIFO_INIT(_name##_buf, _depth, _type, false) \
    }

TU_ATTR_ALWAYS_INLINE static inline osal_queue_t osal_queue_create(osal_queue_def_t* qdef) 
{
    qdef->semaphore = CreateSemaphore(NULL, 0, CFG_TUH_TASK_QUEUE_SZ, NULL);
    tu_fifo_clear(&qdef->ff);
    return (osal_queue_t)qdef;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_queue_receive(osal_queue_t qhdl, void* data, uint32_t msec)
{
    DbgPrint("REC\n");
    DWORD dwWaitResult = WaitForSingleObject(qhdl->semaphore, msec);
    if (dwWaitResult == WAIT_OBJECT_0) {
        return tu_fifo_read(&qhdl->ff, data);
    }
    return false;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_queue_send(osal_queue_t qhdl, void const *data, bool in_isr)
{
    bool r = tu_fifo_write(&qhdl->ff, data);
    ReleaseSemaphore(qhdl->semaphore, 1, NULL);
    return r;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_queue_empty(osal_queue_t qhdl)
{
    return tu_fifo_empty(&qhdl->ff);
}

#ifdef __cplusplus
}
#endif

#endif