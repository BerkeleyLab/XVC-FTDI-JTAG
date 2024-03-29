/*
 * XVC FTDI JTAG Copyright (c) 2021, The Regents of the University of
 * California, through Lawrence Berkeley National Laboratory (subject to
 * receipt of any required approvals from the U.S. Dept. of Energy). All
 * rights reserved.
 * 
 * If you have questions about your rights to use or distribute this software,
 * please contact Berkeley Lab's Intellectual Property Office at
 * IPO@lbl.gov.
 * 
 * NOTICE.  This Software was developed under funding from the U.S. Department
 * of Energy and the U.S. Government consequently retains certain rights.  As
 * such, the U.S. Government has been granted for itself and others acting on
 * its behalf a paid-up, nonexclusive, irrevocable, worldwide license in the
 * Software to reproduce, distribute copies to the public, prepare derivative
 * works, and perform publicly and display publicly, and to permit others to
 * do so.
 */

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <libusb-1.0/libusb.h>

#if (!defined(LIBUSBX_API_VERSION) || (LIBUSBX_API_VERSION < 0x01000102))
# error "You need to get a newer version of libusb-1.0 (16 at the very least)"
#endif

#define XVC_BUFSIZE         1024
#define FTDI_CLOCK_RATE     60000000
#define IDSTRING_CAPACITY   100
#define USB_BUFSIZE         512

/* libusb bmRequestType */
#define BMREQTYPE_OUT (LIBUSB_REQUEST_TYPE_VENDOR | \
                       LIBUSB_RECIPIENT_DEVICE | \
                       LIBUSB_ENDPOINT_OUT)


/* libusb bRequest */
#define BREQ_RESET          0x00
#define BREQ_SET_LATENCY    0x09
#define BREQ_SET_BITMODE    0x0B

/* libusb wValue for assorted bRequest values */
#define WVAL_RESET_RESET        0x00
#define WVAL_RESET_PURGE_RX     0x01
#define WVAL_RESET_PURGE_TX     0x02
#define WVAL_SET_BITMODE_MPSSE (0x0200       | \
                                FTDI_PIN_TCK | \
                                FTDI_PIN_TDI | \
                                FTDI_PIN_TMS)

/* FTDI commands (first byte of bulk write transfer) */
#define FTDI_MPSSE_BIT_WRITE_TMS                0x40
#define FTDI_MPSSE_BIT_READ_DATA                0x20
#define FTDI_MPSSE_BIT_WRITE_DATA               0x10
#define FTDI_MPSSE_BIT_LSB_FIRST                0x08
#define FTDI_MPSSE_BIT_READ_ON_FALLING_EDGE     0x04
#define FTDI_MPSSE_BIT_BIT_MODE                 0x02
#define FTDI_MPSSE_BIT_WRITE_ON_FALLING_EDGE    0x01
#define FTDI_MPSSE_XFER_TDI_BYTES (FTDI_MPSSE_BIT_WRITE_DATA | \
                                   FTDI_MPSSE_BIT_READ_DATA  | \
                                   FTDI_MPSSE_BIT_LSB_FIRST  | \
                                   FTDI_MPSSE_BIT_WRITE_ON_FALLING_EDGE)
#define FTDI_MPSSE_XFER_TDI_BITS (FTDI_MPSSE_BIT_WRITE_DATA | \
                                  FTDI_MPSSE_BIT_READ_DATA  | \
                                  FTDI_MPSSE_BIT_LSB_FIRST  | \
                                  FTDI_MPSSE_BIT_BIT_MODE   | \
                                  FTDI_MPSSE_BIT_WRITE_ON_FALLING_EDGE)
#define FTDI_MPSSE_XFER_TMS_BITS (FTDI_MPSSE_BIT_WRITE_TMS  | \
                                  FTDI_MPSSE_BIT_READ_DATA  | \
                                  FTDI_MPSSE_BIT_LSB_FIRST  | \
                                  FTDI_MPSSE_BIT_BIT_MODE   | \
                                  FTDI_MPSSE_BIT_WRITE_ON_FALLING_EDGE)
#define FTDI_SET_LOW_BYTE           0x80
#define FTDI_ENABLE_LOOPBACK        0x84
#define FTDI_DISABLE_LOOPBACK       0x85
#define FTDI_SET_TCK_DIVISOR        0x86
#define FTDI_DISABLE_TCK_PRESCALER  0x8A
#define FTDI_DISABLE_3_PHASE_CLOCK  0x8D
#define FTDI_ACK_BAD_COMMAND        0xFA

/* FTDI I/O pin bits */
#define FTDI_PIN_TCK    0x1
#define FTDI_PIN_TDI    0x2
#define FTDI_PIN_TDO    0x4
#define FTDI_PIN_TMS    0x8

