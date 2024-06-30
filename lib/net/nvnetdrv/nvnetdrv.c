// SPDX-License-Identifier: MIT

// SPDX-FileCopyrightText: 2022 Stefan Schmidt
// SPDX-FileCopyrightText: 2022 Ryan Wendland
// SPDX-FileCopyrightText: 2024 Dustin Holden

#include "nvnetdrv.h"
#include "nvnetdrv_regs.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <xboxkrnl/xboxkrnl.h>
#include <lwip/pbuf.h>

#define NVNET_RX_EMPTY (NULL)

struct __attribute__((packed)) descriptor_t
{
    uint32_t paddr;
    // TODO: Is it safe to set these seperately?
    uint16_t length;
    uint16_t flags;
};

#ifdef NVNETDRV_ENABLE_STATS
struct nvnetdrv_stats_t
{
    uint32_t rx_interrupts;
    uint32_t rx_extraByteErrors;
    uint32_t tx_interrupts;
    uint32_t phy_interrupts;
    uint32_t rx_receivedPackets;
    uint32_t rx_framingError;
    uint32_t rx_overflowError;
    uint32_t rx_crcError;
    uint32_t rx_error4;
    uint32_t rx_error3;
    uint32_t rx_error2;
    uint32_t rx_error1;
    uint32_t rx_missedFrameError;
};
static struct nvnetdrv_stats_t nvnetdrv_stats;
#define INC_STAT(statname, val) do {nvnetdrv_stats.statname += (val);} while(0);
#else
#define INC_STAT(statname, val)
#endif

// FIXME
#define BASE ((void *)0xFEF00000)
#define reg32(offset) (*((volatile uint32_t *)((uintptr_t)BASE + (offset))))

// Manage NIC
static atomic_bool g_running = false;
static uint8_t g_ethAddr[6];
static uint32_t g_linkSpeed;
static ULONG g_irq;
static KIRQL g_irql;
static KDPC g_dpcObj;
static KINTERRUPT g_interrupt;
static HANDLE g_irqThread;
static KEVENT g_irqEvent;

// Manage RX rings
static struct descriptor_t *g_rxRing;
static size_t g_rxRingSize;
static size_t g_rxRingHead;
static atomic_size_t g_rxRingTail;
static uint8_t *g_rxRingUserBuffers;
static uint32_t g_rxRingBufferVtoP;

// TX packet queue
static LIST_ENTRY nvnetdrv_tx_queue;

// Manage TX rings
// We using a circular buffer for the TX ring, but we're also tracking the
// current busy and free descriptors to make it easier/quicker (perf) to manage.
static bool                  g_tx_full;
static size_t                g_tx_busy;
static size_t                g_tx_size;
static struct descriptor_t * g_tx_desc_first;
static struct descriptor_t * g_tx_desc_last;
static struct descriptor_t * g_tx_desc_busy;
static struct descriptor_t * g_tx_desc_free;

// Manage RX buffer callbacks to user network stack
static nvnetdrv_rx_callback_t g_rxCallback;

// Time constants used in nvnetdrv
#define NO_SLEEP &(LARGE_INTEGER){.QuadPart = 0}
#define TEN_MICRO &(LARGE_INTEGER){.QuadPart = -100}
#define FIFTY_MICRO &(LARGE_INTEGER){.QuadPart = -500}

static inline uint32_t nvnetdrv_rx_ptov (uint32_t phys_address)
{
    return (phys_address == 0) ? 0 : (phys_address + g_rxRingBufferVtoP);
}

static inline uint32_t nvnetdrv_rx_vtop (uint32_t virt_address)
{
    return (virt_address == 0) ? 0 : (virt_address - g_rxRingBufferVtoP);
}

static inline void nvnetdrv_irq_disable (void)
{
    reg32(NvRegIrqMask) = 0;
}

static inline void nvnetdrv_irq_enable (void)
{
    reg32(NvRegIrqMask) = NVREG_IRQMASK_THROUGHPUT;
}

