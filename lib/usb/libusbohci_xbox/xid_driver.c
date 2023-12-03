//Copyright 2020, Ryan Wendland
//SPDX-License-Identifier: MIT

/*
 * XID input driver
 * This driver is applied with the following Original Xbox Input Devices:
 * - Original Xbox gamecontrollers (S, Duke, Wheels, Arcade Sticks etc)
 * - Xbox DVD Movie Playback IR Dongle
 * - Steel Battalion Controller
 * This code should be read in conjuntion with https://xboxdevwiki.net/index.php?title=Xbox_Input_Devices
 */

#include <stdio.h>
#include <string.h>
#include "usbh_lib.h"
#include "xid_driver.h"

//#define ENABLE_USBH_XID_DEBUG
#ifdef ENABLE_USBH_XID_DEBUG
#include <hal/debug.h>
#define USBH_XID_DEBUG debugPrint
#else
#define USBH_XID_DEBUG(...)
#endif

/* Xbox One and XSX specific */
#define GIP_CMD_ACK 0x01
#define GIP_CMD_ANNOUNCE 0x02
#define GIP_CMD_IDENTIFY 0x04
#define GIP_CMD_POWER 0x05
#define GIP_CMD_AUTHENTICATE 0x06
#define GIP_CMD_VIRTUAL_KEY 0x07
#define GIP_CMD_RUMBLE 0x09
#define GIP_CMD_LED 0x0a
#define GIP_CMD_FIRMWARE 0x0c
#define GIP_CMD_INPUT 0x20
#define GIP_SEQ0 0x00
#define GIP_OPT_ACK 0x10
#define GIP_OPT_INTERNAL 0x20
#define GIP_PL_LEN(N) (N)
#define GIP_PWR_ON 0x00
#define GIP_PWR_SLEEP 0x01
#define GIP_PWR_OFF 0x04
#define GIP_PWR_RESET 0x07
#define GIP_LED_ON 0x01
#define BIT(n) (1UL << (n))
#define GIP_MOTOR_R  BIT(0)
#define GIP_MOTOR_L  BIT(1)
#define GIP_MOTOR_RT BIT(2)
#define GIP_MOTOR_LT BIT(3)
#define GIP_MOTOR_ALL (GIP_MOTOR_R | GIP_MOTOR_L | GIP_MOTOR_RT | GIP_MOTOR_LT)
static uint8_t xboxone_power_on[] = {GIP_CMD_POWER, GIP_OPT_INTERNAL, GIP_SEQ0, GIP_PL_LEN(1), GIP_PWR_ON};
static uint8_t xboxone_s_init[] = {GIP_CMD_POWER, GIP_OPT_INTERNAL, GIP_SEQ0, GIP_PL_LEN(15), 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0x53, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static uint8_t xboxone_s_led_init[] = {GIP_CMD_LED, GIP_OPT_INTERNAL, GIP_SEQ0, GIP_PL_LEN(3), 0x00, 0x01, 0x14};
static uint8_t extra_input_packet_init[] = {0x4d, 0x10, GIP_SEQ0, 0x02, 0x07, 0x00};
static uint8_t xboxone_pdp_led_on[] = {GIP_CMD_LED, GIP_OPT_INTERNAL, GIP_SEQ0, GIP_PL_LEN(3), 0x00, GIP_LED_ON, 0x14};
static uint8_t xboxone_pdp_auth[] = {GIP_CMD_AUTHENTICATE, GIP_OPT_INTERNAL, GIP_SEQ0, GIP_PL_LEN(2), 0x01, 0x00};
static void xbox_one_init(xid_dev_t *xid) {
    usbh_xid_write(xid, 0, xboxone_power_on, sizeof(xboxone_power_on), NULL);
    usbh_xid_write(xid, 0, xboxone_s_init, sizeof(xboxone_s_init), NULL);
    usbh_xid_write(xid, 0, xboxone_s_led_init, sizeof(xboxone_s_led_init), NULL);

    if (xid->idVendor == 0x045e && xid->idProduct == 0x0b00) {
        usbh_xid_write(xid, 0, extra_input_packet_init, sizeof(extra_input_packet_init), NULL);
    }

    if (xid->idVendor == 0x0e6f)
    {
        usbh_xid_write(xid, 0, xboxone_pdp_led_on, sizeof(xboxone_pdp_led_on), NULL);
        usbh_xid_write(xid, 0, xboxone_pdp_auth, sizeof(xboxone_pdp_auth), NULL);
    }
}

static xid_dev_t xid_devices[CONFIG_XID_MAX_DEV];
static xid_dev_t *pxid_list = NULL;
static XID_CONN_FUNC *xid_conn_func = NULL, *xid_disconn_func = NULL;

static xid_dev_t *alloc_xid_device(void) {
    xid_dev_t *new_xid = NULL;
    for (int i = 0; i < CONFIG_XID_MAX_DEV; i++)
    {
        if (xid_devices[i].iface == NULL)
        {
            new_xid = &xid_devices[i];
            break;
        }
    }

    if (new_xid == NULL)
    {
        return NULL;
    }

    memset(new_xid, 0, sizeof(xid_dev_t));
    new_xid->uid = get_ticks();

    //Chain the new XID to the end of the list.
    if (pxid_list == NULL)
    {
        pxid_list = new_xid;
    }
    else
    {
        xid_dev_t *x;
        for (x = pxid_list; x->next != NULL; x = x->next)
            ;
        x->next = new_xid;
    }

    return new_xid;
}

static void free_xid_device(xid_dev_t *xid_dev) {
    xid_dev_t *head = pxid_list;

    //If the xid is at the head of the list, remove now.
    if (xid_dev == head)
    {
        pxid_list = head->next;
        head = NULL;
    }

    //Find the device head in the linked list
    while (head != NULL && head->next != xid_dev)
    {
        head = head->next;
    }

    //Remove it from the linked list
    if (head != NULL)
    {
        xid_dev_t *new_tail = xid_dev->next;
        head->next = new_tail;
    }

    //Mark it as free
    memset(xid_dev, 0, sizeof(xid_dev_t));
}

static int xid_probe(IFACE_T *iface) {
    UDEV_T *udev = iface->udev;
    xid_dev_t *xid;
    ALT_IFACE_T *aif = iface->aif;
    DESC_IF_T *ifd = aif->ifd;


    xidtype_t type = XIDUNKNOWN;
    xid_descriptor *xid_desc = usbh_alloc_mem(sizeof(xid_descriptor));
    if (xid_desc == NULL)
    {
        return USBH_ERR_MEMORY_OUT;
    }
    memset(xid_desc, 0, sizeof(xid_descriptor));

    //FIXME: Are these true across all devices including 3rd party?
    if (ifd->bInterfaceSubClass == 0x5D &&
             ifd->bInterfaceProtocol == 0x01)
    {
        type = XBOX360_WIRED;
        xid_desc->bType = XID_TYPE_GAMECONTROLLER;
    }
    else if (ifd->bInterfaceSubClass == 0x47 &&
             ifd->bInterfaceProtocol == 0xD0 &&
             aif->ep[0].bInterval == 0x04 &&
             aif->ep[1].bInterval == 0x04) //The one we want has a bInternal of 4ms on both eps.
    {
        type = XBOXONE;
        xid_desc->bType = XID_TYPE_GAMECONTROLLER;
    }
    else if (ifd->bInterfaceClass == XID_INTERFACE_CLASS &&
             ifd->bInterfaceSubClass == XID_INTERFACE_SUBCLASS)
    {
        // Device is an OG Xbox peripheral. Check the XID descriptor to find out what type:
        // Ref https://xboxdevwiki.net/Xbox_Input_Devices
        uint32_t xfer_len;
        usbh_ctrl_xfer(iface->udev,
                       0xC1,                   /* bmRequestType */
                       0x06,                   /* bRequest */
                       0x4200,                 /* wValue */
                       iface->if_num,          /* wIndex */
                       sizeof(xid_descriptor), /* wLength */
                       (uint8_t *)xid_desc, &xfer_len, 100);
        uint8_t xid_bType = xid_desc->bType;
        switch (xid_bType)
        {
            case 0x01: type = XBOXOG_CONTROLLER;     break; //Duke,S,Wheel,Arcade stick
            case 0x03: type = XBOXOG_XIR;            break; //Xbox DVD Movie Playback IR Dongle
            case 0x80: type = XBOXOG_STEELBATTALION; break; //Steel Battalion Controller
            default:   type = XIDUNKNOWN;            break;
        }
    }

    if (type == XIDUNKNOWN)
    {
        usbh_free_mem(xid_desc, sizeof(xid_descriptor));
        return USBH_ERR_NOT_MATCHED;
    }

    xid = alloc_xid_device();
    if (xid == NULL)
    {
        usbh_free_mem(xid_desc, sizeof(xid_descriptor));
        return USBH_ERR_MEMORY_OUT;
    }

    memcpy(&xid->xid_desc, xid_desc, sizeof(xid_descriptor));
    usbh_free_mem(xid_desc, sizeof(xid_descriptor));

    xid->iface = iface;
    xid->idVendor = udev->descriptor.idVendor;
    xid->idProduct = udev->descriptor.idProduct;
    xid->next = NULL;
    xid->user_data = NULL;
    xid->type = type;
    xid->odata_serial = 1;
    iface->context = (void *)xid;

    // Handle controller specific initialisations
    if (xid->type == XBOXONE)
    {
        xbox_one_init(xid);
    }

    else if (xid->type == XBOX360_WIRED)
    {
        uint8_t port = iface->udev->port_num;
        port += (port <= 2) ? 2 : -2; //Xbox ports are numbered 3,4,1,2, make it 1,2,3,4
        uint8_t xbox360wired_set_led[] = {0x01, 0x03, port + 5};
        usbh_xid_write(xid, 0, xbox360wired_set_led, sizeof(xbox360wired_set_led), NULL);
    }

    if (xid_conn_func)
    {
        xid_conn_func(xid, 0);
    }

    return USBH_OK;
}

static void xid_disconnect(IFACE_T *iface) {
    xid_dev_t *xid_dev;
    UTR_T *utr;
    xid_dev = (xid_dev_t *)(iface->context);

    //Stop any running pipe transfers
    for (int i = 0; i < iface->aif->ifd->bNumEndpoints; i++)
    {
        iface->udev->hc_driver->quit_xfer(NULL, &(iface->aif->ep[i]));
    }

    //Free any running UTRs
    for (int i = 0; i < XID_MAX_TRANSFER_QUEUE; i++)
    {
        utr = xid_dev->utr_list[i];
        if (utr != NULL)
        {
            usbh_quit_utr(utr);
            usbh_free_mem(utr->buff, utr->ep->wMaxPacketSize);
            free_utr(utr);
        }
    }

    USBH_XID_DEBUG("xid disconnected - device (vid=0x%x, pid=0x%x), interface %d.\n",
               xid_dev->idVendor, xid_dev->idProduct, iface->if_num);

    if (xid_disconn_func)
    {
        xid_disconn_func(xid_dev, 0);
    }

    free_xid_device(xid_dev);
}

UDEV_DRV_T xid_driver =
{
    xid_probe,
    xid_disconnect,
    NULL, //suspend
    NULL, //resume
};

/**
 * @brief Adds a callback function when an XID interface is connected or removed. Pass NULL to remove the callback. 
 * 
 * @param conn_func  The user's connection callback function.
 * @param disconn_func The user's disconnection callback function.
 */
void usbh_install_xid_conn_callback(XID_CONN_FUNC *conn_func, XID_CONN_FUNC *disconn_func) {
    xid_conn_func = conn_func;
    xid_disconn_func = disconn_func;
}

/**
 * @brief Initialises the XID driver with the USB backend.
 * 
 */
void usbh_xid_init(void) {
    usbh_register_driver(&xid_driver);
}

/**
 * Returns a pointer to the first connected XID device. This is a linked list
 * to all connected XID devices.
 * @return A pointer to a xid_dev_t device.
 */
xid_dev_t *usbh_xid_get_device_list(void) {
    return pxid_list;
}

static void int_read_callback_hook(UTR_T *utr) {
    xid_dev_t *xid_dev = (xid_dev_t *)utr->context;
    void *user_callback = NULL;

    for (int i = 0; i < XID_MAX_TRANSFER_QUEUE; i++)
    {
        UTR_T *_utr = xid_dev->utr_list[i];
        if (utr == _utr)
        {
            user_callback = xid_dev->rx_complete_cb[i];
            break;
        }
    }

    if (utr->status < 0 || xid_dev == NULL || xid_dev->user_data == NULL)
    {
        goto pass_to_user;
    }

    if (xid_dev->type == XBOXONE)
    {
        uint8_t *rdata = utr->buff;
        if (rdata[0] == GIP_CMD_INPUT) {
            xid_gamepad_in remap;
            memset(&remap, 0, sizeof(xid_gamepad_in));
            uint16_t xbone_buttons = rdata[5] << 8 | rdata[4];

            remap.startByte = 0;
            remap.bLength = sizeof(xid_gamepad_in);
            if (xbone_buttons & (1 << 8)) remap.dButtons |= OGX_GAMEPAD_DPAD_UP;
            if (xbone_buttons & (1 << 9)) remap.dButtons |= OGX_GAMEPAD_DPAD_DOWN;
            if (xbone_buttons & (1 << 10)) remap.dButtons |= OGX_GAMEPAD_DPAD_LEFT;
            if (xbone_buttons & (1 << 11)) remap.dButtons |= OGX_GAMEPAD_DPAD_RIGHT;
            if (xbone_buttons & (1 << 2)) remap.dButtons |= OGX_GAMEPAD_START;
            if (xbone_buttons & (1 << 3)) remap.dButtons |= OGX_GAMEPAD_BACK;
            if (xbone_buttons & (1 << 14)) remap.dButtons |= OGX_GAMEPAD_LEFT_THUMB;
            if (xbone_buttons & (1 << 15)) remap.dButtons |= OGX_GAMEPAD_RIGHT_THUMB;
            if (xbone_buttons & (1 << 12)) remap.white = 0xFF;
            if (xbone_buttons & (1 << 13)) remap.black = 0xFF;
            if (xbone_buttons & (1 << 4)) remap.a = 0xFF;
            if (xbone_buttons & (1 << 5)) remap.b = 0xFF;
            if (xbone_buttons & (1 << 6)) remap.x = 0xFF;
            if (xbone_buttons & (1 << 7)) remap.y = 0xFF;

            remap.l = (rdata[7] << 8 | rdata[6]) >> 2;
            remap.r = (rdata[9] << 8 | rdata[8]) >> 2;
            remap.leftStickX = rdata[11] << 8 | rdata[10];
            remap.leftStickY = rdata[13] << 8 | rdata[12];
            remap.rightStickX = rdata[15] << 8 | rdata[14];
            remap.rightStickY = rdata[17] << 8 | rdata[16];
            memcpy(utr->buff, &remap, sizeof(xid_gamepad_in));
        } else if(rdata[0] == GIP_CMD_ANNOUNCE) {
            xbox_one_init(xid_dev);
            goto ignore_and_requeue;
        } else {
            goto ignore_and_requeue;
        }
        utr->xfer_len = sizeof(xid_gamepad_in);
    }
    if (xid_dev->type == XBOX360_WIRED)
    {
        xid_gamepad_in remap;
        memset(&remap, 0, sizeof(xid_gamepad_in));
        uint8_t *rdata = utr->buff;
        uint16_t xb360_buttons = rdata[3] << 8 | rdata[2];

        remap.startByte = 0;
        remap.bLength = sizeof(xid_gamepad_in);
        if (xb360_buttons & (1 << 0)) remap.dButtons |= OGX_GAMEPAD_DPAD_UP;
        if (xb360_buttons & (1 << 1)) remap.dButtons |= OGX_GAMEPAD_DPAD_DOWN;
        if (xb360_buttons & (1 << 2)) remap.dButtons |= OGX_GAMEPAD_DPAD_LEFT;
        if (xb360_buttons & (1 << 3)) remap.dButtons |= OGX_GAMEPAD_DPAD_RIGHT;
        if (xb360_buttons & (1 << 4)) remap.dButtons |= OGX_GAMEPAD_START;
        if (xb360_buttons & (1 << 5)) remap.dButtons |= OGX_GAMEPAD_BACK;
        if (xb360_buttons & (1 << 6)) remap.dButtons |= OGX_GAMEPAD_LEFT_THUMB;
        if (xb360_buttons & (1 << 7)) remap.dButtons |= OGX_GAMEPAD_RIGHT_THUMB;
        if (xb360_buttons & (1 << 8)) remap.white = 0xFF;
        if (xb360_buttons & (1 << 9)) remap.black = 0xFF;
        if (xb360_buttons & (1 << 12)) remap.a = 0xFF;
        if (xb360_buttons & (1 << 13)) remap.b = 0xFF;
        if (xb360_buttons & (1 << 14)) remap.x = 0xFF;
        if (xb360_buttons & (1 << 15)) remap.y = 0xFF;

        remap.l = (rdata[4] << 8 | rdata[4]) >> 2;
        remap.r = (rdata[5] << 8 | rdata[5]) >> 2;
        remap.leftStickX = rdata[7] << 8 | rdata[6];
        remap.leftStickY = rdata[9] << 8 | rdata[8];
        remap.rightStickX = rdata[11] << 8 | rdata[10];
        remap.rightStickY = rdata[13] << 8 | rdata[12];

        if (rdata[1] == 0x14) {
            memcpy(utr->buff, &remap, sizeof(xid_gamepad_in));
        } else {
            goto ignore_and_requeue;
        }
        utr->xfer_len = sizeof(xid_gamepad_in);
    }

pass_to_user:
    if (user_callback)
    {
        FUNC_UTR_T utr_callback = (FUNC_UTR_T)user_callback;
        utr_callback(utr);
    }
    return;

ignore_and_requeue:
    utr->xfer_len = 0;
    utr->bIsTransferDone = 0;
    usbh_int_xfer(utr);
}

static int32_t queue_int_xfer(xid_dev_t *xid_dev, uint8_t dir, uint8_t ep_addr, uint8_t *buff, uint32_t len, void *callback) {
    IFACE_T *iface = (IFACE_T *)xid_dev->iface;
    UTR_T *utr = NULL;
    int ret, i, free_slot;

    if (iface == NULL || iface->udev == NULL)
    {
        return USBH_ERR_DISCONNECTED;
    }

    EP_INFO_T *ep = usbh_iface_find_ep(iface, ep_addr, dir | EP_ATTR_TT_INT);
    if (ep == NULL)
    {
        return USBH_ERR_EP_NOT_FOUND;
    }

    //Clean up finished UTRs in the queue
    for (i = 0; i < XID_MAX_TRANSFER_QUEUE; i++)
    {
        utr = xid_dev->utr_list[i];
        if (utr != NULL && utr->ep != NULL && utr->ep->bEndpointAddress == ep->bEndpointAddress)
        {
            //Don't queue multiple reads. User is calling faster than controller can update so has no benefit.
            if (dir == EP_ADDR_DIR_IN && utr->bIsTransferDone == 0)
            {
                return HID_RET_XFER_IS_RUNNING;
            }

            if (utr->bIsTransferDone)
            {
                usbh_free_mem(utr->buff, utr->ep->wMaxPacketSize);
                free_utr(utr);
                xid_dev->utr_list[i] = NULL;
            }
        }
    }

    //Find a free slot in the queue
    free_slot = USBH_ERR_MEMORY_OUT;
    for (i = 0; i < XID_MAX_TRANSFER_QUEUE; i++)
    {
        if (xid_dev->utr_list[i] == NULL)
        {
            free_slot = i;
            break;
        }
    }

    if (free_slot < 0)
    {
        return free_slot;
    }

    utr = alloc_utr(iface->udev);
    if (utr == NULL)
    {
        return USBH_ERR_MEMORY_OUT;
    }

    //Populate the new utr
    utr->context = xid_dev;
    utr->ep = ep;
    utr->data_len = (dir == EP_ADDR_DIR_OUT && len < ep->wMaxPacketSize) ? len : ep->wMaxPacketSize;
    utr->xfer_len = 0;
    utr->func = (dir == EP_ADDR_DIR_IN) ? int_read_callback_hook : (FUNC_UTR_T)callback;
    utr->buff = usbh_alloc_mem(ep->wMaxPacketSize);
    if (utr->buff == NULL)
    {
        free_utr(utr);
        return USBH_ERR_MEMORY_OUT;
    }

    if (dir == EP_ADDR_DIR_OUT)
    {
        memcpy(utr->buff, buff, utr->data_len);
    }

    //Register a queued UTR for this device.
    xid_dev->rx_complete_cb[free_slot] = callback;
    xid_dev->utr_list[free_slot] = utr;

    ret = usbh_int_xfer(utr);
    if (ret != USBH_OK)
    {
        usbh_free_mem(utr->buff, ep->wMaxPacketSize);
        free_utr(utr);
        return ret;
    }

    return USBH_OK;
}

/**
 * @brief Reads an interrupt in endpoint. This is a nonblocking function. The received data can be read from the callback function.
 * 
 * @param xid_dev Pointer to the XID device.
 * @param ep_addr The endpoint address to read. If ep_addr equal zero, the first interrupt in endpoint found will be used.
 * @param rx_complete_callback The user's receive complete callback function with the form `void my_callback(UTR_T *utr)`.
 * The received data is read from the user implemented callback function.
 * @return USBH_OK or the error.
 */
int32_t usbh_xid_read(xid_dev_t *xid_dev, uint8_t ep_addr, void *rx_complete_callback) {
    return queue_int_xfer(xid_dev, EP_ADDR_DIR_IN, ep_addr, NULL, 0, rx_complete_callback);
}

/**
 * @brief Queues a write to an interrupt out endpoint. This is a nonblocking function. The total amount of transfers that can be queued is
 * set by XID_MAX_TRANSFER_QUEUE.
 * 
 * @param xid_dev Pointer to the XID device.
 * @param ep_addr The endpoint address to write. If ep_addr equal zero, the first interrupt out endpoint found will be used.
 * @param txbuff The user transmit buffer
 * @param len The number of bytes to transfer
 * @param tx_complete_callback Optional transfer complete callback function.
 * @return USBH_OK or the error.
 */
int32_t usbh_xid_write(xid_dev_t *xid_dev, uint8_t ep_addr, uint8_t *txbuff, uint32_t len, void *tx_complete_callback) {
    if (xid_dev->type == XBOXONE) {
        txbuff[2] = xid_dev->odata_serial++;
    }
    return queue_int_xfer(xid_dev, EP_ADDR_DIR_OUT, ep_addr, txbuff, len, tx_complete_callback);
}

/**
 * @brief Send a rumble command to an xid game controller.
 * 
 * @param xid_dev Pointer to the XID device.
 * @param l_value Value of the low frequency rumble. 0 to 0xFFFF.
 * @param h_value Value of the high frequency rumble. 0 to 0xFFFF.
 * @return USBH_OK or the error. 
 */
int32_t usbh_xid_rumble(xid_dev_t *xid_dev, uint16_t l_value, uint16_t h_value)
{
    if (xid_dev->type == XBOXOG_CONTROLLER)
    {
        xid_gamepad_out command =
            {
                .startByte = 0,
                .bLength = sizeof(xid_gamepad_out),
                .lValue = l_value,
                .hValue = h_value,
            };
        return usbh_xid_write(xid_dev, 0, (uint8_t *)&command, sizeof(xid_gamepad_out), NULL);
    }

    else if (xid_dev->type == XBOXONE)
    {
        uint8_t xboxone_rumble[] = {GIP_CMD_RUMBLE, 0x00, 0x00, GIP_PL_LEN(9), 0x00, GIP_MOTOR_ALL, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF};
        xboxone_rumble[8] = l_value / 512;
        xboxone_rumble[9] = h_value / 512;
        return usbh_xid_write(xid_dev, 0, xboxone_rumble, sizeof(xboxone_rumble), NULL);
    }

    else if (xid_dev->type == XBOX360_WIRED)
    {
        uint8_t xbox360_wired_rumble[] = {0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        xbox360_wired_rumble[3] = l_value / 256;
        xbox360_wired_rumble[4] = h_value / 256;
        return usbh_xid_write(xid_dev, 0, xbox360_wired_rumble, sizeof(xbox360_wired_rumble), NULL);
    }

    return USBH_OK;
}

/**
 * @brief Returns the xid_type.
 * 
 * @param xid_dev Pointer to the XID device.
 * @return xid_type. Refer to xid_type enum.
 */
xid_type usbh_xid_get_type(xid_dev_t *xid_dev)
{
    if (xid_dev->xid_desc.bLength == 0)
    {
        return XID_UNKNOWN;
    }

    if (xid_dev->xid_desc.bType == 0x01)
    {
        switch (xid_dev->xid_desc.bSubType)
        {
            case 0x01: return GAMECONTROLLER_DUKE; break;
            case 0x02: return GAMECONTROLLER_S; break;
            case 0x10: return GAMECONTROLLER_WHEEL; break;
            case 0x20: return GAMECONTROLLER_ARCADESTICK; break;
            default:
                USBH_XID_DEBUG("Unknown XID SubType\n");
                return XID_UNKNOWN;
                break;
        }
    }

    if (xid_dev->xid_desc.bType == 0x03)
    {
        switch (xid_dev->xid_desc.bSubType)
        {
            case 0x00: return XREMOTE; break;
            default:
                USBH_XID_DEBUG("Unknown XID SubType\n");
                return XID_UNKNOWN;
        }
    }

    if (xid_dev->xid_desc.bType == 0x80)
    {
        switch (xid_dev->xid_desc.bSubType)
        {
            case 0x01: return STEEL_BATTALION; break;
            default:
                USBH_XID_DEBUG("Unknown XID SubType\n");
                return XID_UNKNOWN;
        }
    }

    USBH_XID_DEBUG("Unknown XID Type\n");
    return XID_UNKNOWN;
}
