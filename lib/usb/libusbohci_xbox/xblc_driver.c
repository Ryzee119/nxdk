//Copyright 2020, Ryan Wendland
//SPDX-License-Identifier: MIT

/*
 * Xbox Live Communicator (XBLC) driver
 * This code should be read in conjuntion with https://xboxdevwiki.net/Xbox_Live_Communicator
 */

#include <stdio.h>
#include <string.h>
#include "usbh_lib.h"
#include "hub.h"
#include "xblc_driver.h"

//#define ENABLE_USBH_XBLC_DEBUG
#ifdef ENABLE_USBH_XBLC_DEBUG
#include <hal/debug.h>
#define USBH_XBLC_DEBUG debugPrint
#else
#define USBH_XBLC_DEBUG(...)
#endif

/* SET SAMPLE RATE: */
//Phantasy Star Online packet looks like:
//bmRequestType=0x41, bRequest=0x03, wValue=0x0100, wIndex=0x0000, wLength=0x0000
//The game then sends/receives 16 bytes every USB frame. Sample rate = 16kbytes/s = 8k samples/s

//NUDE packet looks like:
//bmRequestType=0x41, bRequest=0x03, wValue=0x0101, wIndex=0x0000, wLength=0x0000
//The game then sends/receives 22 bytes every USB frame. Sample rate = 22kbytes/s = 11k samples/s
//However, this game sends an extra sample (2 bytes) every 40 USB frames (40ms) which adds 25 samples/s.
//The final sample rate is 11025 ksamples/s as expected.

//Rainbow Six 3 packet looks like:
//bmRequestType=0x41, bRequest=0x03, wValue=0x0102, wIndex=0x0000, wLength=0x0000
//The game then sends/receives 32 bytes every USB frame. Sample rate = 32kbytes/s = 16k samples/s

/* SET GAIN CONTROL: */
//Rainbow Six 3/PSO/NUDE packet looks like:
//bmRequestType=0x41, bRequest=0x03, wValue=0x0000 (or 0x0001?), wIndex=0x0001, wLength=0x0000
//Ref https://web.archive.org/web/20200521011406/http://www.kako.com/neta/2005-009/uac3556b.pdf, Section 3.2
//Ref https://www.usb.org/sites/default/files/audio10.pdf Section 5.2.2.4.3.7
//FIXME: Every game I have tried just sets this to zero. What does it actually do?
//FIXME: Is this enabled in the XBLC communcator? How are the levels set? Factory coded?
//FIXME: Do we have other control? MUTE_CONTROL, VOLUME_CONTROL, BASS_CONTROL, MID_CONTROL, TREBLE etc etc (wIndex=2,3,4... etc)

typedef struct {
    uint16_t sample_rate;
    uint16_t bytes_per_frame;
    uint8_t frames_per_extra_sample;  //This is needed when the sample rate does not divide into an integer value.
                                      //i.e for 11025, every 40 frames, send another sample. 1000 / (11025 % 1000) = 40
} xblc_sample_rates_t;

static const xblc_sample_rates_t sample_rates[XBLC_RATE_MAX_NUM] = {
    {8000,  (8000 * 2 / 1000), 0},
    {11025, (11025 * 2 / 1000), 40},
    {16000, (16000 * 2 / 1000), 0},
    {22050, (22050 * 2 / 1000), 20},
    {24000, (24000 * 2 / 1000), 0},
};

static xblc_dev_t xblc_devices[CONFIG_XBLC_MAX_DEV];
static xblc_dev_t *pxblc_list = NULL;
static XBLC_CONN_FUNC *xblc_conn_func = NULL, *xblc_disconn_func = NULL;

static xblc_dev_t *find_xblc_device(UDEV_T *udev) {
    for (int i = 0; i < CONFIG_XBLC_MAX_DEV; i++) {
        if (xblc_devices[i].udev == udev) {
            return &xblc_devices[i];
        }
    }
    return NULL;
}

