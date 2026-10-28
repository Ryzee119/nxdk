/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * WinUSB transfer engine: submits TinyUSB transfers, handles chunking,
 * synchronous/overlapped completion and DMA safe bounce buffers.
 *
 * The OHCI controller on the Xbox performs DMA with physical addresses. A
 * buffer that is not physically contiguous can not be handed to the controller
 * directly (see lib/usb/tinyusb_xbox/hcd_ohci_xbox.c), so transfers whose
 * buffer crosses a page boundary are bounced through a contiguous allocation.
 */

#include <stdlib.h>
#include <string.h>

#include "winusb_internal.h"

#include <xboxkrnl/xboxkrnl.h>

#define WINUSB_CONTROL_TIMEOUT_MS 5000

typedef struct _WINUSB_XFER {
    winusb_device_t *device;
    UCHAR ep_addr;
    BOOL  is_in;
    BOOL  is_control;

    PUCHAR user_buffer;
    PUCHAR dma_buffer;
    ULONG  length;
    ULONG  offset;
    ULONG  chunk;

    tusb_control_request_t setup;

    LPOVERLAPPED overlapped;
    HANDLE event;

    volatile BOOL completed;
    volatile BOOL abandoned;

    DWORD error;
    ULONG actual;
} winusb_xfer_t;

/*--------------------------------------------------------------------
 * DMA BUFFERS
 *--------------------------------------------------------------------*/

static PUCHAR winusb_dma_alloc(ULONG length)
{
    return (PUCHAR)MmAllocateContiguousMemoryEx(length, 0, 0xFFFFFFFF, PAGE_SIZE,
                                                PAGE_READWRITE);
}

static void winusb_dma_free(PVOID buffer)
{
    if (buffer != NULL) {
        MmFreeContiguousMemory(buffer);
    }
}

/* A buffer can be DMA'd in place when it lives inside a single 4 KiB page. */
static BOOL winusb_buffer_is_dma_safe(const void *buffer, ULONG length)
{
    if (buffer == NULL || length == 0) {
        return TRUE;
    }

    return ((((uintptr_t)buffer) & (PAGE_SIZE - 1)) + length) <= PAGE_SIZE;
}

/*--------------------------------------------------------------------
 * COMPLETION
 *--------------------------------------------------------------------*/

static void winusb_xfer_release(winusb_xfer_t *xfer)
{
    if (xfer->event != NULL) {
        CloseHandle(xfer->event);
        xfer->event = NULL;
    }

    if (xfer->dma_buffer != NULL && xfer->dma_buffer != xfer->user_buffer) {
        winusb_dma_free(xfer->dma_buffer);
        xfer->dma_buffer = NULL;
    }
}

static void winusb_xfer_finish(winusb_xfer_t *xfer)
{
    /* Copy received data back when a bounce buffer was used */
    if (xfer->is_in && xfer->error == ERROR_SUCCESS && xfer->actual > 0 &&
        xfer->dma_buffer != NULL && xfer->dma_buffer != xfer->user_buffer) {
        memcpy(xfer->user_buffer, xfer->dma_buffer, xfer->actual);
    }

    xfer->completed = TRUE;

    if (xfer->abandoned) {
        /* Blocking caller timed out and returned; we own a heap context */
        winusb_xfer_release(xfer);
        free(xfer);
        return;
    }

    if (xfer->overlapped != NULL) {
        xfer->overlapped->Internal = (xfer->error == ERROR_SUCCESS) ? 0 : (ULONG_PTR)xfer->error;
        xfer->overlapped->InternalHigh = xfer->actual;
        if (xfer->overlapped->hEvent != NULL) {
            SetEvent(xfer->overlapped->hEvent);

        }
        winusb_xfer_release(xfer);
        free(xfer);
        return;
    }

    /* Blocking transfer: wake up the caller, it owns the context */
    SetEvent(xfer->event);
}

/*--------------------------------------------------------------------
 * BULK / INTERRUPT TRANSFERS
 *--------------------------------------------------------------------*/

static void winusb_xfer_chunk_cb(tuh_xfer_t *transfer);