typedef struct usbInfo {
    /*
     * Diagnostics
     */
    int                    quietFlag;
    int                    runtFlag;
    int                    loopback;
    int                    showUSB;
    int                    showXVC;
    unsigned int           lockedSpeed;

    /*
     * Statistics
     */
    int                    statisticsFlag;
    int                    largestShiftRequest;
    int                    largestWriteRequest;
    int                    largestWriteSent;
    int                    largestReadRequest;
    uint64_t               shiftCount;
    uint64_t               chunkCount;
    uint64_t               bitCount;

    /*
     * Used to find matching device
     */
    int                    vendorId;
    int                    productId;
    const char            *serialNumber;

    /*
     * Matched device
     */
    int                    deviceVendorId;
    int                    deviceProductId;
    char                   deviceVendorString[IDSTRING_CAPACITY];
    char                   deviceProductString[IDSTRING_CAPACITY];
    char                   deviceSerialString[IDSTRING_CAPACITY];

    /*
     * Libusb hooks
     */
    libusb_context        *usb;
    libusb_device_handle  *handle;
    int                    bInterfaceNumber;
    int                    isConnected;
    int                    termChar;
    unsigned char          bTag;
    int                    bulkOutEndpointAddress;
    int                    bulkOutRequestSize;
    int                    bulkInEndpointAddress;
    int                    bulkInRequestSize;

    /*
     * FTDI info
     */
    int                    ftdiJTAGindex;
    const char            *gpioArgument;

    /*
     * I/O buffers
     */
    unsigned char          tmsBuf[XVC_BUFSIZE];
    unsigned char          tdiBuf[XVC_BUFSIZE];
    unsigned char          tdoBuf[XVC_BUFSIZE];
    int                    txCount;
    unsigned char          ioBuf[USB_BUFSIZE];
    unsigned char          rxBuf[USB_BUFSIZE];
    unsigned char          cmdBuf[USB_BUFSIZE];
} usbInfo;

/************************************* MISC ***************************/
static void
showBuf(const char *name, const unsigned char *buf, int numBytes)
{
    int i;
    printf("%s%4d:", name, numBytes);
    if (numBytes > 40) numBytes = 40;
    for (i = 0 ; i < numBytes ; i++) printf(" %02X", buf[i]);
    printf("\n");
}

static void
badEOF(void)
{
    fprintf(stderr, "Unexpected EOF!\n");
}

static void
badChar(void)
{
    fprintf(stderr, "Unexpected character!\n");
}

/*
 * Fetch 32 bit value from client
 */
static int
fetch32(FILE *fp, uint32_t *value)
{
    int i;
    uint32_t v;

    for (i = 0, v = 0 ; i < 32 ; i += 8) {
        int c = fgetc(fp);
        if (c == EOF) {
            badEOF();
            return 0;
        }
        v |= c << i;
    }
    *value = v;
    return 1;
}

/************************************* USB ***************************/
static void
getDeviceString(usbInfo *usb, int i, char *dest)
{
    ssize_t n;

    n = libusb_get_string_descriptor_ascii(usb->handle, i,
                                                 usb->ioBuf, sizeof usb->ioBuf);
    if (n < 0) {
        *dest = '\0';
        return;
    }
    if (n >= IDSTRING_CAPACITY)
        n = IDSTRING_CAPACITY - 1;
    memcpy(dest, (char *)usb->ioBuf, n);
    *(dest + n) = '\0';
}

static void
getDeviceStrings(usbInfo *usb, struct libusb_device_descriptor *desc)
{
    getDeviceString(usb, desc->iManufacturer, usb->deviceVendorString);
    getDeviceString(usb, desc->iProduct, usb->deviceProductString);
    getDeviceString(usb, desc->iSerialNumber, usb->deviceSerialString);
}

/*
 * Get endpoints
 */
static void
getEndpoints(usbInfo *usb, const struct libusb_interface_descriptor *iface_desc)
{
    int e;

    usb->bulkInEndpointAddress = 0;
    usb->bulkOutEndpointAddress = 0;
    for (e = 0 ; e < iface_desc->bNumEndpoints ; e++) {
        const struct libusb_endpoint_descriptor *ep = &iface_desc->endpoint[e];
        if ((ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) ==
                                                    LIBUSB_TRANSFER_TYPE_BULK) {
            if ((ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) ==
                                                           LIBUSB_ENDPOINT_IN) {
                if (usb->bulkInEndpointAddress != 0) {
                    fprintf(stderr, "Too many input endpoints!\n");
                    exit(10);
                }
                usb->bulkInEndpointAddress = ep->bEndpointAddress;
                usb->bulkInRequestSize = ep->wMaxPacketSize;
                if ((size_t)usb->bulkInRequestSize > sizeof usb->ioBuf) {
                    usb->bulkInRequestSize = sizeof usb->ioBuf;
                }
            }
            else {
                if (usb->bulkOutEndpointAddress != 0) {
                    fprintf(stderr, "Too many output endpoints!\n");
                    exit(10);
                }
                usb->bulkOutEndpointAddress = ep->bEndpointAddress;
                usb->bulkOutRequestSize = ep->wMaxPacketSize;
                if ((size_t)usb->bulkOutRequestSize > sizeof usb->cmdBuf) {
                    usb->bulkOutRequestSize = sizeof usb->cmdBuf;
                }
            }
        }
    }
    if (usb->bulkInEndpointAddress == 0) {
        fprintf(stderr, "No input endpoint!\n");
        exit(10);
    }
    if (usb->bulkOutEndpointAddress == 0) {
        fprintf(stderr, "No output endpoint!\n");
        exit(10);
    }
}