static xblc_dev_t *alloc_xblc_device(void)
{
    xblc_dev_t *new_xblc = NULL;
    for (int i = 0; i < CONFIG_XBLC_MAX_DEV; i++) {
        if (xblc_devices[i].uid == 0) {
            new_xblc = &xblc_devices[i];
            break;
        }
    }

    if (new_xblc == NULL) {
        return NULL;
    }

    memset(new_xblc, 0, sizeof(xblc_dev_t));
    new_xblc->uid = get_ticks();

    //Chain the new XBLC to the end of the list.
    if (pxblc_list == NULL) {
        pxblc_list = new_xblc;
    }
    else {
        xblc_dev_t *x;
        for (x = pxblc_list; x->next != NULL; x = x->next)
            ;
        x->next = new_xblc;
    }

    return new_xblc;
}

static void free_xblc_device(xblc_dev_t *xblc_dev) {
    xblc_dev_t *head = pxblc_list;

    //If the xblc is at the head of the list, remove now.
    if (xblc_dev == head) {
        pxblc_list = head->next;
        head = NULL;
    }

    //Find the device head in the linked list
    while (head != NULL && head->next != xblc_dev) {
        head = head->next;
    }

    //Remove it from the linked list
    if (head != NULL) {
        xblc_dev_t *new_tail = xblc_dev->next;
        head->next = new_tail;
    }

    //Mark it as free
    memset(xblc_dev, 0, sizeof(xblc_dev_t));
}

static int xblc_probe(IFACE_T *iface) {
    UDEV_T *udev = iface->udev;
    xblc_dev_t *xblc_dev;
    DESC_IF_T *ifd = iface->aif->ifd;

    if (ifd->bInterfaceClass != XBLC_INTERFACE_CLASS || ifd->bInterfaceSubClass != XBLC_INTERFACE_SUBCLASS) {
        return USBH_ERR_NOT_MATCHED;
    }

    //Xbox live communicators have two interfaces (Speaker and Microphone)
    //First check if an interface is already registered under the same device.
    xblc_dev = find_xblc_device(iface->udev);
    if (xblc_dev == NULL) {
        //No existing device is registered, so make a new one.
        xblc_dev = alloc_xblc_device();
        if (xblc_dev == NULL) {
            return USBH_ERR_MEMORY_OUT;
        }
    }

    EP_INFO_T *ep_in = usbh_iface_find_ep(iface, 0, EP_ADDR_DIR_IN | EP_ATTR_TT_ISO);
    EP_INFO_T *ep_out = usbh_iface_find_ep(iface, 0, EP_ADDR_DIR_OUT | EP_ATTR_TT_ISO);
    if (ep_in != NULL) {
        xblc_dev->microphone.iface = iface;
        xblc_dev->microphone.dir = EP_ADDR_DIR_IN;
    }
    if (ep_out != NULL) {
        xblc_dev->speaker.iface = iface;
        xblc_dev->speaker.dir = EP_ADDR_DIR_OUT;
    }

    xblc_dev->udev = iface->udev;
    xblc_dev->idVendor = udev->descriptor.idVendor;
    xblc_dev->idProduct = udev->descriptor.idProduct;
    xblc_dev->next = NULL;
    xblc_dev->user_data = NULL;
    iface->context = (void *)xblc_dev;

    if (xblc_dev->speaker.iface != NULL && xblc_dev->microphone.iface != NULL) {
        USBH_XBLC_DEBUG("XBLC Info: Xbox Live Communicator Connected Speaker iface: %02x, Microphone iface: %02x\n",
                        xblc_dev->speaker.iface->if_num,
                        xblc_dev->microphone.iface->if_num);
        //Set a known default
        usbh_xblc_set_sample_rate(xblc_dev, XBLC_RATE_16000);
        usbh_xblc_set_auto_gain_control(xblc_dev, 0);

        if (xblc_conn_func) {
            xblc_conn_func(xblc_dev, 0);
        }
    }

    return USBH_OK;
}