static BOOL winusb_xfer_next_chunk(winusb_xfer_t *xfer)
{
    const ULONG remaining = xfer->length - xfer->offset;
    const ULONG chunk = (remaining > WINUSB_XFER_CHUNK) ? WINUSB_XFER_CHUNK : remaining;

    xfer->chunk = chunk;

    tuh_xfer_t transfer;
    memset(&transfer, 0, sizeof(transfer));
    transfer.daddr       = xfer->device->daddr;
    transfer.ep_addr     = xfer->ep_addr;
    transfer.buffer      = xfer->dma_buffer + xfer->offset;
    transfer.buflen      = chunk;
    transfer.complete_cb = winusb_xfer_chunk_cb;
    transfer.user_data   = (uintptr_t)xfer;

    if (!tuh_edpt_xfer(&transfer)) {
        xfer->error = ERROR_GEN_FAILURE;
        return FALSE;
    }

    return TRUE;
}

static void winusb_xfer_chunk_cb(tuh_xfer_t *transfer)
{
    winusb_xfer_t *xfer = (winusb_xfer_t *)(uintptr_t)transfer->user_data;
    const ULONG transferred = transfer->actual_len;

    xfer->actual += transferred;

    if (transfer->result != XFER_RESULT_SUCCESS) {
        xfer->error = ERROR_GEN_FAILURE;
        winusb_xfer_finish(xfer);
        return;
    }

    /* Short packet: stop here (equivalent to ALLOW_PARTIAL_READS) */
    if (transferred < xfer->chunk) {
        winusb_xfer_finish(xfer);
        return;
    }

    xfer->offset += transferred;

    if (xfer->offset < xfer->length) {
        if (!winusb_xfer_next_chunk(xfer)) {
            winusb_xfer_finish(xfer);
        }
        return;
    }

    winusb_xfer_finish(xfer);
}

/*--------------------------------------------------------------------
 * CONTROL TRANSFERS
 *--------------------------------------------------------------------*/

static void winusb_xfer_control_cb(tuh_xfer_t *transfer)
{
    winusb_xfer_t *xfer = (winusb_xfer_t *)(uintptr_t)transfer->user_data;

    if (transfer->result != XFER_RESULT_SUCCESS) {
        xfer->error = ERROR_GEN_FAILURE;
    } else {
        xfer->actual = transfer->actual_len;
    }

    winusb_xfer_finish(xfer);
}

static BOOL winusb_xfer_start_control(winusb_xfer_t *xfer)
{
    tuh_xfer_t transfer;
    memset(&transfer, 0, sizeof(transfer));
    transfer.daddr       = xfer->device->daddr;
    transfer.ep_addr     = 0;
    /* note: "setup" and "buflen" share storage in tuh_xfer_t */
    transfer.setup       = &xfer->setup;
    transfer.buffer      = xfer->dma_buffer;
    transfer.complete_cb = winusb_xfer_control_cb;
    transfer.user_data   = (uintptr_t)xfer;

    if (!tuh_control_xfer(&transfer)) {
        xfer->error = ERROR_GEN_FAILURE;
        return FALSE;
    }

    return TRUE;
}

/*--------------------------------------------------------------------
 * PUBLIC ENTRY POINTS
 *--------------------------------------------------------------------*/