static BOOLEAN NTAPI nvnetdrv_isr (PKINTERRUPT Interrupt, PVOID ServiceContext)
{
    nvnetdrv_irq_disable();
    KeInsertQueueDpc(&g_dpcObj, NULL, NULL);
    return TRUE;
}

static void NTAPI nvnetdrv_dpc (PKDPC Dpc, PVOID DeferredContext, PVOID arg1, PVOID arg2)
{
    KeSetEvent(&g_irqEvent, IO_NETWORK_INCREMENT, FALSE);
}

static void nvnetdrv_handle_rx_irq (void)
{
    while (g_rxRing[g_rxRingHead].paddr != NVNET_RX_EMPTY) {
        volatile struct descriptor_t *rx_packet = &g_rxRing[g_rxRingHead];
        uint16_t flags = rx_packet->flags;

        if (flags & NV_RX_AVAIL) {
            // Reached a descriptor that still belongs to the NIC
            break;
        }

        if ((flags & NV_RX_DESCRIPTORVALID) == 0) {
            goto release_packet;
        }

        uint16_t packet_length = rx_packet->length;

        if (flags & NV_RX_ERROR) {
            // Framing errors are soft errors. Accept if this is the only error bit
            if ((flags & NV_RX_ERROR_MASK) == NV_RX_FRAMINGERR) {
                INC_STAT(rx_framingError, 1);
                if (flags & NV_RX_SUBTRACT1) {
                    INC_STAT(rx_extraByteErrors, 1);
                    if (packet_length > 0) {
                        packet_length--;
                    }
                }
            } else {
                if (flags & NV_RX_MISSEDFRAME) INC_STAT(rx_missedFrameError, 1);
                if (flags & NV_RX_OVERFLOW) INC_STAT(rx_overflowError, 1);
                if (flags & NV_RX_CRCERR) INC_STAT(rx_crcError, 1);
                if (flags & NV_RX_ERROR4) INC_STAT(rx_error4, 1);
                if (flags & NV_RX_ERROR3) INC_STAT(rx_error3, 1);
                if (flags & NV_RX_ERROR2) INC_STAT(rx_error2, 1);
                if (flags & NV_RX_ERROR1) INC_STAT(rx_error1, 1);
                goto release_packet;
            }
        }

        INC_STAT(rx_receivedPackets, 1);

        void *paddr = (void *)nvnetdrv_rx_ptov(rx_packet->paddr);
        rx_packet->paddr = NVNET_RX_EMPTY;
        g_rxCallback(paddr, packet_length);
        goto next_packet;

    // On error drop packet and release buffer
    release_packet:
        nvnetdrv_rx_release((void *)nvnetdrv_rx_ptov(rx_packet->paddr));
        // Fallthrough
    next_packet:
        g_rxRingHead = (g_rxRingHead + 1) % g_rxRingSize;
    }
    INC_STAT(rx_interrupts, 1);
}