static void xblc_disconnect(IFACE_T *iface)
{
    xblc_dev_t *xblc_dev = (xblc_dev_t *)(iface->context);
    UTR_T *utr;

    usbh_xblc_stop_audio_out(xblc_dev);
    usbh_xblc_stop_audio_in(xblc_dev);

    USBH_XBLC_DEBUG("XBLC Info: Disconnected device - (vid=0x%x, pid=0x%x), ID %d.\n",
                    xblc_dev->idVendor, xblc_dev->idProduct, xblc_dev->uid);

    if (xblc_disconn_func && xblc_dev->udev != NULL) {
        xblc_disconn_func(xblc_dev, 0);
    }

    free_xblc_device(xblc_dev);
}

UDEV_DRV_T xblc_driver =
    {
        xblc_probe,
        xblc_disconnect,
        NULL, //suspend
        NULL, //resume
};

/**
 * @brief Adds a callback function when an XBLC interface is connected or removed. Pass NULL to remove the callback. 
 * 
 * @param conn_func  The user's connection callback function.
 * @param disconn_func The user's disconnection callback function.
 */
void usbh_install_xblc_conn_callback(XBLC_CONN_FUNC *conn_func, XBLC_CONN_FUNC *disconn_func) {
    xblc_conn_func = conn_func;
    xblc_disconn_func = disconn_func;
}

/**
 * @brief Initialises the XBLC driver with the USB backend.
 * 
 */
void usbh_xblc_init(void) {
    usbh_register_driver(&xblc_driver);
}

/**
 * Returns a pointer to the first connected XBLC device. This is a linked list
 * to all connected XBLC devices.
 * @return A pointer to a xblc_dev_t device.
 */
xblc_dev_t *usbh_xblc_get_device_list(void) {
    return pxblc_list;
}

static void iso_out_irq(UTR_T *utr) {
    xblc_dev_t *xblc_dev = (xblc_dev_t *)utr->context;

    if (xblc_dev == NULL) {
        return;
    }

    if (xblc_dev->speaker.is_streaming == 0) {
        return;
    }

    utr->bIsoNewSched = 0;

    if (xblc_dev->speaker.xfer_cb) {
        uint16_t bpf = sample_rates[xblc_dev->samplerate].bytes_per_frame;
        uint16_t fpas = sample_rates[xblc_dev->samplerate].frames_per_extra_sample;
        uint8_t extra_samples = 0;
        for (int j = 0; j < IF_PER_UTR; j++) {
            if (utr->iso_status[j] != USBH_OK) {
                //An error we actually care about
                if ((utr->iso_status[j] == USBH_ERR_NOT_ACCESS0) || (utr->iso_status[j] == USBH_ERR_NOT_ACCESS1))
                {
                    USBH_XBLC_DEBUG("XBLC Error: Speaker transfer failed\n");
                    utr->bIsoNewSched = 1;
                }
            }

            //If the sample rate doesnt divide into an integer USB frame size, an
            //additional sample is added every fpas frames.
            if (fpas > 0 && extra_samples == 0 && ((utr->iso_sf + j) % fpas) == 0) {
                extra_samples = 1;
            }

            //Reset all the transfer lengths so we keep transferring
            utr->iso_xlen[j] = bpf;
        }

        utr->iso_xlen[IF_PER_UTR - 1] += extra_samples * XBLC_BYTES_PER_SAMPLE;
        xblc_dev->speaker.xfer_cb(xblc_dev, (int16_t *)utr->buff, ((bpf * IF_PER_UTR) / XBLC_BYTES_PER_SAMPLE) + extra_samples);
    }

    if (usbh_iso_xfer(utr) != USBH_OK) {
        USBH_XBLC_DEBUG("XBLC Error: Request of iso transfer failed. Stopping audio stream\n");
        xblc_dev->speaker.is_streaming = 0;
    }
}

