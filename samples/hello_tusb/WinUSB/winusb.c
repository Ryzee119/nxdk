#include <windows.h>
#include <winusb.h>
#include <assert.h>

BOOL WinUsb_AbortPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_ControlTransfer(WINUSB_INTERFACE_HANDLE InterfaceHandle, WINUSB_SETUP_PACKET SetupPacket, PUCHAR Buffer, ULONG BufferLength, PULONG LengthTransferred, LPOVERLAPPED Overlapped) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_FlushPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_Free(WINUSB_INTERFACE_HANDLE InterfaceHandle) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_GetAdjustedFrameNumber(PULONG CurrentFrameNumber, LARGE_INTEGER TimeStamp) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_GetAssociatedInterface(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR AssociatedInterfaceIndex, PWINUSB_INTERFACE_HANDLE AssociatedInterfaceHandle) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_GetCurrentAlternateSetting(WINUSB_INTERFACE_HANDLE InterfaceHandle, PUCHAR SettingNumber) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_GetCurrentFrameNumber(WINUSB_INTERFACE_HANDLE InterfaceHandle, PULONG CurrentFrameNumber, LARGE_INTEGER* TimeStamp) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_GetDescriptor(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR DescriptorType, UCHAR Index, USHORT LanguageID, PUCHAR Buffer, ULONG BufferLength, PULONG LengthTransferred) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_GetOverlappedResult(WINUSB_INTERFACE_HANDLE InterfaceHandle, LPOVERLAPPED lpOverlapped, LPDWORD lpNumberOfBytesTransferred, BOOL bWait) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_GetPipePolicy(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, ULONG PolicyType, PULONG ValueLength, PVOID Value) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_GetPowerPolicy(WINUSB_INTERFACE_HANDLE InterfaceHandle, ULONG PolicyType, PULONG ValueLength, PVOID Value) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_Initialize(HANDLE DeviceHandle, PWINUSB_INTERFACE_HANDLE InterfaceHandle) {
    assert(0);
    return TRUE;
}

PUSB_INTERFACE_DESCRIPTOR WinUsb_ParseConfigurationDescriptor(PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor, PVOID StartPosition, LONG InterfaceNumber, LONG AlternateSetting, LONG InterfaceClass, LONG InterfaceSubClass, LONG InterfaceProtocol) {
    assert(0);
    return NULL;
}

PUSB_COMMON_DESCRIPTOR WinUsb_ParseDescriptors(PVOID DescriptorBuffer, ULONG TotalLength, PVOID StartPosition, LONG DescriptorType) {
    assert(0);
    return NULL;
}

BOOL WinUsb_ReadIsochPipe(WINUSB_ISOCH_BUFFER_HANDLE BufferHandle, ULONG Offset, ULONG Length, PULONG FrameNumber, ULONG NumberOfPackets, PUSBD_ISO_PACKET_DESCRIPTOR IsoPacketDescriptors, LPOVERLAPPED Overlapped) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_ReadIsochPipeAsap(WINUSB_ISOCH_BUFFER_HANDLE BufferHandle, ULONG Offset, ULONG Length, BOOL ContinueStream, ULONG NumberOfPackets, PUSBD_ISO_PACKET_DESCRIPTOR IsoPacketDescriptors, LPOVERLAPPED Overlapped) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_ReadPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, PUCHAR Buffer, ULONG BufferLength, PULONG LengthTransferred, LPOVERLAPPED Overlapped) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_RegisterIsochBuffer(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, PUCHAR Buffer, ULONG BufferLength, PWINUSB_ISOCH_BUFFER_HANDLE IsochBufferHandle) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_QueryDeviceInformation(WINUSB_INTERFACE_HANDLE InterfaceHandle, ULONG InformationType, PULONG BufferLength, PVOID Buffer) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_QueryInterfaceSettings(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR AlternateInterfaceNumber, PUSB_INTERFACE_DESCRIPTOR UsbAltInterfaceDescriptor) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_QueryPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR AlternateInterfaceNumber, UCHAR PipeIndex, PWINUSB_PIPE_INFORMATION PipeInformation) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_QueryPipeEx(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR AlternateInterfaceNumber, UCHAR PipeIndex, PWINUSB_PIPE_INFORMATION_EX PipeInformationEx) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_ResetPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_SetCurrentAlternateSetting(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR SettingNumber) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_SetPipePolicy(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, ULONG PolicyType, ULONG ValueLength, PVOID Value) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_SetPowerPolicy(WINUSB_INTERFACE_HANDLE InterfaceHandle, ULONG PolicyType, ULONG ValueLength, PVOID Value) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_UnregisterIsochBuffer(WINUSB_ISOCH_BUFFER_HANDLE IsochBufferHandle) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_WriteIsochPipe(WINUSB_ISOCH_BUFFER_HANDLE BufferHandle, ULONG Offset, ULONG Length, PULONG FrameNumber, LPOVERLAPPED Overlapped) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_WriteIsochPipeAsap(WINUSB_ISOCH_BUFFER_HANDLE BufferHandle, ULONG Offset, ULONG Length, BOOL ContinueStream, LPOVERLAPPED Overlapped) {
    assert(0);
    return TRUE;
}

BOOL WinUsb_WritePipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, PUCHAR Buffer, ULONG BufferLength, PULONG LengthTransferred, LPOVERLAPPED Overlapped) {
    assert(0);
    return TRUE;
}
