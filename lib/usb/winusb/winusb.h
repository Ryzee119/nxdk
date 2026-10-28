/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * WinUSB client library for nxdk, implemented on top of the TinyUSB host
 * stack. The API matches winusb.dll so that programs written for WinUSB on
 * Windows can be built for the Xbox with minimal changes.
 *
 * Platform difference (device handles):
 *   On Windows a WinUSB device handle is a file handle obtained with
 *   CreateFile() on a device interface path returned by SetupAPI. nxdk has no
 *   SetupAPI or USB device namespace, so the equivalent is:
 *
 *       HANDLE device = WinUsbOpenDevice("\\\\?\\usb#vid_045e&pid_0202#...");
 *       WINUSB_INTERFACE_HANDLE winusb;
 *       WinUsb_Initialize(device, &winusb);
 *
 *   WinUsbOpenDevice() accepts Windows style device interface paths (it
 *   extracts VID/PID/interface), see the documentation below. Everything after
 *   WinUsb_Initialize() is identical to Windows.
 *
 * The USB host stack is started on first use. When WinUSB owns the stack it
 * also runs the TinyUSB task in an internal thread, so applications only need
 * to call the WinUSB functions. If the application initialises TinyUSB itself
 * (tusb_init()), it must keep calling tuh_task() from its own thread instead.
 */

#ifndef _WINUSB_H_
#define _WINUSB_H_

#include <windows.h>

/* SAL annotations are not available in nxdk headers */
#ifndef _In_
#define _In_
#define _Out_
#define _Inout_
#define _In_opt_
#define _Out_opt_
#define _Inout_opt_
#define _Out_writes_(x)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Opaque WinUSB Handles
 */
typedef PVOID WINUSB_INTERFACE_HANDLE, *PWINUSB_INTERFACE_HANDLE;
typedef PVOID WINUSB_ISOCH_BUFFER_HANDLE, *PWINUSB_ISOCH_BUFFER_HANDLE;

/*
 * USBD Pipe Types
 */
typedef enum _USBD_PIPE_TYPE {
    UsbdPipeTypeControl = 0,
    UsbdPipeTypeIsochronous = 1,
    UsbdPipeTypeBulk = 2,
    UsbdPipeTypeInterrupt = 3
} USBD_PIPE_TYPE;

/*
 * Pipe Policy Types
 */
#define SHORT_PACKET_TERMINATE       0x01
#define AUTO_CLEAR_STALL             0x02
#define PIPE_TRANSFER_TIMEOUT        0x03
#define IGNORE_SHORT_PACKETS         0x04
#define ALLOW_PARTIAL_READS          0x05
#define AUTO_FLUSH                   0x06
#define RAW_IO                       0x07
#define MAXIMUM_TRANSFER_SIZE        0x08
#define RESET_PIPE_ON_RESUME         0x09

/*
 * Power Policy Types
 */
#define AUTO_SUSPEND                 0x81
#define SUSPEND_DELAY                0x82

/*
 * Device Information Types
 */
#define DEVICE_SPEED                 0x01

/* Device Speeds */
#define LowSpeed                     0x01
#define FullSpeed                    0x02
#define HighSpeed                    0x03

/*
 * Standard USB Descriptors
 */
#pragma pack(push, 1)

typedef struct _USB_COMMON_DESCRIPTOR {
    UCHAR bLength;
    UCHAR bDescriptorType;
} USB_COMMON_DESCRIPTOR, *PUSB_COMMON_DESCRIPTOR;

typedef struct _USB_DEVICE_DESCRIPTOR {
    UCHAR  bLength;
    UCHAR  bDescriptorType;
    USHORT bcdUSB;
    UCHAR  bDeviceClass;
    UCHAR  bDeviceSubClass;
    UCHAR  bDeviceProtocol;
    UCHAR  bMaxPacketSize0;
    USHORT idVendor;
    USHORT idProduct;
    USHORT bcdDevice;
    UCHAR  iManufacturer;
    UCHAR  iProduct;
    UCHAR  iSerialNumber;
    UCHAR  bNumConfigurations;
} USB_DEVICE_DESCRIPTOR, *PUSB_DEVICE_DESCRIPTOR;