static void iso_in_irq(UTR_T *utr)
{
    xblc_dev_t *xblc_dev = (xblc_dev_t *)utr->context;

    if (xblc_dev == NULL) {
        return;
    }

    if (xblc_dev->microphone.is_streaming == 0) {
        return;
    }

    utr->bIsoNewSched = 0;

    if (xblc_dev->microphone.xfer_cb) {
        uint16_t bpf = sample_rates[xblc_dev->samplerate].bytes_per_frame;
        uint16_t fpas = sample_rates[xblc_dev->samplerate].frames_per_extra_sample;
        uint16_t rxlen = 0;
        uint8_t extra_samples = 0;
        for (int j = 0; j < IF_PER_UTR; j++) {
            rxlen += utr->iso_xlen[j];

            if (utr->iso_status[j] == USBH_ERR_NOT_ACCESS0 || utr->iso_status[j] == USBH_ERR_NOT_ACCESS1) {
                USBH_XBLC_DEBUG("XBLC Error: Microphone transfer failed\n");
                utr->bIsoNewSched = 1;
            }

            //Prepare the next transfer while looping though the current transfer
            if (fpas > 0 && extra_samples == 0 && ((utr->iso_sf + j) % fpas) == 0) {
                extra_samples = 1;
            }
            utr->iso_xlen[j] = bpf;
        }
        utr->iso_xlen[IF_PER_UTR - 1] += extra_samples * XBLC_BYTES_PER_SAMPLE;

        if (rxlen > 0) {
            xblc_dev->microphone.xfer_cb(xblc_dev, (int16_t *)utr->buff, rxlen / XBLC_BYTES_PER_SAMPLE);
        }
    }

    if (usbh_iso_xfer(utr) != USBH_OK)
    {
        USBH_XBLC_DEBUG("XBLC Error: Request of iso transfer failed. Stopping audio stream\n");
        xblc_dev->microphone.is_streaming = 0;
    }
}

static int32_t usbh_xblc_stop_stream(xblc_stream_t *stream) {
    UTR_T *utr;

    if (stream->iface == NULL) {
        return USBH_ERR_INVALID_PARAM;
    }

    stream->is_streaming = 0;
    stream->xfer_cb = NULL;

    //Free any running UTRs and buffers
    for (int i = 0; i < XBLC_NUM_BUFFERS; i++) {
        utr = stream->utr[i];
        if (utr != NULL) {
            usbh_quit_utr(utr);
            if (utr->buff) {
                usbh_free_mem(utr->buff, utr->data_len);
            }
            free_utr(utr);
        }
    }
    return USBH_OK;
}

static int32_t usbh_xblc_start_audio_stream(xblc_dev_t *xblc_dev, xblc_stream_t *stream, XBLC_CB_FUNC *xfer_cb)
{
    EP_INFO_T *ep;
    UTR_T *utr;
    uint8_t *buff;
    int ret;

    if (stream->iface == NULL) {
        USBH_XBLC_DEBUG("XBLC Error: Device does not have this stream interface\n");
        return USBH_ERR_INVALID_PARAM;
    }

    if (stream->is_streaming) {
        USBH_XBLC_DEBUG("XBLC Warning: Interface already streaming\n");
        return UAC_RET_IS_STREAMING;
    }

    ep = usbh_iface_find_ep(stream->iface, 0, stream->dir | EP_ATTR_TT_ISO);
    if (ep == NULL) {
        USBH_XBLC_DEBUG("XBLC Error: Could not find iso pipe\n");
        return USBH_ERR_EP_NOT_FOUND;
    }

    //Create a USB transfer request (UTR) for each buffer set by XBLC_NUM_BUFFERS.
    //Each UTR will have enough data for 8 USB Frames (Maximum as per USB spec, 
    //which is set by the USB stack in IF_PER_UTR (Iso frames per UTR)).
    uint16_t bytes_per_frame = sample_rates[xblc_dev->samplerate].bytes_per_frame;
    stream->xfer_cb = xfer_cb;
    for (int i = 0; i < XBLC_NUM_BUFFERS; i++) {
        utr = stream->utr[i];
        utr = alloc_utr(xblc_dev->udev);
        if (utr == NULL) {
            usbh_xblc_stop_stream(stream);
            return USBH_ERR_MEMORY_OUT;
        }
        //Allocate buffer, and add an additional sample for the case where frames need an extra sample added.
        utr->buff = usbh_alloc_mem(bytes_per_frame * IF_PER_UTR + 1 * XBLC_BYTES_PER_SAMPLE);
        if (utr->buff == NULL) {
            usbh_xblc_stop_stream(stream);
            return USBH_ERR_MEMORY_OUT;
        }
        utr->data_len = bytes_per_frame * IF_PER_UTR + 1 * XBLC_BYTES_PER_SAMPLE;
        utr->context = xblc_dev;
        utr->ep = ep;

        //Only the first utr of our buffers should start a new iso schedule.
        //Others on this EP are schedule for latest frames.
        utr->bIsoNewSched = (i == 0);

        //Split the iso buffer over IF_PER_UTR frames equally.
        for (int j = 0; j < IF_PER_UTR; j++) {
            utr->iso_buff[j] = (uint8_t *)((uint32_t)utr->buff + (bytes_per_frame * j));
            utr->iso_xlen[j] = bytes_per_frame;
        }

        if (stream->dir == EP_ADDR_DIR_OUT) {
            //Get initial user data for these frames
            stream->xfer_cb(xblc_dev, (int16_t *)utr->buff, bytes_per_frame * IF_PER_UTR / (XBLC_BYTES_PER_SAMPLE));
            utr->func = iso_out_irq;
        }
        else {
            //Set initial user data for these frames to silence
            memset(utr->buff, 0x00, bytes_per_frame * IF_PER_UTR + 1 * XBLC_BYTES_PER_SAMPLE);
            utr->func = iso_in_irq;
        }

        //Queue the transfer. This will happen after OHCI_ISO_DELAY or EHCI_ISO_DELAY frames if bIsoNewSched == 1
        //Others will just be scheduled on the next immediate frames.
        ret = usbh_iso_xfer(utr);
        if (ret != USBH_OK) {
            USBH_XBLC_DEBUG("XBLC Error: Failed to start UTR %d isochronous-out transfer (%d)", i, ret);
            usbh_xblc_stop_stream(stream);
            return ret;
        }
    }
    stream->is_streaming = 1;
    return USBH_OK;
}

