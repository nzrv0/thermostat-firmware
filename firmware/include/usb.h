/*
 * usb.h - the USB device: a CDC-ACM virtual COM port.
 *
 * The USB block of the shipped firmware was recovered from the image:
 *
 *   - The descriptor set at 0x08084938 is a CDC-ACM device:
 *       DEVICE     bcdUSB 0x0200, idVendor 0x0483, idProduct 0x5720,
 *                  bcdDevice 0x0200, iManufacturer 1, iProduct 2,
 *                  iSerialNumber 3, one configuration.
 *       CONFIG     32 bytes, one interface, bmAttributes 0xc0,
 *                  bMaxPower 0x32 (50 mA).
 *       INTERFACE  0: class 0x08 (Communications), subclass 0x06 (ACM),
 *                  protocol 0x50 (CDC), two endpoints.
 *       ENDPOINT   0x81 bulk 64 bytes in, 0x02 bulk 64 bytes out.
 *       STRINGS    "Heatmiser" (1), "Accent" (2), "ST Mass" (3).
 *
 *   - The peripheral registers used are the F1 USBP block:
 *       USB_IMR 0x40005c40, USB_ISTR 0x40005c44, USB_DADDR 0x40005c4c,
 *       USB_BTABLE 0x40005c50 and the packet buffer at 0x40006000.
 *
 *   - The interrupt handler at 0x0800ff74 bridges the CDC data to
 *       UART5 (0x40005000) through the buffers at 0x200002a4..0x200002f8.
 *       The rebuild keeps the same shape: the USB stack is transport
 *       agnostic and the application plugs a handler in.
 *
 * The protocol half lives here and is host-testable; the register half
 * is supplied by the caller through usb_hw_t.
 */

#ifndef USB_H
#define USB_H

#include <stdint.h>

#define USB_EP0        0
#define USB_EP0_MAX    64
#define USB_BULK_MAX   64
#define USB_CDC_RX_MAX 512
#define USB_CDC_TX_MAX 512

/* Standard request codes (bmRequestType type field). */
#define USB_REQ_GET_STATUS        0x00
#define USB_REQ_CLEAR_FEATURE     0x01
#define USB_REQ_SET_FEATURE       0x03
#define USB_REQ_SET_ADDRESS       0x05
#define USB_REQ_GET_DESCRIPTOR    0x06
#define USB_REQ_SET_DESCRIPTOR    0x07
#define USB_REQ_GET_CONFIGURATION 0x08
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_GET_INTERFACE     0x0a
#define USB_REQ_SET_INTERFACE     0x0b
#define USB_REQ_SYNCH_FRAME       0x0c

/* CDC-ACM class requests. */
#define USB_CDC_SET_LINE_CODING      0x20
#define USB_CDC_GET_LINE_CODING      0x21
#define USB_CDC_SET_CONTROL_LINE_STATE 0x22

/* Descriptor types. */
#define USB_DT_DEVICE        0x01
#define USB_DT_CONFIGURATION 0x02
#define USB_DT_STRING        0x03
#define USB_DT_INTERFACE     0x04
#define USB_DT_ENDPOINT      0x05
#define USB_DT_DEVICE_QUALIFIER 0x06

/* The hardware hooks the protocol half needs. */
typedef struct
{
    void (*send)(void *ctx, uint8_t endpoint, const uint8_t *data,
                 uint32_t len);
    /* Returns the number of bytes received, or a negative value when
     * nothing is pending. */
    int  (*receive)(void *ctx, uint8_t endpoint, uint8_t *data,
                    uint32_t len);
    void (*set_address)(void *ctx, uint8_t address);
    void (*set_configured)(void *ctx, int configured);
    void (*set_line_state)(void *ctx, int dtr, int rts);
} usb_hw_t;

/* The CDC-ACM device state. */
typedef struct
{
    const usb_hw_t *hw;
    void           *ctx;

    uint8_t  address;
    int      configured;
    int      dtr;
    int      rts;
    uint8_t  pending_request;  /* the class request awaiting its data
                                * phase */
    uint8_t  line_coding[7];   /* dwDTERate, bCharFormat, bParityType,
                                * bDataBits */

    uint8_t  rx[USB_CDC_RX_MAX];
    uint16_t rx_head;
    uint16_t rx_tail;
    uint8_t  tx[USB_CDC_TX_MAX];
    uint16_t tx_head;
    uint16_t tx_tail;

    /* Called with every received bulk OUT packet. */
    int  (*rx_handler)(void *user, const uint8_t *data, uint32_t len);
    void *user;
} usb_cdc_t;

/* Initialises the device. The hardware is not touched until the host
 * configures it. */
void usb_cdc_init(usb_cdc_t *dev, const usb_hw_t *hw, void *ctx);

/* Handles a SETUP packet. Returns the number of bytes to send in the
 * data phase (0 for a request with no data phase). A negative return
 * means the request is not supported and the device should stall. */
int usb_cdc_setup(usb_cdc_t *dev, const uint8_t *setup);

/* Handles a completed control transfer: hands the OUT data phase over. */
void usb_cdc_control_data(usb_cdc_t *dev, const uint8_t *data, uint32_t len);

/* Queues data for the bulk IN endpoint. Returns the number of bytes
 * accepted (0 when the buffer is full). */
uint32_t usb_cdc_write(usb_cdc_t *dev, const uint8_t *data, uint32_t len);

/* Reads received bulk OUT data. Returns the number of bytes copied. */
uint32_t usb_cdc_read(usb_cdc_t *dev, uint8_t *data, uint32_t len);

/* The number of bytes waiting to be sent. */
uint32_t usb_cdc_tx_pending(const usb_cdc_t *dev);

/* The number of bytes waiting to be read. */
uint32_t usb_cdc_rx_pending(const usb_cdc_t *dev);

/* Called from the USB interrupt when a bulk OUT packet arrives: the
 * data is handed to the receive handler. */
void usb_cdc_on_rx(usb_cdc_t *dev, uint8_t endpoint);

/* Called from the USB interrupt when the host asks for bulk IN data. */
void usb_cdc_on_tx(usb_cdc_t *dev, uint8_t endpoint);

/* The descriptor tables, exposed so the tests can check them. */
const uint8_t *usb_device_descriptor(uint32_t *len);
const uint8_t *usb_config_descriptor(uint32_t *len);
const uint8_t *usb_string_descriptor(uint8_t index, uint32_t *len);

#endif /* USB_H */
