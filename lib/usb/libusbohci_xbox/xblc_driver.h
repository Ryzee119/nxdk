
//Copyright 2020, Ryan Wendland
//SPDX-License-Identifier: MIT
#ifndef _USBH_XBLC_H_
#define _USBH_XBLC_H_

#include "usb.h"

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef CONFIG_XBLC_MAX_DEV
#define CONFIG_XBLC_MAX_DEV 4
#endif
#ifndef XBLC_NUM_BUFFERS
#define XBLC_NUM_BUFFERS 2
#endif
//Ref https://xboxdevwiki.net/Xbox_Live_Communicator
#define XBLC_INTERFACE_CLASS 0x78
#define XBLC_INTERFACE_SUBCLASS 0x0

#define XBLC_SET_SAMPLE_RATE 0x00
#define XBLC_SET_AUTO_GAIN_CONTROL 0x01

#define XBLC_BYTES_PER_SAMPLE 2

struct xblc_dev;
typedef void(XBLC_CONN_FUNC)(struct xblc_dev *xdev, int param);
typedef int(XBLC_CB_FUNC)(struct xblc_dev *dev, int16_t *data, int num_samples);

typedef enum
{
    XBLC_SPEAKER = 0,
    XBLC_MICROPHONE
} xblc_interface;

typedef enum
{
    XBLC_RATE_8000  = 0,
    XBLC_RATE_11025 = 1,
    XBLC_RATE_16000 = 2,
    XBLC_RATE_22050 = 3,
    XBLC_RATE_24000 = 4,
    XBLC_RATE_MAX_NUM = 5
} xblc_samplerate;

typedef struct xblc_stream
{
    IFACE_T *iface;
    UTR_T *utr[XBLC_NUM_BUFFERS];
    XBLC_CB_FUNC *xfer_cb;
    uint32_t dir;
    uint8_t is_streaming;
} xblc_stream_t;

typedef struct xblc_dev
{
    UDEV_T *udev;
    uint32_t uid;
    uint16_t idVendor;
    uint16_t idProduct;
    xblc_samplerate samplerate;
    uint8_t auto_gain_control;
    xblc_stream_t speaker;
    xblc_stream_t microphone;
    struct xblc_dev *next;         //Pointer to the next xid in the linked list.
    void *user_data;               //Pointer to an optional user struct
} xblc_dev_t;

void usbh_xblc_init(void);
void usbh_install_xblc_conn_callback(XBLC_CONN_FUNC *conn_func, XBLC_CONN_FUNC *disconn_func);
xblc_dev_t *usbh_xblc_get_device_list(void);
int32_t usbh_xblc_start_audio_in(xblc_dev_t *xblc_dev, XBLC_CB_FUNC *audio_in_cb);
int32_t usbh_xblc_start_audio_out(xblc_dev_t *xblc_dev, XBLC_CB_FUNC *audio_out_cb);
int32_t usbh_xblc_stop_audio_in(xblc_dev_t *xblc_dev);
int32_t usbh_xblc_stop_audio_out(xblc_dev_t *xblc_dev);
int32_t usbh_xblc_set_sample_rate(xblc_dev_t *xblc_dev, xblc_samplerate rate);
int32_t usbh_xblc_get_sample_rate(xblc_dev_t *xblc_dev);
int32_t usbh_xblc_set_auto_gain_control(xblc_dev_t *xblc_dev, uint16_t state);
uint8_t usbh_xblc_get_port(xblc_dev_t *xblc_dev);

#ifdef __cplusplus
}
#endif

#endif