static void nvnetdrv_handle_tx_irq (void)
{
    while (1) {
        // If the transmit queue is empty, we're done.
        if(IsListEmpty(&nvnetdrv_tx_queue))
            break;

        // Get the first packet in the transmit queue.
        // NOTE: We're not removing the packet from the queue yet since we need
        // to check if it was sent.
        struct pbuf *p = (struct pbuf *)CONTAINING_RECORD(nvnetdrv_tx_queue.Flink, struct pbuf, ListEntry);

        // Get the packet's descriptor.
        struct descriptor_t *tx_packet = g_tx_desc_busy;

        // Check if the descriptor is split.
        if (p->flags & PBUF_FLAG_IS_DESCRIPTOR_SPLIT) {
            // On a split descriptor, the first descriptor should not be marked as the last packet.
            assert((tx_packet->flags & NV_TX_LASTPACKET) == 0);

            // Move to the next descriptor in the chain.
            tx_packet = (tx_packet == g_tx_desc_last ? g_tx_desc_first : (tx_packet + 1));
        }

        // Packet should always be marked as last packet in the chain.
        assert(tx_packet->flags & NV_TX_LASTPACKET);

        // Check if we've reached a descriptor that wasn't processed by hw yet
        if (tx_packet->flags & NV_TX_VALID)
            break;

        // Remove the packet from the transmit queue since it's been sent.
        RemoveHeadList(&nvnetdrv_tx_queue);

        // Update the busy descriptor pointer to the next descriptor in the chain.
        g_tx_desc_busy = (tx_packet == g_tx_desc_last ? g_tx_desc_first : (tx_packet + 1));

        // Update the number of busy descriptors.
        // If this is a split descriptor, we need to decrement the count by 2.
        g_tx_busy -= (p->flags & PBUF_FLAG_IS_DESCRIPTOR_SPLIT) ? 2 : 1;

        // Buffers get locked before sending and unlocked after sending
        MmLockUnlockBufferPages(p->payload, p->len, TRUE);

        // Clear the split flag since we're done with this descriptor.
        // TODO: This probably isn't needed since we're removing the packet from the queue.
        p->flags &= ~PBUF_FLAG_IS_DESCRIPTOR_SPLIT;

        // If registered, call the users tx complete callback funciton.
        if (p->Complete) {
            p->Complete(p);
        }
    }

    // If the queue was previous full, then we need to check if can queue up more packets.
    if (g_tx_full && g_tx_busy < g_tx_size) {
        g_tx_full = FALSE;

        nvnetif_tx_push(NULL, NULL, NULL, NULL);
    }

    // TODO: Re-implement INC_STAT(tx_interrupts, 1);
}

static void nvnetdrv_handle_mii_irq (uint32_t miiStatus, bool init)
{
    uint32_t adapterControl = reg32(NvRegAdapterControl);
    uint32_t linkState = PhyGetLinkState(!init);

    if (miiStatus & NVREG_MIISTAT_LINKCHANGE) {
        nvnetdrv_stop_txrx();
    }

    if (linkState & XNET_ETHERNET_LINK_10MBPS) {
        g_linkSpeed = NVREG_LINKSPEED_10MBPS;
    } else {
        g_linkSpeed = NVREG_LINKSPEED_100MBPS;
    }

    if (linkState & XNET_ETHERNET_LINK_FULL_DUPLEX) {
        reg32(NvRegDuplexMode) &= NVREG_DUPLEX_MODE_FDMASK;
    } else {
        reg32(NvRegDuplexMode) |= NVREG_DUPLEX_MODE_HDFLAG;
    }

    if (miiStatus & NVREG_MIISTAT_LINKCHANGE) {
        nvnetdrv_start_txrx();
    }

    INC_STAT(phy_interrupts, 1);
}

static void nvnetdrv_handle_irq (void)
{
    while (true) {
        uint32_t irq = reg32(NvRegIrqStatus);
        uint32_t mii = reg32(NvRegMIIStatus);

        // No interrupts left to handle. Leave
        if (!irq) break;

        // We need to handle MII irq before acknowledging it to prevent link state IRQ occurring
        // during polling of the link state register
        if (irq & NVREG_IRQ_LINK) {
            nvnetdrv_handle_mii_irq(mii, false);
        }

        // Acknowledge interrupts
        reg32(NvRegMIIStatus) = mii;
        reg32(NvRegIrqStatus) = irq;

        // Handle RX interrupts
        if (irq & NVREG_IRQ_RX_ALL) {
            nvnetdrv_handle_rx_irq();
        }

        // Handle TX interrupts
        // NOTE: We need to always handle TX interrupts just in case we have
        // more packets in that queue that need to be pushed to the NIC.
        nvnetdrv_handle_tx_irq();

        if (irq & NVREG_IRQ_RX_NOBUF) {
            reg32(NvRegTxRxControl) = NVREG_TXRXCTL_GET;
        }
    }
}

