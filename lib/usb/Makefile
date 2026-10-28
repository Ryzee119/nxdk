USBH_DIR         = $(NXDK_DIR)/lib/usb

# TinyUSB is pinned to a commit after v0.21.0: v0.21.0's hub driver keeps the
# port status completion callback in a single global, which loses callbacks
# when more than one hub has a port status request in flight (the Xbox v1.0
# daughterboard + an external hub, or xemu's USB setup).
TINYUSB_DIR      = $(USBH_DIR)/tinyusb
TINYUSB_PORT_DIR = $(USBH_DIR)/tinyusb_xbox

# USB Host core (TinyUSB). The Xbox OHCI host controller driver is compiled
# from tinyusb_xbox/hcd_ohci_xbox.c, which also implements the OSAL backing
# functions (physical <-> virtual translations) for the host stack.
USB_CORE_SRCS := \
	$(TINYUSB_DIR)/src/tusb.c \
	$(TINYUSB_DIR)/src/common/tusb_fifo.c \
	$(TINYUSB_DIR)/src/host/usbh.c \
	$(TINYUSB_DIR)/src/host/hub.c \
	$(TINYUSB_PORT_DIR)/hcd_ohci_xbox.c \
	$(TINYUSB_PORT_DIR)/debug_xbox.c

USBH_OBJS = $(addsuffix .obj, $(basename $(USB_CORE_SRCS)))

# WinUSB compatibility layer, implemented on top of the TinyUSB host stack
WINUSB_SRCS := \
	$(USBH_DIR)/winusb/winusb_device.c \
	$(USBH_DIR)/winusb/winusb_descriptors.c \
	$(USBH_DIR)/winusb/winusb_pipes.c \
	$(USBH_DIR)/winusb/winusb_policy.c \
	$(USBH_DIR)/winusb/winusb_isoch.c \
	$(USBH_DIR)/winusb/winusb_timesync.c \
	$(USBH_DIR)/winusb/winusb_xfer.c

USBH_OBJS += $(addsuffix .obj, $(basename $(WINUSB_SRCS)))

$(NXDK_DIR)/lib/nxdk_usb.lib: $(USBH_OBJS)

# TinyUSB detects GCC style compilers (attribute macros, endianness) through
# __GNUC__. nxdk's clang uses a win32 target and defines _MSC_VER instead, so
# advertise the GNU compatible attribute support TinyUSB relies on.
NXDK_CFLAGS   += -D__GNUC__=4 -D__GNUC_MINOR__=2 -D__GNUC_PATCHLEVEL__=0
NXDK_CXXFLAGS += -D__GNUC__=4 -D__GNUC_MINOR__=2 -D__GNUC_PATCHLEVEL__=0

NXDK_CFLAGS   += -I$(TINYUSB_DIR)/src -I$(TINYUSB_PORT_DIR) -I$(USBH_DIR)/winusb
NXDK_CXXFLAGS += -I$(TINYUSB_DIR)/src -I$(TINYUSB_PORT_DIR) -I$(USBH_DIR)/winusb

main.exe: $(NXDK_DIR)/lib/nxdk_usb.lib

CLEANRULES += clean-usb

.PHONY: clean-usb
clean-usb:
	$(VE)rm -f $(USBH_OBJS) $(NXDK_DIR)/lib/nxdk_usb.lib