typedef struct _USB_CONFIGURATION_DESCRIPTOR {
    UCHAR  bLength;
    UCHAR  bDescriptorType;
    USHORT wTotalLength;
    UCHAR  bNumInterfaces;
    UCHAR  bConfigurationValue;
    UCHAR  iConfiguration;
    UCHAR  bmAttributes;
    UCHAR  MaxPower;
} USB_CONFIGURATION_DESCRIPTOR, *PUSB_CONFIGURATION_DESCRIPTOR;

typedef struct _USB_INTERFACE_DESCRIPTOR {
    UCHAR bLength;
    UCHAR bDescriptorType;
    UCHAR bInterfaceNumber;
    UCHAR bAlternateSetting;
    UCHAR bNumEndpoints;
    UCHAR bInterfaceClass;
    UCHAR bInterfaceSubClass;
    UCHAR bInterfaceProtocol;
    UCHAR iInterface;
} USB_INTERFACE_DESCRIPTOR, *PUSB_INTERFACE_DESCRIPTOR;

typedef struct _USB_ENDPOINT_DESCRIPTOR {
    UCHAR  bLength;
    UCHAR  bDescriptorType;
    UCHAR  bEndpointAddress;
    UCHAR  bmAttributes;
    USHORT wMaxPacketSize;
    UCHAR  bInterval;
} USB_ENDPOINT_DESCRIPTOR, *PUSB_ENDPOINT_DESCRIPTOR;

typedef struct _WINUSB_SETUP_PACKET {
    UCHAR  RequestType;
    UCHAR  Request;
    USHORT Value;
    USHORT Index;
    USHORT Length;
} WINUSB_SETUP_PACKET, *PWINUSB_SETUP_PACKET;

#pragma pack(pop)

/*
 * WinUSB Pipe Information Structures
 */
typedef struct _WINUSB_PIPE_INFORMATION {
    USBD_PIPE_TYPE PipeType;
    UCHAR          PipeId;
    USHORT         MaximumPacketSize;
    UCHAR          Interval;
} WINUSB_PIPE_INFORMATION, *PWINUSB_PIPE_INFORMATION;

typedef struct _WINUSB_PIPE_INFORMATION_EX {
    USBD_PIPE_TYPE PipeType;
    UCHAR          PipeId;
    USHORT         MaximumPacketSize;
    UCHAR          Interval;
    ULONG          MaximumBytesPerInterval;
} WINUSB_PIPE_INFORMATION_EX, *PWINUSB_PIPE_INFORMATION_EX;

/*
 * Isochronous Packet Descriptor
 */
typedef struct _USBD_ISO_PACKET_DESCRIPTOR {
    ULONG Offset;
    ULONG Length;
    ULONG Status;
} USBD_ISO_PACKET_DESCRIPTOR, *PUSBD_ISO_PACKET_DESCRIPTOR;

/*
 * Time Synchronization Information Structures
 */
typedef struct _USB_FRAME_NUMBER_AND_QPC_FOR_TIME_SYNC_INFORMATION {
    HANDLE         TimeSyncHandle;
    ULONG          CurrentQueryFrameNumber;
    ULONG          CurrentQueryQpcValue;
    LARGE_INTEGER  QueryPerformanceCounterFrequency;
    LARGE_INTEGER  QueryPerformanceCounterValue;
    LARGE_INTEGER  SystemTime;
    ULONG          FrameNumberFromHal;
    ULONG          Flags;
} USB_FRAME_NUMBER_AND_QPC_FOR_TIME_SYNC_INFORMATION, *PUSB_FRAME_NUMBER_AND_QPC_FOR_TIME_SYNC_INFORMATION;