static void NTAPI irq_thread (PKSTART_ROUTINE StartRoutine, PVOID StartContext)
{
    (void)StartRoutine;
    (void)StartContext;

    while (true) {
        KeWaitForSingleObject(&g_irqEvent, Executive, KernelMode, FALSE, NULL);
        if (!g_running) break;

        nvnetdrv_handle_irq();
        nvnetdrv_irq_enable();
    }

    PsTerminateSystemThread(0);
}

bool nvnetdrv_tx_ready (void)
{
    bool ready = g_tx_busy < g_tx_size;

    if (!ready)
        g_tx_full = true;

    return ready;
}

const uint8_t *nvnetdrv_get_ethernet_addr (void)
{
    return g_ethAddr;
}

int nvnetdrv_init (size_t rx_buffer_count, nvnetdrv_rx_callback_t rx_callback, size_t tx_queue_size)
{
    assert(!g_running);
    assert(rx_callback);
    assert(rx_buffer_count > 1);

    g_rxCallback = rx_callback;
    g_rxRingSize = rx_buffer_count;
    g_tx_size    = tx_queue_size;

    // Initialize our packet queue for TX packets
    InitializeListHead(&nvnetdrv_tx_queue);

    // Get Mac Address from EEPROM
    ULONG type;
    NTSTATUS status = ExQueryNonVolatileSetting(XC_FACTORY_ETHERNET_ADDR, &type, &g_ethAddr, 6, NULL);
    if (!NT_SUCCESS(status)) {
        return NVNET_NO_MAC;
    }

    // Allocate memory for TX and RX ring descriptors.
    void *descriptors = MmAllocateContiguousMemoryEx((g_rxRingSize + g_tx_size) * sizeof(struct descriptor_t), 0,
                                                     0xFFFFFFFF, 0, PAGE_READWRITE);
    if (!descriptors) {
        return NVNET_NO_MEM;
    }

    // Allocate memory for RX buffers. TX buffers are supplied by the user.
    g_rxRingUserBuffers =
        MmAllocateContiguousMemoryEx(g_rxRingSize * NVNET_RX_BUFF_LEN, 0, 0xFFFFFFFF, 0, PAGE_READWRITE);
    if (!g_rxRingUserBuffers) {
        MmFreeContiguousMemory(descriptors);
        return NVNET_NO_MEM;
    }

    RtlZeroMemory(descriptors, (g_rxRingSize + g_tx_size) * sizeof(struct descriptor_t));

    // Reset NIC. MSDash delays 10us here
    nvnetdrv_stop_txrx();
    reg32(NvRegTxRxControl) = NVREG_TXRXCTL_RESET;
    KeDelayExecutionThread(KernelMode, FALSE, TEN_MICRO);
    reg32(NvRegTxRxControl) = 0;
    KeDelayExecutionThread(KernelMode, FALSE, TEN_MICRO);
    reg32(NvRegMIIMask) = 0;
    reg32(NvRegIrqMask) = 0;
    reg32(NvRegWakeUpFlags) = 0;
    reg32(NvRegPollingControl) = 0;
    reg32(NvRegTxRingPhysAddr) = 0;
    reg32(NvRegRxRingPhysAddr) = 0;
    reg32(NvRegTransmitPoll) = 0;
    reg32(NvRegLinkSpeed) = 0;

    // Acknowledge any existing interrupts status bits
    reg32(NvRegTransmitterStatus) = reg32(NvRegTransmitterStatus);
    reg32(NvRegReceiverStatus) = reg32(NvRegReceiverStatus);
    reg32(NvRegIrqStatus) = reg32(NvRegIrqStatus);
    reg32(NvRegMIIStatus) = reg32(NvRegMIIStatus);

    // Reset local rx ring tracking variables
    g_rxRingHead = 0;
    g_rxRingTail = 0;

    // Setup the RX ring descriptor pointers
    g_rxRing = (struct descriptor_t *)descriptors;

    // Setup TX ring descriptor pointers
    g_tx_desc_first = (struct descriptor_t *)descriptors + g_rxRingSize;
    g_tx_desc_busy  = g_tx_desc_first;
    g_tx_desc_free  = g_tx_desc_first;
    g_tx_desc_last  = g_tx_desc_first + g_tx_size - 1;

    // HACK: Reduce the number of TX descriptors by 1 so that we always have a
    // spare descriptor in case we need to split a packet.
    g_tx_size -= 1;

    // Remember the offset between virtual and physical address
    g_rxRingBufferVtoP = ((uint32_t)g_rxRingUserBuffers) - (uint32_t)MmGetPhysicalAddress(g_rxRingUserBuffers);

    // Setup some fixed registers for the NIC
    reg32(NvRegMacAddrA) = (g_ethAddr[0] << 0) | (g_ethAddr[1] << 8) | (g_ethAddr[2] << 16) | (g_ethAddr[3] << 24);
    reg32(NvRegMacAddrB) = (g_ethAddr[4] << 0) | (g_ethAddr[5] << 8);
    reg32(NvRegMulticastAddrA) = NVREG_MCASTMASKA_NONE;
    reg32(NvRegMulticastAddrB) = NVREG_MCASTMASKB_NONE;
    reg32(NvRegMulticastMaskA) = NVREG_MCASTMASKA_NONE;
    reg32(NvRegMulticastMaskB) = NVREG_MCASTMASKB_NONE;
    reg32(NvRegOffloadConfig) = NVREG_OFFLOAD_NORMAL;
    reg32(NvRegPacketFilterFlags) = NVREG_PFF_ALWAYS_MYADDR;
    reg32(NvRegDuplexMode) = NVREG_DUPLEX_MODE_FORCEH;

    // Pseudo random slot time to minimise collisions
    reg32(NvRegSlotTime) = ((rand() % 0xFF) & NVREG_SLOTTIME_MASK) | NVREG_SLOTTIME_10_100_FULL;
    reg32(NvRegTxDeferral) = NVREG_TX_DEFERRAL_RGMII_10_100;
    reg32(NvRegRxDeferral) = NVREG_RX_DEFERRAL_DEFAULT;

    // Point the NIC to our TX and RX ring buffers. NIC expects Ring size as size-1.
    reg32(NvRegTxRingPhysAddr) = MmGetPhysicalAddress((void *)g_tx_desc_first);
    reg32(NvRegRxRingPhysAddr) = MmGetPhysicalAddress((void *)g_rxRing);
    reg32(NvRegRingSizes) = ((g_rxRingSize - 1) << NVREG_RINGSZ_RXSHIFT) | ((g_tx_size) << NVREG_RINGSZ_TXSHIFT);

    // MS Dash does this and sets up both these registers with 0x300010)
    reg32(NvRegUnknownSetupReg7) = NVREG_UNKSETUP7_VAL1;  // RxWatermark?
    reg32(NvRegTxWatermark) = NVREG_UNKSETUP7_VAL1;

    // Prepare for Phy Init
    reg32(NvRegAdapterControl) = (1 << NVREG_ADAPTCTL_PHYSHIFT) | NVREG_ADAPTCTL_PHYVALID;
    reg32(NvRegMIISpeed) = NVREG_MIISPEED_BIT8 | NVREG_MIIDELAY;
    reg32(NvRegMIIMask) = NVREG_MII_LINKCHANGE;
    KeDelayExecutionThread(KernelMode, FALSE, FIFTY_MICRO);

    // Initialise the transceiver
    if (PhyInitialize(FALSE, NULL) != STATUS_SUCCESS) {
        MmFreeContiguousMemory(descriptors);
        MmFreeContiguousMemory(g_rxRingUserBuffers);
        return NVNET_PHY_ERR;
    }

    // Short delay to allow the phy to startup. MSDash delays 50us
    reg32(NvRegAdapterControl) |= NVREG_ADAPTCTL_RUNNING;
    KeDelayExecutionThread(KernelMode, FALSE, FIFTY_MICRO);

    // The NIC hardware IRQ queues a DPC. The DPC then sets g_irqEvent.
    // g_irqEvent is monitored by irqthread to handle the IRQ
    g_irq = HalGetInterruptVector(4, &g_irql);
    KeInitializeInterrupt(&g_interrupt, &nvnetdrv_isr, NULL, g_irq, g_irql, LevelSensitive, TRUE);
    KeInitializeDpc(&g_dpcObj, nvnetdrv_dpc, NULL);
    KeInitializeEvent(&g_irqEvent, SynchronizationEvent, FALSE);

    // Fill up our RX ring with buffers
    for (uint32_t i = 0; i < g_rxRingSize; i++) {
        nvnetdrv_rx_release(g_rxRingUserBuffers + (i * NVNET_RX_BUFF_LEN));
    }

    // Get link speed settings from Phy
    nvnetdrv_handle_mii_irq(0, true);

    // Start getting and sending data
    nvnetdrv_start_txrx();

    reg32(NvRegMIIMask) = NVREG_MII_LINKCHANGE;
    nvnetdrv_irq_enable();

    // Set running flag. This needs to happen before we connect the irq and start threads.
    bool prev_value = atomic_exchange(&g_running, true);
    assert(!prev_value);

    // Create a minimal stack, no TLS thread to handle NIC events
    PsCreateSystemThreadEx(&g_irqThread, 0, 4096, 0, NULL, NULL, NULL, FALSE, FALSE, irq_thread);

    // Connect the NIC IRQ to the ISR
    status = KeConnectInterrupt(&g_interrupt);
    if (!NT_SUCCESS(status)) {
        nvnetdrv_stop();
        return NVNET_SYS_ERR;
    }
    return NVNET_OK;
}

