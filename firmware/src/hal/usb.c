/*
 * usb.c - the USB device: a CDC-ACM virtual COM port.
 *
 * See usb.h for the evidence behind the descriptor set.
 */

#include <string.h>
#include "usb.h"

/* ------------------------------------------------------------------ */
/* The descriptor set, recovered from 0x08084938.                      */
/* ------------------------------------------------------------------ */

static const uint8_t g_device_descriptor[18] = {
    0x12,        /* bLength */
    0x01,        /* bDescriptorType: DEVICE */
    0x00, 0x02,  /* bcdUSB 2.00 */
    0x00,        /* bDeviceClass */
    0x00,        /* bDeviceSubClass */
    0x00,        /* bDeviceProtocol */
    0x40,        /* bMaxPacketSize0 */
    0x83, 0x04,  /* idVendor 0x0483 (STMicroelectronics) */
    0x20, 0x57,  /* idProduct 0x5720 (virtual COM port) */
    0x00, 0x02,  /* bcdDevice 2.00 */
    0x01,        /* iManufacturer */
    0x02,        /* iProduct */
    0x03,        /* iSerialNumber */
    0x01         /* bNumConfigurations */
};

static const uint8_t g_config_descriptor[32] = {
    0x09, 0x02,  /* bLength, CONFIGURATION */
    0x20, 0x00,  /* wTotalLength 32 */
    0x01,        /* bNumInterfaces */
    0x01,        /* bConfigurationValue */
    0x00,        /* iConfiguration */
    0xc0,        /* bmAttributes */
    0x32,        /* bMaxPower 50 mA */
    /* Interface 0 */
    0x09, 0x04,  /* bLength, INTERFACE */
    0x00,        /* bInterfaceNumber */
    0x00,        /* bAlternateSetting */
    0x02,        /* bNumEndpoints */
    0x08,        /* bInterfaceClass: Communications */
    0x06,        /* bInterfaceSubClass: ACM */
    0x50,        /* bInterfaceProtocol: CDC */
    0x04,        /* iInterface */
    /* Endpoint 0x81, bulk in */
    0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,
    /* Endpoint 0x02, bulk out */
    0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00
};

/* The CDC class functional descriptors the interface needs. They are
 * not part of wTotalLength in the configuration descriptor the firmware
 * ships, so they are served separately. */
static const uint8_t g_cdc_descriptors[] = {
    /* Header functional descriptor */
    0x05, 0x24, 0x00, 0x10, 0x01,
    /* Call management functional descriptor */
    0x05, 0x24, 0x01, 0x00, 0x01,
    /* Abstract control management functional descriptor */
    0x04, 0x24, 0x02, 0x00,
    /* Union functional descriptor */
    0x05, 0x24, 0x06, 0x00, 0x01
};

/* The strings, as UTF-16LE. */
static const uint8_t g_string_manufacturer[] = {
    0x0c, 0x03, 'H', 0, 'e', 0, 'a', 0, 't', 0, 'm', 0
};
static const uint8_t g_string_product[] = {
    0x0e, 0x03, 'A', 0, 'c', 0, 'c', 0, 'e', 0, 'n', 0, 't', 0
};
static const uint8_t g_string_serial[] = {
    0x10, 0x03, 'S', 0, 'T', 0, ' ', 0, 'M', 0, 'a', 0, 's', 0,
    's', 0
};
static const uint8_t g_string_interface[] = {
    0x08, 0x03, 'C', 0, 'O', 0, 'M', 0
};

const uint8_t *usb_device_descriptor(uint32_t *len)
{
    if (len != 0)
    {
        *len = sizeof(g_device_descriptor);
    }
    return g_device_descriptor;
}

const uint8_t *usb_config_descriptor(uint32_t *len)
{
    if (len != 0)
    {
        *len = sizeof(g_config_descriptor);
    }
    return g_config_descriptor;
}

const uint8_t *usb_string_descriptor(uint8_t index, uint32_t *len)
{
    const uint8_t *p = 0;
    uint32_t n = 0;

    switch (index)
    {
    case 0:
        p = g_string_interface;
        n = sizeof(g_string_interface);
        break;
    case 1:
        /* The manufacturer, "Heatmiser". */
        p = g_string_manufacturer;
        n = sizeof(g_string_manufacturer);
        break;
    case 2:
        p = g_string_product;
        n = sizeof(g_string_product);
        break;
    case 3:
        p = g_string_serial;
        n = sizeof(g_string_serial);
        break;
    default:
        break;
    }
    if (len != 0)
    {
        *len = n;
    }
    return p;
}

/* The CDC functional descriptor block, served on a GET_DESCRIPTOR of
 * type 0x24 (CS_INTERFACE). */
