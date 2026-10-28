/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * TinyUSB configuration for nxdk.
 *
 * The Xbox is not an MCU supported by upstream TinyUSB: the host controller
 * (portable/ohci) and the OSAL are provided by the nxdk port in this directory.
 */

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

/*--------------------------------------------------------------------
 * COMMON CONFIGURATION
 *--------------------------------------------------------------------*/

/* No MCU specific code is used, the HCD and OSAL are implemented by nxdk */
#define CFG_TUSB_MCU                OPT_MCU_NONE

/* OSAL implemented on top of xboxkrnl (see tusb_os_custom.h) */
#define CFG_TUSB_OS                 OPT_OS_CUSTOM

/*
 * TinyUSB log output goes through nxdk_tu_printf() (debug_xbox.c), which
 * forwards it to the Xbox kernel's DbgPrint().
 * Level 3 also prints the host stack's informational messages.
 */
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG              0
#define CFG_TUSB_DEBUG_PRINTF       nxdk_tu_printf
#endif

/*--------------------------------------------------------------------
 * HOST CONFIGURATION
 *--------------------------------------------------------------------*/

/*
 * Xbox USB0 is the only OHCI controller wired to the game controller ports.
 * It is a USB 1.1 (full speed) controller with 4 root hub ports.
 */
#define CFG_TUSB_RHPORT0_MODE       OPT_MODE_HOST

#define CFG_TUH_DEVICE_MAX          8
#define CFG_TUH_ENDPOINT_MAX        8
#define CFG_TUH_ENUMERATION_BUFSIZE 256

/* Root hub (4 ports) + support for the v1.0 USB daughterboard + 4 external hubs */
#define CFG_TUH_HUB                 5
#define CFG_TUH_TASK_QUEUE_SZ       16

/*
 * The WinUSB compatibility layer (lib/usb/winusb) submits bulk/interrupt
 * transfers with a completion callback.
 */
#define CFG_TUH_API_EDPT_XFER       1

/*
 * The OHCI controller DMAs to physical addresses. On the Xbox a buffer is only
 * guaranteed to be physically contiguous within a single 4 KiB page, so every
 * DMA-visible object is page aligned. The OHCI data structures are checked to
 * fit in one page in hcd_ohci_xbox.c. User supplied transfer buffers must also
 * not cross a page boundary (or must be physically contiguous).
 *
 * Note that CFG_TUH_MEM_SECTION is applied to each individual host stack object
 * so that every one of them starts on its own page.
 */
#define CFG_TUH_MEM_SECTION         __attribute__((aligned(4096)))
#define CFG_TUH_MEM_ALIGN           __attribute__((aligned(4096)))

/* x86 DMA is cache coherent, no explicit cache maintenance is required */
#define CFG_TUH_MEM_DCACHE_ENABLE   0

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