/*
 * Search the bus for a matching device
 */
static int
findDevice(usbInfo *usb, libusb_device **list, int n)
{
    int i;
    for (i = 0 ; i < n ; i++) {
        libusb_device *dev = list[i];
        struct libusb_device_descriptor desc;
        struct libusb_config_descriptor *config;
        int productMatch = 0;
        int s = libusb_get_device_descriptor(dev, &desc);
        if (s != 0) {
           fprintf(stderr, "libusb_get_device_descriptor failed: %s",
                                                            libusb_strerror(s));
            return 0;
        }
        if (desc.bDeviceClass != LIBUSB_CLASS_PER_INTERFACE)
            continue;
        if (usb->productId < 0) {
            static const uint16_t validCodes[] = { 0x6010, /* FT2232H */
                                                   0x6011, /* FT4232H */
                                                   0x6014  /* FT232H  */
                                                 };
            int nCodes = sizeof validCodes / sizeof validCodes[0];
            int p;
            for (p = 0 ; p < nCodes ; p++) {
                if (desc.idProduct == validCodes[p]) {
                    productMatch = 1;
                    break;
                }
            }
        }
        else if (usb->productId == desc.idProduct) {
            productMatch = 1;
        }
        if ((usb->vendorId != desc.idVendor) || !productMatch) {
            continue;
        }
        if ((libusb_get_active_config_descriptor(dev, &config) < 0)
         && (libusb_get_config_descriptor(dev, 0, &config) < 0)) {
            fprintf(stderr,
                          "Can't get vendor %04X product %04X configuration.\n",
                                                 desc.idVendor, desc.idProduct);
            continue;
        }
        if (config == NULL) {
            continue;
        }
        if (config->bNumInterfaces >= usb->ftdiJTAGindex) {
            s = libusb_open(dev, &usb->handle);
            if (s == 0) {
                const struct libusb_interface *iface =
                                       &config->interface[usb->ftdiJTAGindex-1];
                const struct libusb_interface_descriptor *iface_desc =
                                                          &iface->altsetting[0];
                usb->bInterfaceNumber = iface_desc->bInterfaceNumber;
                usb->deviceVendorId = desc.idVendor;
                usb->deviceProductId = desc.idProduct;
                getDeviceStrings(usb, &desc);
                if ((usb->serialNumber == NULL)
                 || (strcmp(usb->serialNumber,
                            usb->deviceSerialString) == 0)) {
                    getEndpoints(usb, iface_desc);
                    libusb_free_config_descriptor(config);
                    usb->productId = desc.idProduct;
                    return 1;
                }
                libusb_close(usb->handle);
            }
            else {
                fprintf(stderr, "libusb_open failed: %s\n",
                                                    libusb_strerror(s));
                exit(1);
            }
        }
        libusb_free_config_descriptor(config);
    }
    return 0;
}

static int
usbControl(usbInfo *usb, int bmRequestType, int bRequest, int wValue)
{
    int c;
    if (usb->showUSB) {
        printf("usbControl bmRequestType:%02X bRequest:%02X wValue:%04X\n",
                                               bmRequestType, bRequest, wValue);
    }
    c = libusb_control_transfer(usb->handle, bmRequestType, bRequest, wValue,
                                             usb->ftdiJTAGindex, NULL, 0, 1000);
    if (c != 0) {
        fprintf(stderr, "usb_control_transfer failed: %s\n",libusb_strerror(c));
        exit(1);
    }
    return 1;
}

static int
usbWriteData(usbInfo *usb, unsigned char *buf, int nSend)
{
    int nSent, s;

    if (usb->showUSB) {
        showBuf("Tx", buf, nSend);
    }
    if (nSend > usb->largestWriteRequest) {
        usb->largestWriteRequest = nSend;
    }
    while (nSend) {
        s = libusb_bulk_transfer(usb->handle, usb->bulkOutEndpointAddress, buf,
                                                          nSend, &nSent, 10000);
        if (s) {
            fprintf(stderr, "Bulk write (%d) failed: %s\n", nSend,
                                                            libusb_strerror(s));
            exit(1);
        }
        nSend -= nSent;
        buf += nSent;
        if (nSent > usb->largestWriteSent) {
            usb->largestWriteSent = nSent;
        }
    }
    return 1;
}