void nvnetdrv_stop (void)
{
    assert(g_running);

    KeDisconnectInterrupt(&g_interrupt);

    // Disable DMA and wait for it to idle, re-checking every 50 microseconds
    reg32(NvRegTxRxControl) = NVREG_TXRXCTL_DISABLE;
    for (int i = 0; i < 10000; i++) {
        if (reg32(NvRegTxRxControl) & NVREG_TXRXCTL_IDLE) {
            break;
        }
        KeDelayExecutionThread(KernelMode, FALSE, FIFTY_MICRO);
    }

    // Stop NIC processing rings
    nvnetdrv_stop_txrx();

    // Clear the nvnet running flag so threads know to end
    bool prev_value = atomic_exchange(&g_running, false);
    assert(prev_value);

    // TODO: Pass back all TX buffers to user.

    // Reset TX & RX control
    reg32(NvRegTxRxControl) = NVREG_TXRXCTL_DISABLE | NVREG_TXRXCTL_RESET;
    KeDelayExecutionThread(KernelMode, FALSE, TEN_MICRO);
    reg32(NvRegTxRxControl) = NVREG_TXRXCTL_DISABLE;

    // Free all memory allocated by nvnetdrv
    MmFreeContiguousMemory((void *)g_rxRing);
    MmFreeContiguousMemory((void *)g_rxRingUserBuffers);
}