typedef struct _USB_START_TRACKING_FOR_TIME_SYNC_INFORMATION {
    HANDLE        TimeSyncHandle;
    ULONG         TargetFrameNumber;
    LARGE_INTEGER TargetQpcValue;
} USB_START_TRACKING_FOR_TIME_SYNC_INFORMATION, *PUSB_START_TRACKING_FOR_TIME_SYNC_INFORMATION;

typedef struct _USB_STOP_TRACKING_FOR_TIME_SYNC_INFORMATION {
    HANDLE TimeSyncHandle;
} USB_STOP_TRACKING_FOR_TIME_SYNC_INFORMATION, *PUSB_STOP_TRACKING_FOR_TIME_SYNC_INFORMATION;

/*
 * nxdk platform helper: opens a USB device and returns a handle suitable for
 * WinUsb_Initialize(). Accepted DevicePath formats (case insensitive):
 *
 *   \\?\usb#vid_045e&pid_0202#5&2a1b3c4d&0&1#{guid}  Windows device path
 *   usb#vid_045e&pid_0202#...                        without the \\?\ prefix
 *   USB\VID_045E&PID_0202\...                        device instance path
 *   vid_045e&pid_0202                                VID/PID selector
 *
 * An optional "&mi_0X" selects the interface (defaults to the first one) and
 * an optional "&index=N" selects the Nth device when several devices share
 * the same VID/PID (also accepted without the leading '&').
 *
 * Returns NULL on failure (call GetLastError()).
 */
HANDLE WINAPI WinUsbOpenDevice(
    _In_ LPCSTR DevicePath
);

/*
 * nxdk platform helper: closes a handle returned by WinUsbOpenDevice().
 * This is the equivalent of CloseHandle() on the Windows file handle.
 */
BOOL WINAPI WinUsbCloseDevice(
    _In_ HANDLE DeviceHandle
);

/*
 * Official WinUSB API
 */

BOOL WINAPI WinUsb_Initialize(
    _In_  HANDLE                   DeviceHandle,
    _Out_ PWINUSB_INTERFACE_HANDLE InterfaceHandle
);

BOOL WINAPI WinUsb_Free(
    _In_ WINUSB_INTERFACE_HANDLE InterfaceHandle
);

BOOL WINAPI WinUsb_GetAssociatedInterface(
    _In_  WINUSB_INTERFACE_HANDLE  InterfaceHandle,
    _In_  UCHAR                    AssociatedInterfaceIndex,
    _Out_ PWINUSB_INTERFACE_HANDLE AssociatedInterfaceHandle
);

BOOL WINAPI WinUsb_GetDescriptor(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  UCHAR                   DescriptorType,
    _In_  UCHAR                   Index,
    _In_  USHORT                  LanguageID,
    _Out_ PUCHAR                  Buffer,
    _In_  ULONG                   BufferLength,
    _Out_ PULONG                  LengthTransferred
);

BOOL WINAPI WinUsb_QueryInterfaceSettings(
    _In_  WINUSB_INTERFACE_HANDLE   InterfaceHandle,
    _In_  UCHAR                    AlternateInterfaceNumber,
    _Out_ PUSB_INTERFACE_DESCRIPTOR UsbAltInterfaceDescriptor
);

BOOL WINAPI WinUsb_QueryDeviceInformation(
    _In_    WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_    ULONG                   InformationType,
    _Inout_ PULONG                  BufferLength,
    _Out_   PVOID                   Buffer
);

BOOL WINAPI WinUsb_SetCurrentAlternateSetting(
    _In_ WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_ UCHAR                   SettingNumber
);

BOOL WINAPI WinUsb_GetCurrentAlternateSetting(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _Out_ PUCHAR                  SettingNumber
);

BOOL WINAPI WinUsb_QueryPipe(
    _In_  WINUSB_INTERFACE_HANDLE  InterfaceHandle,
    _In_  UCHAR                    AlternateInterfaceNumber,
    _In_  UCHAR                    PipeIndex,
    _Out_ PWINUSB_PIPE_INFORMATION PipeInformation
);