static int
usbReadData(usbInfo *usb, unsigned char *buf, int nWant)
{
    int nWanted = nWant;
    const unsigned char *base = buf;

    if (nWant > usb->largestReadRequest) {
        usb->largestReadRequest = nWant;
        if ((nWant+2) > usb->bulkInRequestSize) {
            fprintf(stderr, "usbReadData requested %d, limit is %d.\n",
                                               nWant+2, usb->bulkInRequestSize);
            exit(1);
        }
    }
    while (nWant) {
        int nRecv, s;
        const unsigned char *src = usb->ioBuf;
        s = libusb_bulk_transfer(usb->handle, usb->bulkInEndpointAddress,
                                             usb->ioBuf, nWant+2, &nRecv, 5000);
        if (s) {
            fprintf(stderr, "Bulk read failed: %s\n", libusb_strerror(s));
            exit(1);
        }
        if (nRecv <= 2) {
            if (usb->runtFlag) {
                fprintf(stderr, "wanted:%d want:%d got:%d",nWanted,nWant,nRecv);
                if (nRecv >= 1) {
                    fprintf(stderr, " [%02X", src[0]);
                    if (nRecv >= 2) {
                        fprintf(stderr, " %02X", src[1]);
                    }
                    fprintf(stderr, "]");
                }
                fprintf(stderr, "\n");
            }
            continue;
        }
        else {
            /* Skip FTDI status bytes */
            nRecv -= 2;
            src += 2;
        }
        if (nRecv > nWant) nRecv = nWant;
        memcpy(buf, src, nRecv);
        nWant -= nRecv;
        buf += nRecv;
    }
    if (usb->showUSB) {
        showBuf("Rx", base, nWanted);
    }
    return 1;
}

/************************************* FTDI/JTAG ***************************/
static int
divisorForFrequency(unsigned int frequency)
{
    unsigned int divisor;
    unsigned int actual;
    double r;
    static unsigned int warned = ~0;

    if (frequency <= 0) frequency = 1;
    divisor = ((FTDI_CLOCK_RATE / 2) + (frequency - 1)) / frequency;
    if (divisor >= 0x10000) {
        divisor = 0x10000;
    }
    if (divisor < 1)  {
        divisor = 1;
    }
    actual = FTDI_CLOCK_RATE / (2 * divisor);
    r = (double)frequency / actual;
    if (warned != actual) {
        warned = actual;
        if ((r < 0.999) || (r > 1.001)) {
            fprintf(stderr, "Warning -- %d Hz clock requested, %d Hz actual\n",
                                                             frequency, actual);
        }
        if (actual < 500000) {
            fprintf(stderr, "Warning -- %d Hz clock is a slow choice.\n",
                                                                        actual);
        }
    }
    return divisor;
}

static int
ftdiSetClockSpeed(usbInfo *usb, unsigned int frequency)
{
    unsigned int count;
    if (usb->lockedSpeed) {
        frequency = usb->lockedSpeed;
    }
    count = divisorForFrequency(frequency) - 1;
    usb->ioBuf[0] = FTDI_DISABLE_TCK_PRESCALER;
    usb->ioBuf[1] = FTDI_SET_TCK_DIVISOR;
    usb->ioBuf[2] = count;
    usb->ioBuf[3] = count >> 8;
    return usbWriteData(usb, usb->ioBuf, 4);
}

static int
ftdiGPIO(usbInfo *usb)
{
    unsigned long value;
    unsigned int direction;
    const char *str = usb->gpioArgument;
    char *endp;
    static const struct timespec ms100 = { .tv_sec = 0, .tv_nsec = 100000000 };

    usb->ioBuf[0] = FTDI_SET_LOW_BYTE;
    for (;;) {
        value = strtol(str, &endp, 16);
        if ((endp == str) || ((*endp != '\0') && (*endp != ':'))) {
            break;
        }
        str = endp + 1;
        if (value > 0xFF) {
            break;
        }
        direction = value >> 4;
        value &= 0xF;
        usb->ioBuf[1] = (value << 4) | FTDI_PIN_TMS;
        usb->ioBuf[2] = (direction << 4) |
                                 FTDI_PIN_TMS | FTDI_PIN_TDI | FTDI_PIN_TCK;
        if (!usbWriteData(usb, usb->ioBuf, 3)) {
            break;
        }
        if (*endp == '\0') {
            return 1;
        }
        nanosleep(&ms100, NULL);
    }
    return 0;
}

