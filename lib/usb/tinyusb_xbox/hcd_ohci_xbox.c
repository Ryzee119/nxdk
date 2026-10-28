/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * TinyUSB host controller port for the OHCI controller in the Xbox MCPX.
 *
 * The Xbox has two OHCI controllers; only USB0 (0xFED00000, IRQ1) is routed to
 * the four front panel game controller ports. It is a standard OHCI 1.0
 * controller, so TinyUSB's portable OHCI driver is used directly. Upstream
 * only ships an NXP backend for that driver; this file provides the Xbox
 * backend (controller base address + interrupt handling) and compiles the
 * portable driver into this translation unit.
 *
 * Physical vs virtual memory
 * -------------------------
 * The OHCI controller DMAs using physical addresses, while nxdk applications
 * run with virtual addresses. TinyUSB's OHCI driver funnels every address
 * handed to the controller through tusb_app_virt_to_phys(), and every
 * descriptor address read back from the controller through
 * tusb_app_phys_to_virt().
 *
 * 1. OHCI descriptors (ohci_data):
 *    The portable driver only calls tusb_app_phys_to_virt() on OHCI descriptor
 *    pointers (HCCA done_head and ED/GTD next links), all of which live in
 *    the static ohci_data structure. Because ohci_data is page-aligned and fits
 *    within a single 4 KiB page, it is locked once at initialization and
 *    translated in O(1) using its cached physical base (ohci_data_phys).
 *
 * 2. Transfer buffers:
 *    An XBE's sections are pageable and not necessarily physically contiguous,
 *    so every buffer visible to the controller must fit inside a single 4 KiB
 *    page (enforced via CFG_TUH_MEM_SECTION) and be locked into RAM while the
 *    controller owns it. A 4 KiB bitmap indexed by Physical Frame Number (PFN)
 *    tracks which physical pages across the 128 MiB RAM space have already
 *    been locked, avoiding duplicate kernel lock calls and fixed-size table
 *    overflows.
 *
 * The x86 front side bus snoops the CPU cache for DMA accesses, so no explicit
 * cache maintenance is needed (CFG_TUH_MEM_DCACHE_ENABLE is 0).
 */

#include "tusb_option.h"

#if CFG_TUH_ENABLED

#include <xboxkrnl/xboxkrnl.h>

/* Xbox USB0 */
#define USB_XBOX_OHCI_BASE    0xFED00000u
#define USB_XBOX_OHCI_RHPORTS 4
#define USB_XBOX_OHCI_IRQ     1

/* HcInterruptEnable/HcInterruptDisable.MIE */
#define USB_XBOX_OHCI_MIE     (1u << 31)

/* Maximum physical pages on Xbox / Chihiro (128 MiB / 4 KiB = 32,768 pages) */
#define USB_XBOX_MAX_PFN      32768u

/*
 * Tell the portable driver that the OHCI IP is the NXP variant (it is the same
 * OHCI core) and suppress its NXP specific backend header, which is replaced
 * by the implementation below.
 */
#define TUP_USBIP_OHCI
#define TUP_USBIP_OHCI_NXP
#define TUP_OHCI_RHPORTS      USB_XBOX_OHCI_RHPORTS
#define TUSB_OHCI_NXP_H

#include "osal/osal.h"
#include "host/hcd.h"
#include "portable/ohci/ohci.h"

#define OHCI_REG ((ohci_registers_t*)USB_XBOX_OHCI_BASE)

static void ohci_phy_init(uint8_t rhport);

/*--------------------------------------------------------------------
 * INTERRUPT HANDLING
 *--------------------------------------------------------------------*/

/*
 * The OHCI interrupt is level triggered and TinyUSB's interrupt handler runs
 * from a DPC, so the ISR masks the controller's Master Interrupt Enable (MIE)
 * until the DPC has acknowledged the pending status bits.
 */
static KINTERRUPT usb_xbox_interrupt;
static KDPC usb_xbox_dpc;
static bool usb_xbox_irq_connected;

static VOID NTAPI usb_xbox_dpc_routine(PKDPC Dpc, PVOID DeferredContext,
                                       PVOID SystemArgument1, PVOID SystemArgument2)
{
  (void)Dpc;
  (void)DeferredContext;
  (void)SystemArgument1;
  (void)SystemArgument2;

  hcd_int_handler(0, true);

  /*
   * Explicitly restore MIE in case hcd_int_handler() returned early because
   * all enabled status bits were already cleared by a prior DPC execution.
   */
  OHCI_REG->interrupt_enable = USB_XBOX_OHCI_MIE;
}

static BOOLEAN NTAPI usb_xbox_isr(PKINTERRUPT Interrupt, PVOID ServiceContext)
{
  (void)Interrupt;
  (void)ServiceContext;

  if ((OHCI_REG->interrupt_status & OHCI_REG->interrupt_enable) == 0) {
    return FALSE;
  }

  OHCI_REG->interrupt_disable = USB_XBOX_OHCI_MIE;
  KeInsertQueueDpc(&usb_xbox_dpc, NULL, NULL);

  return TRUE;
}

void hcd_int_enable(uint8_t rhport)
{
  (void)rhport;

  if (!usb_xbox_irq_connected) {
    KIRQL irql;
    ULONG vector = HalGetInterruptVector(USB_XBOX_OHCI_IRQ, &irql);

    KeInitializeInterrupt(&usb_xbox_interrupt, usb_xbox_isr, NULL, vector, irql,
                          LevelSensitive, FALSE);
    KeInitializeDpc(&usb_xbox_dpc, usb_xbox_dpc_routine, NULL);
    KeConnectInterrupt(&usb_xbox_interrupt);

    usb_xbox_irq_connected = true;
  }

  OHCI_REG->interrupt_enable = USB_XBOX_OHCI_MIE;
}