static BOOL winusb_do_xfer(winusb_device_t *device, UCHAR ep_addr, PUCHAR buffer, ULONG length,
                           WINUSB_SETUP_PACKET const *setup, LPOVERLAPPED overlapped,
                           ULONG timeout_ms, PULONG transferred)
{
    winusb_xfer_t stack_xfer;
    winusb_xfer_t *xfer = &stack_xfer;
    BOOL heap_xfer = FALSE;
    BOOL started;

    if (transferred != NULL) {
        *transferred = 0;
    }

    if (device == NULL || !device->open) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    if (length > 0 && buffer == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    /* Overlapped and timed transfers need a context that outlives this call */
    if (overlapped != NULL || timeout_ms != 0) {
        xfer = (winusb_xfer_t *)calloc(1, sizeof(*xfer));
        if (xfer == NULL) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        heap_xfer = TRUE;
    } else {
        memset(xfer, 0, sizeof(*xfer));
    }

    xfer->device      = device;
    xfer->ep_addr     = ep_addr;
    xfer->user_buffer = buffer;
    xfer->dma_buffer  = buffer;
    xfer->length      = length;
    xfer->overlapped  = overlapped;
    xfer->is_control  = (setup != NULL);

    if (setup != NULL) {
        xfer->is_in = (setup->RequestType & 0x80u) != 0;
        xfer->setup.bmRequestType = setup->RequestType;
        xfer->setup.bRequest      = setup->Request;
        xfer->setup.wValue        = (uint16_t)setup->Value;
        xfer->setup.wIndex        = (uint16_t)setup->Index;
        xfer->setup.wLength       = (uint16_t)length;
    } else {
        xfer->is_in = (ep_addr & 0x80u) != 0;
    }

    /*
     * The controller needs contiguous physical memory. Buffers that cross a
     * page boundary are copied through a contiguous allocation. Transfers with
     * a timeout always use the bounce buffer: if the caller gives up, the
     * controller may still be writing when the API returns.
     */
    if (length > 0 && ((timeout_ms != 0) || !winusb_buffer_is_dma_safe(buffer, length))) {
        xfer->dma_buffer = winusb_dma_alloc(length);
        if (xfer->dma_buffer == NULL) {
            if (heap_xfer) {
                free(xfer);
            }
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }

        if (!xfer->is_in) {
            memcpy(xfer->dma_buffer, buffer, length);
        }
    }

    if (overlapped != NULL) {
        overlapped->Internal = STATUS_PENDING;
        overlapped->InternalHigh = 0;
    } else {
        xfer->event = CreateEventA(NULL, TRUE, FALSE, NULL);
        if (xfer->event == NULL) {
            winusb_xfer_release(xfer);
            if (heap_xfer) {
                free(xfer);
            }
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
    }

    started = xfer->is_control ? winusb_xfer_start_control(xfer) : winusb_xfer_next_chunk(xfer);

    if (!started) {
        if (overlapped != NULL) {
            overlapped->Internal = (ULONG_PTR)ERROR_GEN_FAILURE;
            if (overlapped->hEvent != NULL) {
                SetEvent(overlapped->hEvent);
            }
        }
        winusb_xfer_release(xfer);
        if (heap_xfer) {
            free(xfer);
        }
        SetLastError(ERROR_GEN_FAILURE);
        return FALSE;
    }

    if (overlapped != NULL) {
        SetLastError(ERROR_IO_PENDING);
        return FALSE;
    }

    /* Blocking transfer */
    {
        DWORD wait = WaitForSingleObject(xfer->event, (timeout_ms == 0) ? INFINITE : timeout_ms);

        if (wait != WAIT_OBJECT_0 && !xfer->completed) {
            if (heap_xfer) {
                /* The completion callback frees the abandoned context */
                xfer->abandoned = TRUE;
                SetLastError(ERROR_SEM_TIMEOUT);
                return FALSE;
            }

            /* Stack context can not be abandoned, wait for completion */
            WaitForSingleObject(xfer->event, INFINITE);
        }
    }

    {
        const BOOL success = (xfer->error == ERROR_SUCCESS);

        if (transferred != NULL) {
            *transferred = xfer->actual;
        }
        SetLastError(xfer->error);

        winusb_xfer_release(xfer);
        if (heap_xfer) {
            free(xfer);
        }

        return success;
    }
}

BOOL winusb_xfer(winusb_interface_t *itf, UCHAR ep_addr, PUCHAR buffer, ULONG length,
                 WINUSB_SETUP_PACKET const *setup, LPOVERLAPPED overlapped,
                 ULONG timeout_ms, PULONG transferred)
{
    if (itf == NULL || itf->device == NULL) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    return winusb_do_xfer(itf->device, ep_addr, buffer, length, setup, overlapped,
                          timeout_ms, transferred);
}

BOOL winusb_control_transfer_sync(winusb_device_t *dev, WINUSB_SETUP_PACKET const *setup,
                                  PUCHAR buffer, ULONG length, PULONG transferred)
{
    return winusb_do_xfer(dev, 0, buffer, length, setup, NULL, WINUSB_CONTROL_TIMEOUT_MS,
                          transferred);
}