/**
 * @brief Starts an audio out stream to the headset's speaker. `usbh_xblc_set_sample_rate()` should be called to set the sample rate first.
 * 
 * @param xblc_dev Pointer to the XBLC device.
 * @param audio_out_cb The user's audio out callback function with the form `int audio_out_cb(xblc_dev_t *xblc_dev, int16_t *tx_data, int num_samples)`.
 * XBLC expects PCM audio data as s16le format in `tx_data`, num_samples indicates how many s16le samples to send.
 * @return USBH_OK or the error.
 */
int32_t usbh_xblc_start_audio_out(xblc_dev_t *xblc_dev, XBLC_CB_FUNC *audio_out_cb)
{
    return usbh_xblc_start_audio_stream(xblc_dev, &xblc_dev->speaker, audio_out_cb);
}

/**
 * @brief Starts an audio in stream from the headset's microphone. `usbh_xblc_set_sample_rate()` should be called to set the sample rate first.
 * 
 * @param xblc_dev Pointer to the XBLC device.
 * @param audio_in_cb The user's audio in callback function with the form `int audio_in_cb(xblc_dev_t *xblc_dev, int16_t *rx_data, int num_samples)`.
 * XBLC returns PCM audio data as s16le format in `rx_data`, num_samples indicates how many s16le samples were received.
 * @return USBH_OK or the error.
 */
int32_t usbh_xblc_start_audio_in(xblc_dev_t *xblc_dev, XBLC_CB_FUNC *audio_in_cb)
{
    return usbh_xblc_start_audio_stream(xblc_dev, &xblc_dev->microphone, audio_in_cb);
}

/**
 * @brief Stops the audio out stream to the headset's speaker.
 * 
 * @param xblc_dev Pointer to the XBLC device.
 * @return USBH_OK or the error.
 */
int32_t usbh_xblc_stop_audio_out(xblc_dev_t *xblc_dev)
{
    return usbh_xblc_stop_stream(&xblc_dev->speaker);
}

/**
 * @brief Stops the audio in stream from the headset's microphone.
 * 
 * @param xblc_dev Pointer to the XBLC device.
 * @return USBH_OK or the error.
 */
