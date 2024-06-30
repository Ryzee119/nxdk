// SPDX-License-Identifier: BSD

// SPDX-FileCopyrightText: 2001-2004 Swedish Institute of Computer Science.
// SPDX-FileCopyrightText: 2015 Matt Borgerson
// SPDX-FileCopyrightText: 2022 Stefan Schmidt
// SPDX-FileCopyrightText: 2022 Ryan Wendland
// SPDX-FileCopyrightText: 2024 Dustin Holden

#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"
#include "lwip/sys.h"
#include "lwip/snmp.h"
#include "lwip/ethip6.h"
#include "lwip/mld6.h"
#include "netif/etharp.h"
#include "netif/ppp/pppoe.h"
#include "nvnetdrv.h"
#include <xboxkrnl/xboxkrnl.h>
#include <assert.h>

/* Define those to better describe your network interface. */
#define IFNAME0 'x'
#define IFNAME1 'b'
#ifndef RX_BUFF_CNT
#define RX_BUFF_CNT (64)
#endif


#define LINK_SPEED_OF_YOUR_NETIF_IN_BPS 100 * 1000 * 1000 /* 100 Mbps */

static struct netif *g_pnetif;
static sys_mbox_t rx_mbox;

// DPC for handling TX packets from the lwIP stack
static KDPC nvnetif_tx_dcp;

// Network packets are queued here before being sent to the NIC driver
static LIST_ENTRY nvnetif_tx_queue;

/**
 * Helper struct to hold private data used to operate your ethernet interface.
 * Keeping the ethernet address of the MAC in this struct is not necessary
 * as it is already kept in the struct netif.
 * But this is only an example, anyway...
 */
struct nforceif
{
    struct eth_addr *ethaddr;
    /* Add whatever per-interface state that is needed here. */
};

/**
 * Create a memory pool of rx pbufs
 * RX buffers are linked to a custom pbuf during its lifecycle in lwip stack.
 * When the user has finished with the pbuf, it is freed by custom_free_function to allow the NIC to reuse the buffer
 * This is entirely zero-copy.
 * */
typedef struct
{
    struct pbuf_custom p;
    uint8_t *buff;
    uint16_t length;
} rx_pbuf_t;

LWIP_MEMPOOL_DECLARE(RX_POOL, RX_BUFF_CNT, sizeof(rx_pbuf_t), "Zero-copy RX PBUF pool");
static void rx_pbuf_free_callback(struct pbuf *p)
{
    rx_pbuf_t *rx_pbuf = (rx_pbuf_t *)p;
    nvnetdrv_rx_release(rx_pbuf->buff);
    LWIP_MEMPOOL_FREE(RX_POOL, rx_pbuf);
}

static void rx_callback_handler(void *arg)
{
    rx_pbuf_t *rx_pbuf;
    while(1) {
        sys_arch_mbox_fetch(&rx_mbox, (void **)&rx_pbuf, 0);
        struct pbuf *p = pbuf_alloced_custom(PBUF_RAW,
                                        rx_pbuf->length + ETH_PAD_SIZE,
                                        PBUF_REF,
                                        &rx_pbuf->p,
                                        rx_pbuf->buff - ETH_PAD_SIZE,
                                        NVNET_RX_BUFF_LEN - ETH_PAD_SIZE);

        if (g_pnetif->input(p, g_pnetif) != ERR_OK) {
            pbuf_free(p);
        }
    }
}

// This callback is from the HW IRQ, we cant directly send to lwip tcpip core
// as it may block and could deadlock us. Instead we post to a mailbox and handle it in a separate worker
// thread.
static void rx_callback(void *buffer, uint16_t length)
{
    rx_pbuf_t *rx_pbuf = (rx_pbuf_t *)LWIP_MEMPOOL_ALLOC(RX_POOL);
    assert(rx_pbuf != NULL);
    LWIP_ASSERT("RX_POOL full\n", rx_pbuf != NULL);
    if (rx_pbuf == NULL) return;
    rx_pbuf->p.custom_free_function = rx_pbuf_free_callback;
    rx_pbuf->buff = buffer;
    rx_pbuf->length = length;

    // Post it, this should always succeed as mbox size matches rx ring size
    err_t res = sys_mbox_trypost(&rx_mbox, (void *)rx_pbuf);
    assert(res == ERR_OK);
}

/**
 * In this function, the hardware should be initialized.
 * Called from nforceif_init().
 *
 * @param netif the already initialized lwip network interface structure
 * for this nforceif
 * @return ERR_OK if low level initiliazation succeeds
 *         ERR_IF if any failure
 */
static err_t low_level_init(struct netif *netif)
{
    if (nvnetdrv_init(RX_BUFF_CNT, rx_callback, PBUF_POOL_SIZE) < 0) {
        return ERR_IF;
    }

    // Initialize DPC
    KeInitializeDpc(&nvnetif_tx_dcp, nvnetif_tx_push, NULL);

    // Initialize TX queue
    InitializeListHead(&nvnetif_tx_queue);

    sys_mbox_new(&rx_mbox, RX_BUFF_CNT);
    sys_thread_new("rx_thread", rx_callback_handler, NULL,
                    DEFAULT_THREAD_STACKSIZE,DEFAULT_THREAD_PRIO);

    /* set MAC hardware address length */
    netif->hwaddr_len = ETHARP_HWADDR_LEN;

    /* set MAC hardware address */
    memcpy(netif->hwaddr, nvnetdrv_get_ethernet_addr(), 6);

    /* maximum transfer unit */
    netif->mtu = 1500;

    /* device capabilities */
    /* don't set NETIF_FLAG_ETHARP if this device is not an ethernet one */
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

#if LWIP_IPV6 && LWIP_IPV6_MLD
    /*
     * For hardware/netifs that implement MAC filtering.
     * All-nodes link-local is handled by default, so we must let the hardware know
     * to allow multicast packets in.
     * Should set mld_mac_filter previously. */
    if (netif->mld_mac_filter != NULL)
    {
        ip6_addr_t ip6_allnodes_ll;
        ip6_addr_set_allnodes_linklocal(&ip6_allnodes_ll);
        netif->mld_mac_filter(netif, &ip6_allnodes_ll, NETIF_ADD_MAC_FILTER);
    }
#endif /* LWIP_IPV6 && LWIP_IPV6_MLD */
    g_pnetif = netif;

    return ERR_OK;
}