static int
ftdiInit(usbInfo *usb)
{
    static unsigned char startup[] = {
        FTDI_DISABLE_LOOPBACK,
        FTDI_DISABLE_3_PHASE_CLOCK,
        FTDI_SET_LOW_BYTE,
        FTDI_PIN_TMS,
        FTDI_PIN_TMS | FTDI_PIN_TDI | FTDI_PIN_TCK
    };
    if (!usbControl(usb, BMREQTYPE_OUT, BREQ_RESET, WVAL_RESET_RESET)
     || !usbControl(usb, BMREQTYPE_OUT, BREQ_SET_BITMODE,WVAL_SET_BITMODE_MPSSE)
     || !usbControl(usb, BMREQTYPE_OUT, BREQ_SET_LATENCY, 2)
     || !usbControl(usb, BMREQTYPE_OUT, BREQ_RESET, WVAL_RESET_PURGE_TX)
     || !usbControl(usb, BMREQTYPE_OUT, BREQ_RESET, WVAL_RESET_PURGE_RX)
     || !ftdiSetClockSpeed(usb, 10000000)
     || !usbWriteData(usb, startup, sizeof startup)) {
        return 0;
    }
    if (usb->gpioArgument && !ftdiGPIO(usb)) {
        fprintf(stderr, "Bad -g direction:value[:value...]\n");
        return 0;
    }
    return 1;
}

/************************************* XVC ***************************/
static void
cmdByte(usbInfo *usb, int byte)
{
    if (usb->txCount == USB_BUFSIZE) {
        fprintf(stderr, "USB TX OVERFLOW!\n");
        exit(4);
    }
    usb->ioBuf[usb->txCount++] = byte;
}

/*
 * The USB/JTAG chip can't shift data to TMS and TDI simultaneously
 * so switch between TMS and TDI shift commands as necessary.
 * Break into chunks small enough to fit in single packet.
 */
static int
shiftChunks(usbInfo *usb, int nBits)
{
    int iBit = 0x01, iIndex = 0;
    int cmdBit, cmdIndex, cmdBitcount;
    int tmsBit, tmsBits, tmsState;
    int rxBit, rxIndex;
    int tdoBit = 0x01, tdoIndex = 0;
    unsigned short rxBitcounts[USB_BUFSIZE/3];

    if (usb->loopback) {
        cmdByte(usb, FTDI_ENABLE_LOOPBACK);
    }
    while (nBits) {
        int rxBytesWanted = 0;
        int rxBitcountIndex = 0;
        usb->txCount = 0;
        usb->chunkCount++;
        do {
            /*
             * Stash TMS bits until bit limit reached or TDI would change state
             */
            int tdiFirstState = ((usb->tdiBuf[iIndex] & iBit) != 0);
            cmdBitcount = 0;
            cmdBit = 0x01;
            tmsBits = 0;
            do {
                tmsBit = (usb->tmsBuf[iIndex] & iBit) ? cmdBit : 0;
                tmsBits |= tmsBit;
                if (iBit == 0x80) {
                    iBit = 0x01;
                    iIndex++;
                }
                else {
                    iBit <<= 1;
                }
                cmdBitcount++;
                cmdBit <<= 1;
            } while ((cmdBitcount < 6) && (cmdBitcount < nBits)
                && (((usb->tdiBuf[iIndex] & iBit) != 0) == tdiFirstState));

            /*
             * Duplicate the final TMS bit so the TMS pin holds
             * its value for subsequent TDI shift commands.
             * This is why the bit limit above is 6 and not 7 since
             * we need space to hold the copy of the final bit.
             */
            tmsBits |= (tmsBit << 1);
            tmsState = (tmsBit != 0);

            /*
             * Send the TMS bits and TDI value.
             */
            cmdByte(usb, FTDI_MPSSE_XFER_TMS_BITS);
            cmdByte(usb, cmdBitcount - 1);
            cmdByte(usb, (tdiFirstState << 7) | tmsBits);
            rxBitcounts[rxBitcountIndex++] = cmdBitcount;
            rxBytesWanted++;
            nBits -= cmdBitcount;

            /*
             * Stash TDI bits until bit limit reached
             * or TMS change of state
             * or transmitter buffer capacity reached.
             */
            cmdBitcount = 0;
            cmdIndex = 0;
            cmdBit = 0x01;
            usb->cmdBuf[0] = 0;
            while ((nBits != 0)
               && (((usb->tmsBuf[iIndex] & iBit) != 0) == tmsState)
               && ((usb->txCount+(cmdBitcount/8))<(usb->bulkOutRequestSize-5))){
                if (usb->tdiBuf[iIndex] & iBit) {
                    usb->cmdBuf[cmdIndex] |= cmdBit;
                }
                if (cmdBit == 0x80) {
                    cmdBit = 0x01;
                    cmdIndex++;
                    usb->cmdBuf[cmdIndex] = 0;
                }
                else {
                    cmdBit <<= 1;
                }
                if (iBit == 0x80) {
                    iBit = 0x01;
                    iIndex++;
                }
                else {
                    iBit <<= 1;
                }
                cmdBitcount++;
                nBits--;
            }

            /*
             * Send stashed TDI bits
             */
            if (cmdBitcount > 0) {
                int cmdBytes = cmdBitcount / 8;
                rxBitcounts[rxBitcountIndex++] = cmdBitcount;
                if (cmdBitcount >= 8) {
                    int i;
                    rxBytesWanted += cmdBytes;
                    cmdBitcount -= cmdBytes * 8;
                    i = cmdBytes - 1;
                    cmdByte(usb, FTDI_MPSSE_XFER_TDI_BYTES);
                    cmdByte(usb, i);
                    cmdByte(usb, i >> 8);
                    for (i = 0 ; i < cmdBytes ; i++) {
                        cmdByte(usb, usb->cmdBuf[i]);
                    }
                }
                if (cmdBitcount) {
                    rxBytesWanted++;
                    cmdByte(usb, FTDI_MPSSE_XFER_TDI_BITS);
                    cmdByte(usb, cmdBitcount - 1);
                    cmdByte(usb, usb->cmdBuf[cmdBytes]);
                }
            }
        } while ((nBits != 0)
              && ((usb->txCount+(cmdBitcount/8))<(usb->bulkOutRequestSize-6)));

        /*
         * Shift
         */
        if (!usbWriteData(usb, usb->ioBuf, usb->txCount)
         || !usbReadData(usb, usb->rxBuf, rxBytesWanted)) {
            return 0;
        }

        /*
         * Process received data
         */
        rxIndex = 0;
        for (int i = 0 ; i < rxBitcountIndex ; i++) {
            int rxBitcount = rxBitcounts[i];
            if (rxBitcount < 8) {
                rxBit = 0x1 << (8 - rxBitcount);
            }
            else {
                rxBit = 0x01;
            }
            while (rxBitcount--) {
                if (tdoBit == 0x1) {
                    usb->tdoBuf[tdoIndex] = 0;
                }
                if (usb->rxBuf[rxIndex] & rxBit) {
                    usb->tdoBuf[tdoIndex] |= tdoBit;
                }
                if (rxBit == 0x80) {
                    if (rxBitcount < 8) {
                        rxBit = 0x1 << (8 - rxBitcount);
                    }
                    else {
                        rxBit = 0x01;
                    }
                    rxIndex++;
                }
                else {
                    rxBit <<= 1;
                }
                if (tdoBit == 0x80) {
                    tdoBit = 0x01;
                    tdoIndex++;
                }
                else {
                    tdoBit <<= 1;
                }
            }
        }
        if (rxIndex != rxBytesWanted) {
            printf("Warning -- consumed %d but supplied %d\n", rxIndex,
                                                                 rxBytesWanted);
        }
    }
    return 1;
}