int32_t usbh_xblc_stop_audio_in(xblc_dev_t *xblc_dev)
{
    return usbh_xblc_stop_stream(&xblc_dev->microphone);
}

/**
 * @brief Set the sample rate of the headset's microphone and speaker. Note, the speaker and microphone always have to same sample rate.
 * 
 * @param xblc_dev Pointer to the XBLC device.
 * @param rate The sample rate.
 * @return USBH_OK or the error.
 */
int32_t usbh_xblc_set_sample_rate(xblc_dev_t *xblc_dev, xblc_samplerate rate)
{
    uint32_t xfer_len;
    int32_t ret = usbh_ctrl_xfer(xblc_dev->udev,
                                 REQ_TYPE_OUT | REQ_TYPE_VENDOR_DEV | REQ_TYPE_TO_IFACE, //bmRequestType
                                 USB_REQ_SET_FEATURE,                                    //bRequest
                                 0x0100 | (uint16_t)rate,                                //wValue
                                 XBLC_SET_SAMPLE_RATE,                                   //wIndex
                                 0,                                                      //wLength
                                 NULL, &xfer_len, 100);

    if (ret == USBH_OK) {
        xblc_dev->samplerate = rate;
    }
    else {
        USBH_XBLC_DEBUG("XBLC Error: Could not set sample rate on dev ID %d\n", xblc_dev->uid);
    }

    return ret;
}

/**
 * @brief Enable or disable auto gain control on the headset microphone.
 * Ref https://web.archive.org/web/20200521011406/http://www.kako.com/neta/2005-009/uac3556b.pdf, Section 3.2
 * FIXME: Is this enabled in the XBLC communcator? How are the levels set? Factory coded?
 * @param xblc_dev Pointer to the XBLC device.
 * @param state 0 = off, 1 = on.
 * @return USBH_OK or the error.
 */
int32_t usbh_xblc_set_auto_gain_control(xblc_dev_t *xblc_dev, uint16_t state)
{
    uint32_t xfer_len;
    int32_t ret = usbh_ctrl_xfer(xblc_dev->udev,
                                 REQ_TYPE_OUT | REQ_TYPE_VENDOR_DEV | REQ_TYPE_TO_IFACE, //bmRequestType
                                 USB_REQ_SET_FEATURE,                                    //bRequest
                                 (state) ? 0x0001 : 0x0000,                              //wValue
                                 XBLC_SET_AUTO_GAIN_CONTROL,                             //wIndex
                                 0,                                                      //wLength
                                 NULL, &xfer_len, 100);

    if (ret == USBH_OK) {
        xblc_dev->auto_gain_control = (state) ? 0x0001 : 0x0000;
    }
    else {
        USBH_XBLC_DEBUG("XBLC Error: Could not set auto gain control on dev ID %d\n", xblc_dev->uid);
    }

    return ret;
}

/**
 * @brief Get the controller port the headset is plugged into.
 * @param xblc_dev Pointer to the XBLC device.
 * @return 0 for Player 1, 1 for Player 2 etc.
 */
uint8_t usbh_xblc_get_port(xblc_dev_t *xblc_dev)
{
    if (xblc_dev->udev == NULL) {
        //Device is disconnected.
        return 0xFF;   
    }
    //Need to get the root hub port as we're probably
    //connected to a downstream USB hub.
    HUB_DEV_T *parent = xblc_dev->udev->parent;
    while (1) {
        if (parent->iface->udev == NULL) {
            //Hub upstream as been disconnected
            return 0xFF;   
        }
        if (parent->iface->udev->parent == NULL) {
            break;
        }
        parent = parent->iface->udev->parent;
    }
    uint8_t port = parent->iface->udev->port_num - 1;

    //Convert 2,3,0,1 to 0,1,2,3
    port ^= 2;

    return port;
}

/**
 * @brief Get the currently set sample rate
 * @param xblc_dev Pointer to the XBLC device.
 * @return The rate is samples per second. i.e 16000.
 */
int32_t usbh_xblc_get_sample_rate(xblc_dev_t *xblc_dev) {
    return sample_rates[xblc_dev->samplerate].sample_rate;
}