BOOL WINAPI WinUsb_QueryPipeEx(
    _In_  WINUSB_INTERFACE_HANDLE     InterfaceHandle,
    _In_  UCHAR                       AlternateSettingNumber,
    _In_  UCHAR                       PipeIndex,
    _Out_ PWINUSB_PIPE_INFORMATION_EX PipeInformationEx
);

BOOL WINAPI WinUsb_SetPipePolicy(
    _In_ WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_ UCHAR                   PipeID,
    _In_ ULONG                   PolicyType,
    _In_ ULONG                   ValueLength,
    _In_ PVOID                   Value
);

BOOL WINAPI WinUsb_GetPipePolicy(
    _In_    WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_    UCHAR                   PipeID,
    _In_    ULONG                   PolicyType,
    _Inout_ PULONG                  ValueLength,
    _Out_   PVOID                   Value
);

BOOL WINAPI WinUsb_ReadPipe(
    _In_      WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_      UCHAR                   PipeID,
    _Out_     PUCHAR                  Buffer,
    _In_      ULONG                   BufferLength,
    _Out_opt_ PULONG                  LengthTransferred,
    _Inout_opt_ LPOVERLAPPED          Overlapped
);

BOOL WINAPI WinUsb_WritePipe(
    _In_      WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_      UCHAR                   PipeID,
    _In_      PUCHAR                  Buffer,
    _In_      ULONG                   BufferLength,
    _Out_opt_ PULONG                  LengthTransferred,
    _Inout_opt_ LPOVERLAPPED          Overlapped
);

BOOL WINAPI WinUsb_ControlTransfer(
    _In_      WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_      WINUSB_SETUP_PACKET     SetupPacket,
    _Inout_   PUCHAR                  Buffer,
    _In_      ULONG                   BufferLength,
    _Out_opt_ PULONG                  LengthTransferred,
    _Inout_opt_ LPOVERLAPPED          Overlapped
);

BOOL WINAPI WinUsb_ResetPipe(
    _In_ WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_ UCHAR                   PipeID
);

BOOL WINAPI WinUsb_AbortPipe(
    _In_ WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_ UCHAR                   PipeID
);

BOOL WINAPI WinUsb_FlushPipe(
    _In_ WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_ UCHAR                   PipeID
);

BOOL WINAPI WinUsb_SetPowerPolicy(
    _In_ WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_ ULONG                   PolicyType,
    _In_ ULONG                   ValueLength,
    _In_ PVOID                   Value
);

BOOL WINAPI WinUsb_GetPowerPolicy(
    _In_    WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_    ULONG                   PolicyType,
    _Inout_ PULONG                  ValueLength,
    _Out_   PVOID                   Buffer
);

BOOL WINAPI WinUsb_GetOverlappedResult(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  LPOVERLAPPED            lpOverlapped,
    _Out_ LPDWORD                 lpNumberOfBytesTransferred,
    _In_  BOOL                    bWait
);

PUSB_COMMON_DESCRIPTOR WINAPI WinUsb_ParseConfigurationDescriptor(
    _In_ PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    _In_ PVOID                         StartPosition,
    _In_ LONG                          InterfaceNumber,
    _In_ LONG                          AlternateSetting,
    _In_ LONG                          InterfaceClass,
    _In_ LONG                          InterfaceSubClass,
    _In_ LONG                          InterfaceProtocol
);

PUSB_COMMON_DESCRIPTOR WINAPI WinUsb_ParseDescriptors(
    _In_ PVOID DescriptorBuffer,
    _In_ ULONG TotalLength,
    _In_ PVOID StartPosition,
    _In_ LONG  DescriptorType
);

BOOL WINAPI WinUsb_GetCurrentFrameNumber(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _Out_ PULONG                  CurrentFrameNumber,
    _Out_ LARGE_INTEGER          *TimeStamp
);