/**
 * This function gets registered as callback function to free pbufs after the
 * NIC driver is done sending their contents.
 *
 * @param userdata the pbuf address, supplied by low_level_output
 */
void tx_pbuf_free_callback(void *userdata)
{
    struct pbuf *p = (struct pbuf *)userdata;
    pbuf_free(p);
}

/**
 * This function should do the actual transmission of the packet. The packet is
 * contained in the pbuf that is passed to the function. This pbuf
 * might be chained.
 *
 * @param netif the lwip network interface structure for this nforceif
 * @param p the MAC packet to send (e.g. IP packet including MAC addresses and type)
 * @return ERR_OK if the packet could be sent
 *                 an err_t value if the packet couldn't be sent
 *
 * @note Returning ERR_MEM here if a DMA queue of your MAC is full can lead to
 *             strange results. You might consider waiting for space in the DMA queue
 *             to become available since the stack doesn't retry to send a packet
 *             dropped because of memory failure (except for the TCP timers).
 */

static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
#if ETH_PAD_SIZE
    pbuf_header(p, -ETH_PAD_SIZE); /* drop the padding word */
#endif

    // TODO: Verify if we can support more than 2 pbufs chained. The driver
    // currently makes assumptions about split descriptors and the number of
    // pbufs chained.
    assert(p->len < 4096);
    assert(pbuf_clen(p) <= 2);

    // Add the pbuf to our transmission queue
    for (struct pbuf *q = p; q != NULL; q = q->next) {
        // Set the completion callback for the last pbuf in the chain
        if (q->next == NULL)
            q->Complete = (complete_cb)tx_pbuf_free_callback;

        // Add the pbuf to the queue
        InsertTailList(&nvnetif_tx_queue, &q->ListEntry);
    }

    // Increase pbuf refcount so they don't get freed while the NIC requires them
    pbuf_ref(p);

    // Queue DPC to handle the packet
    KeInsertQueueDpc(&nvnetif_tx_dcp, NULL, NULL);

    return ERR_OK;
}

/**
 * This function pushes a packet off the transmisson queue and into the lwIP driver.
 * Called from the DPC context.
 *
 * @param Dpc unused
 * @param DeferredContext unused
 * @param SystemArgument1 unused
 * @param SystemArgument2 unused
 *
 * @return VOID
 */
void NTAPI nvnetif_tx_push(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2)
{
    LWIP_UNUSED_ARG(Dpc);
    LWIP_UNUSED_ARG(DeferredContext);
    LWIP_UNUSED_ARG(SystemArgument1);
    LWIP_UNUSED_ARG(SystemArgument2);

    while (!IsListEmpty(&nvnetif_tx_queue) && nvnetdrv_tx_ready())
    {
        LIST_ENTRY *entry = RemoveHeadList(&nvnetif_tx_queue);

        // TODO: Make sure the network has not been stopped

        // Send the packet to the driver
        nvnetdrv_tx_transmit((struct pbuf *)CONTAINING_RECORD(entry, struct pbuf, ListEntry));
    }
}

/**
 * Should be called at the beginning of the program to set up the
 * network interface. It calls the function low_level_init() to do the
 * actual setup of the hardware.
 *
 * This function should be passed as a parameter to netif_add().
 *
 * @param netif the lwip network interface structure for this nforceif
 * @return ERR_OK if the loopif is initialized
 *                 ERR_MEM if private data couldn't be allocated
 *                 any other err_t on error
 */
err_t nvnetif_init(struct netif *netif)
{
    struct nforceif *nforceif;

    LWIP_ASSERT("netif != NULL", (netif != NULL));

    nforceif = mem_malloc(sizeof(struct nforceif));
    if (nforceif == NULL) {
        LWIP_DEBUGF(NETIF_DEBUG, ("nforceif_init: out of memory\n"));
        return ERR_MEM;
    }

#if LWIP_NETIF_HOSTNAME
    /* Initialize interface hostname */
    netif->hostname = "lwip";
#endif /* LWIP_NETIF_HOSTNAME */

    /*
     * Initialize the snmp variables and counters inside the struct netif.
     * The last argument should be replaced with your link speed, in units
     * of bits per second.
     */
    MIB2_INIT_NETIF(netif, snmp_ifType_ethernet_csmacd, LINK_SPEED_OF_YOUR_NETIF_IN_BPS);

    netif->state = nforceif;
    netif->name[0] = IFNAME0;
    netif->name[1] = IFNAME1;
    /* We directly use etharp_output() here to save a function call.
     * You can instead declare your own function an call etharp_output()
     * from it if you have to do some checks before sending (e.g. if link
     * is available...) */
    netif->output = etharp_output;
#if LWIP_IPV6
    netif->output_ip6 = ethip6_output;
#endif /* LWIP_IPV6 */
    netif->linkoutput = low_level_output;

    nforceif->ethaddr = (struct eth_addr *)&(netif->hwaddr[0]);

    /* initialize the hardware */
    return low_level_init(netif);
}
