#ifndef __WINUSBIO_H
#define __WINUSBIO_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _USBD_PIPE_TYPE {
  UsbdPipeTypeControl,
  UsbdPipeTypeIsochronous,
  UsbdPipeTypeBulk,
  UsbdPipeTypeInterrupt
} USBD_PIPE_TYPE;

typedef struct _WINUSB_PIPE_INFORMATION {
  USBD_PIPE_TYPE PipeType;
  UCHAR PipeId;
  USHORT MaximumPacketSize;
  UCHAR Interval;
} WINUSB_PIPE_INFORMATION, *PWINUSB_PIPE_INFORMATION;

typedef struct _WINUSB_PIPE_INFORMATION_EX {
  USBD_PIPE_TYPE PipeType;
  UCHAR PipeId;
  USHORT MaximumPacketSize;
  UCHAR Interval;
  ULONG MaximumBytesPerInterval;
} WINUSB_PIPE_INFORMATION_EX, *PWINUSB_PIPE_INFORMATION_EX;

/* constants for WinUsb_Get/SetPipePolicy.  */
#define SHORT_PACKET_TERMINATE 0x01
#define AUTO_CLEAR_STALL       0x02
#define PIPE_TRANSFER_TIMEOUT  0x03
#define IGNORE_SHORT_PACKETS   0x04
#define ALLOW_PARTIAL_READS    0x05
#define AUTO_FLUSH             0x06
#define RAW_IO                 0x07
#define MAXIMUM_TRANSFER_SIZE 0x08
#define RESET_PIPE_ON_RESUME 0x09

/* constants for WinUsb_Get/SetPowerPolicy.  */
#define AUTO_SUSPEND  0x81
#define ENABLE_WAKE   0x82
#define SUSPEND_DELAY 0x83

/* constants for WinUsb_QueryDeviceInformation.  */
#define DEVICE_SPEED 0x01
#define LowSpeed     0x01
#define FullSpeed    0x02
#define HighSpeed    0x03

#ifdef __cplusplus
}
#endif

#endif