/*
 * Shift a client packet set of bits
 */
static int
shift(usbInfo *usb, FILE *fp)
{
    uint32_t nBits, nBytes;

    if (!fetch32(fp, &nBits)) {
        return 0;
    }
    if (nBits > (unsigned int)usb->largestShiftRequest) {
        usb->largestShiftRequest = nBits;
    }
    usb->bitCount += nBits;
    usb->shiftCount++;
    nBytes = (nBits + 7) / 8;
    if (usb->showXVC) {
        printf("shift:%d\n", (int)nBits);
    }
    if (nBytes > XVC_BUFSIZE) {
        fprintf(stderr, "Client requested %u, max is %u\n", nBytes,XVC_BUFSIZE);
        exit(1);
    }
    if ((fread(usb->tmsBuf, 1, nBytes, fp) != nBytes)
     || (fread(usb->tdiBuf, 1, nBytes, fp) != nBytes)) {
        return 0;
    }
    if (usb->showXVC) {
        showBuf("TMS", usb->tmsBuf, nBytes);
        showBuf("TDI", usb->tdiBuf, nBytes);
    }
    if (!shiftChunks(usb, nBits)) {
        return 0;
    }
    if (usb->showXVC) {
        showBuf("TDO", usb->tdoBuf, nBytes);
    }
    if (usb->loopback) {
        if (memcmp(usb->tdiBuf, usb->tdoBuf, nBytes)) {
            printf("Loopback failed.\n");
        }
    }
    return nBytes;
}

/*
 * Fetch a known string
 */
static int
matchInput(FILE *fp, const char *str)
{
    while (*str) {
        int c = fgetc(fp);
        if (c == EOF) {
            badEOF();
            return 0;
        }
        if (c != *str) {
            fprintf(stderr, "Expected 0x%2x, got 0x%2x\n", *str, c);
            return 0;
        }
        str++;
    }
    return 1;
}

static int
reply(int fd, const unsigned char *buf, int len)
{
    if (write(fd, buf, len) != len) {
        fprintf(stderr, "reply failed: %s\n", strerror(errno));
        return 0;
    }
    return 1;
}

/*
 * Return a 32 bit value
 */
static int
reply32(int fd, uint32_t value)
{
    int i;
    unsigned char cbuf[4];

    for (i = 0 ; i < 4 ; i++) {
        cbuf[i] = value;
        value >>= 8;
    }
    return reply(fd, cbuf, 4);
}

/*
 * Read and process commands
 */