static const uint8_t *cdc_descriptors(uint32_t *len)
{
    if (len != 0)
    {
        *len = sizeof(g_cdc_descriptors);
    }
    return g_cdc_descriptors;
}

/* ------------------------------------------------------------------ */
/* The device                                                           */
/* ------------------------------------------------------------------ */

void usb_cdc_init(usb_cdc_t *dev, const usb_hw_t *hw, void *ctx)
{
    if (dev == 0)
    {
        return;
    }
    memset(dev, 0, sizeof(*dev));
    dev->hw = hw;
    dev->ctx = ctx;
    dev->address = 0;
    dev->configured = 0;
    /* 115200 8N1, the default the CDC class asks for. */
    dev->line_coding[0] = 0x00;
    dev->line_coding[1] = 0xC2;
    dev->line_coding[2] = 0x00;
    dev->line_coding[3] = 0x00;
    dev->line_coding[4] = 0x00;
    dev->line_coding[5] = 0x00;
    dev->line_coding[6] = 0x08;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* The buffer the control transfer's data phase is staged in. */
static uint8_t g_control_buffer[USB_EP0_MAX];

int usb_cdc_setup(usb_cdc_t *dev, const uint8_t *setup)
{
    uint8_t  type;
    uint8_t  recipient;
    uint8_t  request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
    uint32_t n;

    if (dev == 0 || setup == 0 || dev->hw == 0)
    {
        return -1;
    }

    type      = setup[0] & 0x60u;
    recipient = setup[0] & 0x1Fu;
    request   = setup[1];
    value     = rd16(setup + 2);
    index     = rd16(setup + 4);
    length    = rd16(setup + 6);

    if (type != 0x00u)          /* only standard requests */
    {
        if (type == 0x20u)      /* class requests */
        {
            switch (request)
            {
            case USB_CDC_SET_LINE_CODING:
                if (length > sizeof(dev->line_coding))
                {
                    return -1;
                }
                /* The data phase arrives after this call, through
                 * usb_cdc_control_data(). */
                dev->pending_request = USB_CDC_SET_LINE_CODING;
                return 0;

            case USB_CDC_GET_LINE_CODING:
                n = (length < sizeof(dev->line_coding)) ?
                    length : sizeof(dev->line_coding);
                memcpy(g_control_buffer, dev->line_coding, n);
                dev->hw->send(dev->ctx, USB_EP0, g_control_buffer, n);
                return (int)n;

            case USB_CDC_SET_CONTROL_LINE_STATE:
                dev->dtr = (value & 0x01u) ? 1 : 0;
                dev->rts = (value & 0x02u) ? 1 : 0;
                if (dev->hw->set_line_state != 0)
                {
                    dev->hw->set_line_state(dev->ctx, dev->dtr, dev->rts);
                }
                return 0;

            default:
                return -1;
            }
        }
        return -1;
    }

    switch (request)
    {
    case USB_REQ_GET_STATUS:
        g_control_buffer[0] = 0x00;
        g_control_buffer[1] = 0x00;
        dev->hw->send(dev->ctx, USB_EP0, g_control_buffer, 2);
        return 2;

    case USB_REQ_SET_ADDRESS:
        dev->address = (uint8_t)(value & 0x7Fu);
        if (dev->hw->set_address != 0)
        {
            dev->hw->set_address(dev->ctx, dev->address);
        }
        return 0;

    case USB_REQ_SET_CONFIGURATION:
        dev->configured = (value != 0) ? 1 : 0;
        if (dev->hw->set_configured != 0)
        {
            dev->hw->set_configured(dev->ctx, dev->configured);
        }
        return 0;

    case USB_REQ_GET_CONFIGURATION:
        g_control_buffer[0] = dev->configured ? 1 : 0;
        dev->hw->send(dev->ctx, USB_EP0, g_control_buffer, 1);
        return 1;

    case USB_REQ_CLEAR_FEATURE:
    case USB_REQ_SET_FEATURE:
    case USB_REQ_SET_INTERFACE:
    case USB_REQ_SYNCH_FRAME:
        return 0;

    case USB_REQ_GET_DESCRIPTOR:
        switch (value >> 8)
        {
        case USB_DT_DEVICE:
        {
            const uint8_t *d = usb_device_descriptor(&n);
            if (n > length)
            {
                n = length;
            }
            memcpy(g_control_buffer, d, n);
            dev->hw->send(dev->ctx, USB_EP0, g_control_buffer, n);
            return (int)n;
        }
        case USB_DT_CONFIGURATION:
        {
            const uint8_t *d = usb_config_descriptor(&n);
            if (n > length)
            {
                n = length;
            }
            memcpy(g_control_buffer, d, n);
            dev->hw->send(dev->ctx, USB_EP0, g_control_buffer, n);
            return (int)n;
        }
        case USB_DT_STRING:
        {
            uint32_t slen = 0;
            const uint8_t *d = usb_string_descriptor((uint8_t)value,
                                                     &slen);
            if (d == 0)
            {
                return -1;
            }
            if (slen > length)
            {
                slen = length;
            }
            memcpy(g_control_buffer, d, slen);
            dev->hw->send(dev->ctx, USB_EP0, g_control_buffer, slen);
            return (int)slen;
        }
        default:
            break;
        }
        /* The CDC functional descriptors are served on the class
         * specific descriptor type. */
        if ((value >> 8) == 0x24u || (value >> 8) == 0x21u)
        {
            const uint8_t *d = cdc_descriptors(&n);
            if (n > length)
            {
                n = length;
            }
            memcpy(g_control_buffer, d, n);
            dev->hw->send(dev->ctx, USB_EP0, g_control_buffer, n);
            return (int)n;
        }
        return -1;

    default:
        break;
    }

    (void)recipient;
    (void)index;
    return -1;
}

void usb_cdc_control_data(usb_cdc_t *dev, const uint8_t *data, uint32_t len)
{
    if (dev == 0 || data == 0)
    {
        return;
    }
    /* The only class request with an OUT data phase is SET_LINE_CODING;
     * the setup stage already validated the length. */
    if (dev->pending_request == USB_CDC_SET_LINE_CODING &&
        len <= sizeof(dev->line_coding))
    {
        memcpy(dev->line_coding, data, len);
    }
    dev->pending_request = 0;
}

uint32_t usb_cdc_write(usb_cdc_t *dev, const uint8_t *data, uint32_t len)
{
    uint32_t space;
    uint32_t used;
    uint32_t n;

    if (dev == 0 || data == 0)
    {
        return 0;
    }
    used = dev->tx_head - dev->tx_tail;
    space = USB_CDC_TX_MAX - used;
    if (space == 0)
    {
        return 0;
    }
    n = (len < space) ? len : space;
    memcpy(&dev->tx[dev->tx_head % USB_CDC_TX_MAX], data, n);
    dev->tx_head = (uint16_t)(dev->tx_head + n);
    return n;
}

uint32_t usb_cdc_read(usb_cdc_t *dev, uint8_t *data, uint32_t len)
{
    uint32_t n = 0;

    if (dev == 0 || data == 0)
    {
        return 0;
    }
    while (n < len && dev->rx_tail != dev->rx_head)
    {
        data[n++] = dev->rx[dev->rx_tail % USB_CDC_RX_MAX];
        dev->rx_tail = (uint16_t)(dev->rx_tail + 1);
    }
    return n;
}

uint32_t usb_cdc_tx_pending(const usb_cdc_t *dev)
{
    if (dev == 0)
    {
        return 0;
    }
    return (uint32_t)(dev->tx_head - dev->tx_tail);
}

uint32_t usb_cdc_rx_pending(const usb_cdc_t *dev)
{
    if (dev == 0)
    {
        return 0;
    }
    return (uint32_t)(dev->rx_head - dev->rx_tail);
}

void usb_cdc_on_rx(usb_cdc_t *dev, uint8_t endpoint)
{
    uint8_t packet[USB_BULK_MAX];
    int n;

    if (dev == 0 || dev->hw == 0 || dev->hw->receive == 0)
    {
        return;
    }
    n = dev->hw->receive(dev->ctx, endpoint, packet, sizeof(packet));
    if (n <= 0)
    {
        return;
    }
    if (dev->rx_handler != 0)
    {
        dev->rx_handler(dev->user, packet, (uint32_t)n);
        return;
    }
    /* Without a handler the bytes are queued for the application. */
    {
        uint32_t space = USB_CDC_RX_MAX - (dev->rx_head - dev->rx_tail);
        uint32_t take = ((uint32_t)n < space) ? (uint32_t)n : space;
        memcpy(&dev->rx[dev->rx_head % USB_CDC_RX_MAX], packet, take);
        dev->rx_head = (uint16_t)(dev->rx_head + take);
    }
}

void usb_cdc_on_tx(usb_cdc_t *dev, uint8_t endpoint)
{
    uint8_t packet[USB_BULK_MAX];
    uint32_t pending;
    uint32_t n;
    uint32_t i;

    if (dev == 0 || dev->hw == 0 || dev->hw->send == 0)
    {
        return;
    }
    pending = usb_cdc_tx_pending(dev);
    if (pending == 0)
    {
        return;
    }
    n = (pending < USB_BULK_MAX) ? pending : USB_BULK_MAX;
    for (i = 0; i < n; i++)
    {
        packet[i] = dev->tx[dev->tx_tail % USB_CDC_TX_MAX];
        dev->tx_tail = (uint16_t)(dev->tx_tail + 1);
    }
    dev->hw->send(dev->ctx, endpoint, packet, n);
}