BOOL WINAPI WinUsb_RegisterIsochBuffer(
    _In_  WINUSB_INTERFACE_HANDLE      InterfaceHandle,
    _In_  UCHAR                        PipeID,
    _Inout_ PUCHAR                     Buffer,
    _In_  ULONG                        BufferLength,
    _Out_ PWINUSB_ISOCH_BUFFER_HANDLE  IsochBufferHandle
);

BOOL WINAPI WinUsb_UnregisterIsochBuffer(
    _In_ WINUSB_ISOCH_BUFFER_HANDLE IsochBufferHandle
);

BOOL WINAPI WinUsb_WriteIsochPipe(
    _In_      WINUSB_ISOCH_BUFFER_HANDLE BufferHandle,
    _In_      ULONG                      Offset,
    _In_      ULONG                      Length,
    _Inout_   PULONG                     FrameNumber,
    _Inout_opt_ LPOVERLAPPED             Overlapped
);

BOOL WINAPI WinUsb_ReadIsochPipe(
    _In_      WINUSB_ISOCH_BUFFER_HANDLE BufferHandle,
    _In_      ULONG                      Offset,
    _In_      ULONG                      Length,
    _Inout_   PULONG                     FrameNumber,
    _In_      ULONG                      NumberOfPackets,
    _Out_writes_(NumberOfPackets) PUSBD_ISO_PACKET_DESCRIPTOR IsoPacketDescriptors,
    _Inout_opt_ LPOVERLAPPED             Overlapped
);

BOOL WINAPI WinUsb_WriteIsochPipeAsap(
    _In_      WINUSB_ISOCH_BUFFER_HANDLE BufferHandle,
    _In_      ULONG                      Offset,
    _In_      ULONG                      Length,
    _In_      BOOL                       ContinueStream,
    _Inout_opt_ LPOVERLAPPED             Overlapped
);

BOOL WINAPI WinUsb_ReadIsochPipeAsap(
    _In_      WINUSB_ISOCH_BUFFER_HANDLE BufferHandle,
    _In_      ULONG                      Offset,
    _In_      ULONG                      Length,
    _In_      BOOL                       ContinueStream,
    _In_      ULONG                      NumberOfPackets,
    _Out_writes_(NumberOfPackets) PUSBD_ISO_PACKET_DESCRIPTOR IsoPacketDescriptors,
    _Inout_opt_ LPOVERLAPPED             Overlapped
);

BOOL WINAPI WinUsb_ResetPipeAsync(
    _In_        WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_        UCHAR                   PipeID,
    _Inout_opt_ LPOVERLAPPED            Overlapped
);

BOOL WINAPI WinUsb_AbortPipeAsync(
    _In_        WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_        UCHAR                   PipeID,
    _Inout_opt_ LPOVERLAPPED            Overlapped
);

BOOL WINAPI WinUsb_SetCurrentAlternateSettingAsync(
    _In_        WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_        UCHAR                   SettingNumber,
    _Inout_opt_ LPOVERLAPPED            Overlapped
);

BOOL WINAPI WinUsb_GetCurrentFrameNumberAndQpc(
    _In_  WINUSB_INTERFACE_HANDLE                             InterfaceHandle,
    _Out_ PUSB_FRAME_NUMBER_AND_QPC_FOR_TIME_SYNC_INFORMATION FrameNumberAndQpcInfo
);

BOOL WINAPI WinUsb_StartTrackingForTimeSync(
    _In_ WINUSB_INTERFACE_HANDLE                      InterfaceHandle,
    _In_ PUSB_START_TRACKING_FOR_TIME_SYNC_INFORMATION StartTrackingInfo
);

BOOL WINAPI WinUsb_StopTrackingForTimeSync(
    _In_ WINUSB_INTERFACE_HANDLE                     InterfaceHandle,
    _In_ PUSB_STOP_TRACKING_FOR_TIME_SYNC_INFORMATION StopTrackingInfo
);

#ifdef __cplusplus
}
#endif

#endif /* _WINUSB_H_ */