static void
processCommands(FILE *fp, int fd, usbInfo *usb)
{
    int c;

    for (;;) {
        switch(c = fgetc(fp)) {
        case 's':
            switch(c = fgetc(fp)) {
            case 'e':
                {
                uint32_t num;
                int frequency;
                if (!matchInput(fp, "ttck:")) return;
                if (!fetch32(fp, &num)) return;
                frequency = 1000000000 / num;
                if (usb->showXVC) {
                    printf("settck:%d  (%d Hz)\n", (int)num, frequency);
                }
                if (!ftdiSetClockSpeed(usb, frequency)) return;
                if (!reply32(fd, num)) return;
                }
                break;

            case 'h':
                {
                int nBytes;
                if (!matchInput(fp, "ift:")) return;
                nBytes = shift(usb, fp);
                if ((nBytes <= 0) || !reply(fd, usb->tdoBuf, nBytes)) {
                    return;
                }
                }
                break;

            default:
                if (usb->showXVC) {
                    printf("Bad second char 0x%02x\n", c);
                }
                badChar();
                return;
            }
            break;

        case 'g':
            if (matchInput(fp, "etinfo:")) {
                char cBuf[40];
                int len;;
                if (usb->showXVC) {
                    printf("getinfo:\n");
                }
                len = sprintf(cBuf, "xvcServer_v1.0:%u\n", XVC_BUFSIZE);
                if (reply(fd, (unsigned char *)cBuf, len)) {
                    break;
                }
            }
            return;

        case EOF:
            return;

        default:
            if (usb->showXVC) {
                printf("Bad initial char 0x%02x\n", c);
            }
            badChar();
            return;
        }
    }
}

static int
createSocket(const char *interface, int port)
{
    int s, o;
    struct sockaddr_in myAddr;

    s = socket (AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        return -1;
    }
    o = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &o, sizeof o) < 0) {
        return -1;
    }
    memset (&myAddr, '\0', sizeof myAddr);
    myAddr.sin_family = AF_INET;
    if (inet_pton(AF_INET, interface, &myAddr.sin_addr) != 1) {
        fprintf(stderr, "Bad address \"%s\"\n", interface);
        return -1;
    }
    myAddr.sin_port = htons(port);
    if (bind (s, (struct sockaddr *)&myAddr, sizeof myAddr) < 0) {
        fprintf(stderr, "Bind() failed: %s\n", strerror (errno));
        return -1;
    }
    if (listen (s, 1) < 0) {
        fprintf(stderr, "Listen() failed: %s\n", strerror (errno));
        return -1;
    }
    return s;
}

/************************************* Application ***************************/
static int
connectUSB(usbInfo *usb)
{
   libusb_device **list;
    ssize_t n;
    int s;

    n = libusb_get_device_list(usb->usb, &list);
    if (n < 0) {
        fprintf(stderr, "libusb_get_device_list failed: %s", libusb_strerror((int)n));
        return 0;
    }
    s = findDevice(usb, list, n);
    libusb_free_device_list(list, 1);
    if (s) {
        s = libusb_kernel_driver_active(usb->handle, usb->bInterfaceNumber);
        if (s < 0) {
            fprintf(stderr, "libusb_kernel_driver_active() failed: %s\n", libusb_strerror(s));
        }
        else if (s) {
            s = libusb_detach_kernel_driver(usb->handle, usb->bInterfaceNumber);
            if (s) {
                fprintf(stderr, "libusb_detach_kernel_driver() failed: %s\n", libusb_strerror(s));
            }
        }
        s = libusb_claim_interface(usb->handle, usb->bInterfaceNumber);
        if (s) {
            libusb_close(usb->handle);
            fprintf(stderr, "libusb_claim_interface failed: %s\n", libusb_strerror(s));
            return 0;
        }
        if (usb->showUSB || !usb->quietFlag) {
            printf(" Vendor (%04X): \"%s\"\n", usb->vendorId, usb->deviceVendorString);
            printf("Product (%04X): \"%s\"\n", usb->productId, usb->deviceProductString);
            printf("        Serial: \"%s\"\n", usb->deviceSerialString);
            fflush(stdout);
        }
    }
    else {
        fprintf(stderr, "Can't find USB device.\n");
        return 0;
    }
    if (!ftdiInit(usb)) {
        return 0;
    }
    return 1;
}

static void
usage(char *name)
{
    fprintf(stderr, "Usage: %s [-a address] [-p port] "
     "[-d vendor:product[:[serial]]] [-g direction_value[:direction_value...]] "
     "[-c frequency] [-q] [-B] [-L] [-R] [-S] [-U] [-X]\n", name);
    exit(2);
}

static int
convertInt(const char *str)
{
    long v;
    char *endp;
    v = strtol(str, &endp, 0);
    if ((endp == str) || (*endp != '\0')) {
        fprintf(stderr, "Bad integer argument \"%s\"\n", str);
        exit(2);
    }
    return v;
}