void hcd_int_disable(uint8_t rhport)
{
  (void)rhport;
  OHCI_REG->interrupt_disable = USB_XBOX_OHCI_MIE;
}

/*--------------------------------------------------------------------
 * PORTABLE OHCI DRIVER
 *--------------------------------------------------------------------*/

/*
 * Both usbh.c and the portable OHCI driver provide weak, unused, cache
 * maintenance stubs. lld does not allow two weak definitions of the same
 * symbol, so the copies in the driver are given a private name. x86 DMA is
 * cache coherent, so they are no-ops anyway (CFG_TUH_MEM_DCACHE_ENABLE is 0).
 */
#define hcd_dcache_clean      usb_xbox_ohci_dcache_clean
#define hcd_dcache_invalidate usb_xbox_ohci_dcache_invalidate
#include "portable/ohci/ohci.c"
#undef hcd_dcache_clean
#undef hcd_dcache_invalidate

/*
 * The driver's descriptors (EDs, GTDs and the HCCA) live in one static
 * structure. It must be 4 KiB page-aligned and fit in a single page so it
 * never crosses a non-contiguous physical page boundary.
 */
TU_VERIFY_STATIC(sizeof(ohci_data_t) <= PAGE_SIZE,
                 "OHCI data structures must fit in a single 4 KiB page");
TU_VERIFY_STATIC(__alignof__(ohci_data) >= PAGE_SIZE,
                 "ohci_data must be 4 KiB page-aligned via CFG_TUH_MEM_SECTION");

/*--------------------------------------------------------------------
 * PHYSICAL <-> VIRTUAL ADDRESS TRANSLATION
 *--------------------------------------------------------------------*/

static uint32_t ohci_data_phys;
static uint32_t usb_xbox_locked_pfn[USB_XBOX_MAX_PFN / 32];

static void usb_xbox_lock_ohci_data(void)
{
  if (ohci_data_phys == 0) {
    MmLockUnlockBufferPages(&ohci_data, sizeof(ohci_data_t), FALSE);
    ohci_data_phys = (uint32_t)MmGetPhysicalAddress(&ohci_data);
  }
}

/* The Xbox OHCI controller needs no PHY setup; ensure ohci_data is locked. */
static void ohci_phy_init(uint8_t rhport)
{
  (void)rhport;
  usb_xbox_lock_ohci_data();
}

void* tusb_app_virt_to_phys(void* virt_addr)
{
  const uint32_t addr = (uint32_t)virt_addr;

  if (addr == 0) {
    return NULL;
  }

  /*
   * Fast path 1: Descriptors inside ohci_data.
   * Handles 100% of calls made from DPC context (done_queue_isr) as well as
   * ED/GTD linkage. Lazy-initialized because hcd_init() translates
   * &ohci_data.hcca before calling ohci_phy_init().
   */
  const uint32_t ohci_offset = addr - (uint32_t)&ohci_data;
  if (ohci_offset < sizeof(ohci_data_t)) {
    if (__builtin_expect(ohci_data_phys == 0, 0)) {
      usb_xbox_lock_ohci_data();
    }
    return (void*)(ohci_data_phys + ohci_offset);
  }

  /*
   * Fast path 2: Kernel contiguous / direct-mapped RAM (0x80000000..0x87FFFFFF).
   * Pages in the kernel's direct physical map are already non-pageable.
   */
  if (addr >= 0x80000000u && addr < 0x88000000u) {
    return (void*)(addr & 0x0FFFFFFFu);
  }

  /*
   * Fast path 3: Check if the backing physical page is already pinned in our
   * PFN bitmap. Once locked, a page's physical frame cannot be relocated.
   */
  uint32_t phys = (uint32_t)MmGetPhysicalAddress(virt_addr);
  uint32_t pfn  = phys >> 12;

  if (phys != 0 && pfn < USB_XBOX_MAX_PFN &&
      (usb_xbox_locked_pfn[pfn >> 5] & (1u << (pfn & 31)))) {
    return (void*)phys;
  }

  /*
   * Cold path: Lock the page into RAM first, then re-read its physical address
   * in case locking faulted it in or relocated it.
   */
  const uint32_t virt_page = addr & ~(uint32_t)(PAGE_SIZE - 1);
  MmLockUnlockBufferPages((PVOID)virt_page, PAGE_SIZE, FALSE);

  phys = (uint32_t)MmGetPhysicalAddress(virt_addr);
  pfn  = phys >> 12;

  if (pfn < USB_XBOX_MAX_PFN) {
    const uint32_t mask = 1u << (pfn & 31);
    const uint32_t prev = __sync_fetch_and_or(&usb_xbox_locked_pfn[pfn >> 5], mask);
    if (prev & mask) {
      /* Another context locked this page concurrently; undo duplicate lock */
      MmLockUnlockBufferPages((PVOID)virt_page, PAGE_SIZE, TRUE);
    }
  }

  return (void*)phys;
}

void* tusb_app_phys_to_virt(void* phys_addr)
{
  const uint32_t addr = (uint32_t)phys_addr;

  if (addr == 0) {
    return NULL;
  }

  /* All OHCI descriptor pointers (HCCA done_head, ED/GTD links) live in ohci_data */
  const uint32_t ohci_offset = addr - ohci_data_phys;
  if (ohci_offset < sizeof(ohci_data_t)) {
    return (uint8_t*)&ohci_data + ohci_offset;
  }

  /* Fallback to the kernel's uncached direct map of RAM */
  return (void*)(0x80000000u + addr);
}

#endif /* CFG_TUH_ENABLED */
