// SPDX-License-Identifier: MIT

// SPDX-FileCopyrightText: 2022 Stefan Schmidt
// SPDX-FileCopyrightText: 2022 Ryan Wendland
// SPDX-FileCopyrightText: 2024 Dustin Holden

#ifndef __NVNETDRV_H__
#define __NVNETDRV_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <xboxkrnl/xboxkrnl.h>
#include <lwip/pbuf.h>

// Must be greater than max ethernet frame size. A multiple of page size prevents page boundary crossing
#define NVNET_RX_BUFF_LEN (PAGE_SIZE / 2)

// NVNET error codes
#define NVNET_OK 0
#define NVNET_NO_MEM -1
#define NVNET_NO_MAC -2
#define NVNET_PHY_ERR -3
#define NVNET_SYS_ERR -4

typedef void (*nvnetdrv_rx_callback_t) (void *buffer, uint16_t length);

/**
 * Temporarily stop sending and receiving ethernet packets. TX packets will be held. RX packets will be dropped.
 */
void nvnetdrv_stop_txrx (void);

/**
 * Resume sending and receiving ethernet packets after nvnetdrv_stop_txrx();
 */
void nvnetdrv_start_txrx (void);

/**
 * Check if the NIC is ready to transmit a packet.
 */
bool nvnetdrv_tx_ready (void);

/**
 * Transmit a packet. This function is called by lwIP.
 */
void nvnetdrv_tx_transmit (struct pbuf *p);

/**
 * This function pushes a packet off the transmisson queue and into the lwIP driver.
 * @param Dpc unused
 * @param DeferredContext unused
 * @param SystemArgument1 unused
 * @param SystemArgument2 unused
 * @return VOID
 */
void NTAPI nvnetif_tx_push(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2);

/**
 * Initialised the low level NIC hardware.
 * @param rx_buffer_count The number of receive buffers to reserve for network packets
 * @param rx_callback. Pointer to a callback function that is called when a new packet is received by the NIC.
 * @param tx_queue_size. How many packets can be queued for transfer simultaneously.
 * @return NVNET_OK or the error.
 */
int nvnetdrv_init (size_t rx_buffer_count, nvnetdrv_rx_callback_t rx_callback, size_t tx_queue_size);

/**
 * Stop the low level NIC hardware. Should be called after nvnetdrv_init() to shutdown the NIC hardware.
 */
void nvnetdrv_stop (void);

/**
 * Returns the ethernet MAC Address.
 * @return A pointer to an array containing the 6 byte ethernet MAC address.
 */
const uint8_t *nvnetdrv_get_ethernet_addr (void);

/**
 * Releases an RX buffer given out by nvnetdrv. All RX buffers need to be
 * released eventually, or the NIC will run out of buffers to use.
 * This function is thread-safe.
 * @param buffer_virt Pointer to the buffer given out by nvnetdrv.
 */
void nvnetdrv_rx_release (void *buffer_virt);

#endif