static void
deviceConfig(usbInfo *usb, const char *str)
{
    unsigned long vendor, product;
    char *endp;
    vendor = strtol(str, &endp, 16);
    if ((endp != str) && (*endp == ':')) {
        str = endp + 1;
        product = strtol(str, &endp, 16);
        if ((endp != str)
         && ((*endp == '\0') || (*endp == ':'))
         && (vendor <= 0xFFFF)
         && (product <= 0xFFFF)) {
             if (*endp == ':') {
                usb->serialNumber = endp + 1;
            }
            usb->vendorId = vendor;
            usb->productId = product;
            return;
        }
    }
    fprintf(stderr, "Bad -d vendor:product[:[serial]]\n");
    exit(2);
}

static int
clockSpeed(const char *str)
{
    double frequency;
    char *endp;
    frequency = strtod(str, &endp);
    if ((endp == str)
     || ((*endp != '\0') && (*endp != 'M') && (*endp != 'k'))
     || ((*endp != '\0') && (*(endp+1) != '\0'))) {
        fprintf(stderr, "Bad clock frequency argument.\n");
        exit(2);
    }
    if (*endp == 'M') frequency *= 1000000;
    if (*endp == 'k') frequency *= 1000;
    if (frequency >= INT_MAX) frequency = INT_MAX;
    if (frequency <= 0) frequency = 1;
    divisorForFrequency(frequency);
    return frequency;
}

int
main(int argc, char **argv)
{
    int c;
    const char *bindAddress = "127.0.0.1";
    int port = 2542;
    int s;
    char farName[100];
    static usbInfo usbWorkspace = {
        .vendorId = 0x0403,
        .productId = -1,
        .ftdiJTAGindex = 1
    };
    usbInfo *usb = &usbWorkspace;

    while ((c = getopt(argc, argv, "a:b:c:d:g:hp:qBLRSUX")) >= 0) {
        switch(c) {
        case 'a': bindAddress = optarg;                     break;
        case 'c': usb->lockedSpeed = clockSpeed(optarg);    break;
        case 'd': deviceConfig(usb, optarg);                break;
        case 'g': usb->gpioArgument = optarg;               break;
        case 'h': usage(argv[0]);                           break;
        case 'p': port = convertInt(optarg);                break;
        case 'q': usb->quietFlag = 1;                       break;
        case 'u': usb->showUSB = 1;                         break;
        case 'x': usb->showXVC = 1;                         break;
        case 'B': usb->ftdiJTAGindex = 2;                   break;
        case 'L': usb->loopback = 1;                        break;
        case 'R': usb->runtFlag = 1;                        break;
        case 'S': usb->statisticsFlag = 1;                  break;
        case 'U': usb->showUSB = 1;                         break;
        case 'X': usb->showXVC = 1;                         break;
        default:  usage(argv[0]);
        }
    }
    if (optind != argc) {
        fprintf(stderr, "Unexpected argument.\n");
        usage(argv[0]);
    }
    s = libusb_init(&usb->usb);
    if (!connectUSB(usb)) {
        exit(1);
    }
    if (s != 0) {
        fprintf(stderr, "libusb_init() failed: %s\n", libusb_strerror(s));
        return 0;
    }
    if ((s = createSocket(bindAddress, port)) < 0) {
        exit(1);
    }
    for (;;) {
        struct sockaddr_in farAddr;
        socklen_t addrlen = sizeof farAddr;
        FILE *fp;

        int fd = accept(s, (struct sockaddr *)&farAddr, &addrlen);
        if (fd < 0) {
            fprintf(stderr, "Can't accept connection: %s\n", strerror (errno));
            exit(1);
        }
        if ((usb->handle == NULL) && !connectUSB(usb)) {
            exit(1);
        }
        usb->shiftCount = 0;
        usb->chunkCount = 0;
        usb->bitCount = 0;
        if (!usb->quietFlag) {
            inet_ntop(farAddr.sin_family, &(farAddr.sin_addr), farName, sizeof farName);
            printf("Connect %s\n", farName);
        }
        fp = fdopen(fd, "r");
        if (fp == NULL) {
            fprintf(stderr, "fdopen failed: %s\n", strerror(errno));
            close(fd);
            exit(2);
        }
        else {
            processCommands(fp, fd, usb);
            fclose(fp);  /* Closes underlying socket, too */
        }
        if (!usb->quietFlag) {
            printf("Disconnect %s\n", farName);
        }
        if (usb->statisticsFlag) {
            printf("   Shifts: %" PRIu64 "\n", usb->shiftCount);
            printf("   Chunks: %" PRIu64 "\n", usb->chunkCount);
            printf("     Bits: %" PRIu64 "\n", usb->bitCount);
            printf(" Largest shift request: %d\n", usb->largestShiftRequest);
            printf(" Largest write request: %d\n", usb->largestWriteRequest);
            printf("Largest write transfer: %d\n", usb->largestWriteSent);
            printf("  Largest read request: %d\n", usb->largestReadRequest);
        }
        libusb_close(usb->handle);
        usb->handle = NULL;
    }
}
