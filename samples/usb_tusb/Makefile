XBE_TITLE = usb_test
GEN_XISO = $(XBE_TITLE).iso

USBH_SRC_DIR = $(CURDIR)/tinyusb_src/src

SRCS = \
	$(CURDIR)/main.c \
	$(CURDIR)/xinput_host.c \
	$(USBH_SRC_DIR)/tusb.c \
	$(USBH_SRC_DIR)/common/tusb_fifo.c \
	$(USBH_SRC_DIR)/host/hub.c \
	$(USBH_SRC_DIR)/host/usbh_control.c \
	$(USBH_SRC_DIR)/host/usbh.c \
	$(USBH_SRC_DIR)/portable/ohci/ohci.c \
	$(USBH_SRC_DIR)/class/hid/hid_host.c

CFLAGS += -I. \
		-I$(USBH_SRC_DIR) \
		-DCFG_TUSB_MCU=OPT_MCU_LPC40XX \
		-DCFG_TUSB_DEBUG_PRINTF="tusb_printf_hook" \
		-DCFG_TUSB_DEBUG=2 \
		-DDEBUG_CONSOLE \
		-DCFG_TUSB_OS=OPT_OS_CUSTOM \
		-DCFG_TUH_TASK_QUEUE_SZ=32 \
		-O2

NXDK_DIR ?= $(CURDIR)/nxdk

include $(NXDK_DIR)/Makefile