void nvnetdrv_start_txrx (void)
{
    reg32(NvRegLinkSpeed) = g_linkSpeed | NVREG_LINKSPEED_FORCE;
    reg32(NvRegTransmitterControl) = NVREG_XMITCTL_START;
    reg32(NvRegReceiverControl) = NVREG_RCVCTL_START;
    reg32(NvRegTxRxControl) = NVREG_TXRXCTL_KICK | NVREG_TXRXCTL_GET;
}

void nvnetdrv_stop_txrx (void)
{
    reg32(NvRegLinkSpeed) = 0;
    reg32(NvRegReceiverControl) = 0;
    reg32(NvRegTransmitterControl) = 0;

    // Wait for active TX and RX descriptors to finish
    for (int i = 0; i < 50000; i++) {
        if (!((reg32(NvRegReceiverStatus) & NVREG_RCVSTAT_BUSY) ||
              (reg32(NvRegTransmitterStatus) & NVREG_XMITSTAT_BUSY))) {
            break;
        }
        KeDelayExecutionThread(KernelMode, FALSE, TEN_MICRO);
    }

    reg32(NvRegTxRxControl) = NVREG_TXRXCTL_DISABLE;

    for (int i = 0; i < 50000; i++) {
        if ((reg32(NvRegTxRxControl) & NVREG_TXRXCTL_IDLE) != 0) {
            break;
        }
        KeDelayExecutionThread(KernelMode, FALSE, TEN_MICRO);
    }
    reg32(NvRegTxRxControl) = 0;
}

void nvnetdrv_tx_transmit (struct pbuf *p)
{
    // Insert the packet into the transmit queue
    InsertTailList(&nvnetdrv_tx_queue, &p->ListEntry);

    // Lock the buffer pages since the NIC will be accessing them
    MmLockUnlockBufferPages(p->payload, p->len, FALSE);

    // Set up the first descriptor for the packet
    struct descriptor_t *tx_desc = g_tx_desc_free;
    tx_desc->paddr = MmGetPhysicalAddress(p->payload);

    // Calculate the number of bytes that cross over the page boundary
    size_t cross_page_bytes = PAGE_SIZE - (tx_desc->paddr & (PAGE_SIZE - 1));

    // Check if the packet crosses a page boundary
    // TODO: Use unlikely/likely macros
    if(cross_page_bytes < p->len) {
        // Store flag in the packet to indicate that it crosses a page boundary
        p->flags |= PBUF_FLAG_IS_DESCRIPTOR_SPLIT;

        // This packet crosses a page boundary. We need to split it into two descriptors for the NIC.
        struct descriptor_t *tx_desc0 = tx_desc;

        // Swap the first descriptor to the next one so that the NIC processes the second descriptor first
        tx_desc = (tx_desc0 == g_tx_desc_last ? g_tx_desc_first : (tx_desc0 + 1));
        tx_desc->paddr = MmGetPhysicalAddress((PVOID)((char *)p->payload + cross_page_bytes));

        // Increase the busy descriptor count to account for the second descriptor
        g_tx_busy++;

        // Enable the second descriptor
        tx_desc->length = p->len - cross_page_bytes - 1;
        tx_desc->flags  = NV_TX_VALID | NV_TX_LASTPACKET;

        // Enable first descriptor last to keep the NIC from sending incomplete packets
        tx_desc0->length = cross_page_bytes - 1;
        tx_desc0->flags  = NV_TX_VALID;
    } else {
        // Clear the flag in the packet to indicate that it does not cross a page boundary
        p->flags &= ~PBUF_FLAG_IS_DESCRIPTOR_SPLIT;

        // Enable the packet descriptor
        tx_desc->length = p->len - 1;
        tx_desc->flags  = NV_TX_VALID | NV_TX_LASTPACKET;
    }

    // Update the transmit free descriptor pointer to the next descriptor
    g_tx_desc_free = (tx_desc == g_tx_desc_last ? g_tx_desc_first : (tx_desc + 1));

    // Keep track of how many descriptors are in use
    g_tx_busy++;

    // Inform the NIC that we have TX packets waiting
    // TODO: Check if the NIC has been stopped before kicking it
    reg32(NvRegTxRxControl) = NVREG_TXRXCTL_KICK;
}

void nvnetdrv_rx_release (void *buffer_virt)
{
    size_t index = g_rxRingTail;
    while (!atomic_compare_exchange_weak(&g_rxRingTail, &index, (index + 1) % g_rxRingSize)) {
        index = g_rxRingTail;
    }
    g_rxRing[index].flags = 0;
    g_rxRing[index].paddr = nvnetdrv_rx_vtop((uint32_t)buffer_virt);
    g_rxRing[index].length = NVNET_RX_BUFF_LEN;
    g_rxRing[index].flags = NV_RX_AVAIL;
}
