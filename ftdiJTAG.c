/*
 * Copyright 2020, Lawrence Berkeley National Laboratory
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS
 * AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
# error "You need to get a newer version of libsb-1.0 (16 at the very least)"
#endif

#define XVC_BUFSIZE         2048
#define USB_SHIFT_LIMIT     1000
#define FTDI_CLOCK_RATE     60000000
#define IDSTRING_CAPACITY   100
#define USB_BUFSIZE         ((USB_SHIFT_LIMIT)*4)

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

/* FTDI commands (first byte of bulk transfer) */
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
#define FTDI_SEND_IMMEDIATE         0x87
#define FTDI_DISABLE_TCK_PRESCALER  0x8A

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
    int                    loopback;
    int                    showUSB;
    int                    showXVC;
    unsigned int           lockedSpeed;

    /*
     * Statistics
     */
    int                    statisticsFlag;
    uint32_t               shiftCount;
    uint32_t               chunkCount;
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
    int                    bInterfaceProtocol;
    int                    isConnected;
    int                    termChar;
    unsigned char          bTag;
    int                    bulkOutEndpointAddress;
    int                    bulkInEndpointAddress;

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
    unsigned int           txCount;
    unsigned char          ioBuf[USB_BUFSIZE];
    unsigned char          rxBuf[USB_BUFSIZE];
} usbInfo;

/************************************* MISC ***************************/
static void
showBuf(const char *name, const unsigned char *buf, int numBytes)
{
    int i;
    printf("%s:", name);
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
                usb->bulkInEndpointAddress = ep->bEndpointAddress;
            }
            else {
                usb->bulkOutEndpointAddress = ep->bEndpointAddress;
            }
        }
    }
}

/*
 * Search the bus for a matching device
 */
static int
findDevice(usbInfo *usb, libusb_device **list, int n)
{
    int i, k;

    for (i = 0 ; i < n ; i++) {
        libusb_device *dev = list[i];
        struct libusb_device_descriptor desc;
        struct libusb_config_descriptor *config;

        int s = libusb_get_device_descriptor(dev, &desc);
        if (s != 0) {
           fprintf(stderr, "libusb_get_device_descriptor failed: %s",
                                                            libusb_strerror(s));
            return 0;
        }
        if (desc.bDeviceClass != LIBUSB_CLASS_PER_INTERFACE)
            continue;
        if ((libusb_get_active_config_descriptor(dev, &config) < 0)
         && (libusb_get_config_descriptor(dev, 0, &config) < 0))
            continue;
        if (config == NULL)
            continue;
        if (config->bNumInterfaces == 4) {
            const struct libusb_interface *iface =
                                       &config->interface[usb->ftdiJTAGindex-1];
            for (k = 0 ; k < iface->num_altsetting ; k++) {
                const struct libusb_interface_descriptor *iface_desc;
                iface_desc = &iface->altsetting[k];
                if ((usb->vendorId == desc.idVendor)
                 && (usb->productId == desc.idProduct)) {
                    usb->bInterfaceNumber = iface_desc->bInterfaceNumber;
                    usb->bInterfaceProtocol = iface_desc->bInterfaceProtocol;
                    s = libusb_open(dev, &usb->handle);
                    if (s == 0) {
                        usb->deviceVendorId = desc.idVendor;
                        usb->deviceProductId = desc.idProduct;
                        getDeviceStrings(usb, &desc);
                        if ((usb->serialNumber == NULL)
                         || (strcmp(usb->serialNumber,
                             usb->deviceSerialString) == 0)) {
                            getEndpoints(usb, iface_desc);
                            libusb_free_config_descriptor(config);
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
    c = (libusb_control_transfer(usb->handle, bmRequestType, bRequest, wValue,
                                       usb->ftdiJTAGindex, NULL, 0, 1000) < 0);
    if (c < 0) {
        fprintf(stderr, "usb_control_transfer failed: %s\n",libusb_strerror(c));
        return 0;
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
    while (nSend) {
        s = libusb_bulk_transfer(usb->handle, usb->bulkOutEndpointAddress, buf,
                                                           nSend, &nSent, 1000);
        if (s) {
            fprintf(stderr, "Bulk transfer failed: %s\n", libusb_strerror(s));
            return 0;
        }
        nSend -= nSent;
        buf += nSent;
    }
    return 1;
}

static int
usbReadData(usbInfo *usb, unsigned char *buf, int nWant)
{
    int nWanted = nWant;
    const unsigned char *base = buf;

    while (nWant) {
        int nRecv, nCopy, s;
        s = libusb_bulk_transfer(usb->handle, usb->bulkInEndpointAddress,
                                   usb->ioBuf, sizeof usb->ioBuf, &nRecv, 1000);
        if (s) {
            fprintf(stderr, "Bulk transfer failed: %s\n", libusb_strerror(s));
            return 0;
        }
        if (nRecv <= 2) {
            fprintf(stderr, "Runt (%d)\n", nRecv);
            continue;
        }
        /* Skip FTDI status bytes */
        nCopy = nRecv - 2;
        if (nCopy > nWant) nCopy = nWant;
        memcpy(buf, usb->ioBuf + 2, nCopy);
        nWant -= nCopy;
        buf += nCopy;
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
    if ((r < 0.999) || (r > 1.001)) {
        fprintf(stderr, "Warning -- %d Hz clock requested, %d Hz actual\n",
                                                             frequency, actual);
    }
    if (actual < 500000) {
        fprintf(stderr, "Warning -- %d Hz clock is a slow choice.\n", actual);
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
    unsigned long direction, value;
    const char *str = usb->gpioArgument;
    char *endp;
    static const struct timespec ms100 = { .tv_sec = 0, .tv_nsec = 100000000 };

    usb->ioBuf[0] = FTDI_SET_LOW_BYTE;
    direction = strtol(str, &endp, 16);
    if ((endp != str) && (*endp == ':')) {
        for (;;) {
            str = endp + 1;
            value = strtol(str, &endp, 16);
            if ((endp == str) || ((*endp != '\0') && (*endp != ':'))) {
                break;
            }
            if ((direction > 0xF) || (value > 0xF)) {
                break;
            }
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
    }
    return 0;
}

static int
ftdiInit(usbInfo *usb)
{
    static unsigned char startup[] = {
        FTDI_DISABLE_LOOPBACK,
        FTDI_SET_LOW_BYTE,
        FTDI_PIN_TMS,
        FTDI_PIN_TMS | FTDI_PIN_TDI | FTDI_PIN_TCK
    };
    if (!usbControl(usb, BMREQTYPE_OUT, BREQ_RESET, WVAL_RESET_RESET)
     || !usbControl(usb, BMREQTYPE_OUT, BREQ_SET_LATENCY, 0)
     || !usbControl(usb, BMREQTYPE_OUT, BREQ_SET_BITMODE,WVAL_SET_BITMODE_MPSSE)
     || !usbControl(usb, BMREQTYPE_OUT, BREQ_RESET, WVAL_RESET_PURGE_TX)
     || !usbControl(usb, BMREQTYPE_OUT, BREQ_RESET, WVAL_RESET_PURGE_RX)
     || !ftdiSetClockSpeed(usb, 10000000)
     || !usbWriteData(usb, startup, sizeof startup)) {
        return 0;
    }
    if (!ftdiSetClockSpeed(usb, usb->lockedSpeed ? usb->lockedSpeed
                                                 : FTDI_CLOCK_RATE / 2)) {
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
 * Keep USB packet sizes reasonable by transferring a chunk at a time.
 * The USB/JTAG chip can't shift data to TMS and TDI simultaneously
 * so switch between TMS and TDI shift commands as necessary.
 */
static int
shiftChunk(usbInfo *usb, int nBits, const unsigned char *tmsBuf,
                             const unsigned char *tdiBuf, unsigned char *tdoBuf)
{
    int iBit = 0x01, iIndex = 0;
    int cmdBit, cmdIndex, cmdBitcount;
    int tmsBit, tmsBits, tmsState;
    int rxBitcountIndex = 0;
    int rxBit, rxIndex = 0, rxWanted = 0;
    int tdoBit = 0x01, tdoIndex = 0;
    unsigned char cmdBuf[USB_BUFSIZE];
    unsigned short rxBitcounts[USB_BUFSIZE/3];

    usb->chunkCount++;
    usb->txCount = 0;
    if (usb->loopback) {
        cmdByte(usb, FTDI_ENABLE_LOOPBACK);
    }
    while (nBits) {
        /*
         * Stash TMS bits until bit limit reached or TDI would change state
         */
        int tdiFirstState = ((tdiBuf[iIndex] & iBit) != 0);
        cmdBitcount = 0;
        cmdBit = 0x01;
        tmsBits = 0;
        do {
            tmsBit = (tmsBuf[iIndex] & iBit) ? cmdBit : 0;
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
            && (((tdiBuf[iIndex] & iBit) != 0) == tdiFirstState));

        /*
         * Duplicate the final TMS bit so the TMS pin holds
         * its value for subsequent TDI shift commands.
         * This is why the bit limit above is 6 and not 7.
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
        rxWanted++;
        nBits -= cmdBitcount;

        /*
         * Stash TDI bits until TMS change of state
         */
        cmdBitcount = 0;
        cmdIndex = 0;
        cmdBit = 0x01;
        cmdBuf[0] = 0;
        while (nBits && (((tmsBuf[iIndex] & iBit) != 0) == tmsState)) {
            if (tdiBuf[iIndex] & iBit) {
                cmdBuf[cmdIndex] |= cmdBit;
            }
            if (cmdBit == 0x80) {
                cmdBit = 0x01;
                cmdIndex++;
                cmdBuf[cmdIndex] = 0;
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
                rxWanted += cmdBytes;
                cmdBitcount -= cmdBytes * 8;
                i = cmdBytes - 1;
                cmdByte(usb, FTDI_MPSSE_XFER_TDI_BYTES);
                cmdByte(usb, i);
                cmdByte(usb, i >> 8);
                for (i = 0 ; i < cmdBytes ; i++) {
                    cmdByte(usb, cmdBuf[i]);
                }
            }
            if (cmdBitcount) {
                rxWanted++;
                cmdByte(usb, FTDI_MPSSE_XFER_TDI_BITS);
                cmdByte(usb, cmdBitcount - 1);
                cmdByte(usb, cmdBuf[cmdBytes]);
            }
        }
    }
    if (!usbWriteData(usb, usb->ioBuf, usb->txCount)
     || !usbReadData(usb, usb->rxBuf, rxWanted)) {
        return 0;
    }
    tdoBuf[tdoIndex] = 0;
    for (int i = 0 ; i < rxBitcountIndex ; i++) {
        int rxBitcount = rxBitcounts[i];
        rxBit = 0x01;
        if (rxBitcount < 8) {
            rxBit <<= (8 - rxBitcount);
        }
        while (rxBitcount--) {
            if (usb->rxBuf[rxIndex] & rxBit) {
                tdoBuf[tdoIndex] |= tdoBit;
            }
            if (rxBit == 0x80) {
                rxBit = 0x01;
                if (rxBitcount < 8) {
                    rxBit <<= (8 - rxBitcount);
                }
                rxIndex++;
            }
            else {
                rxBit <<= 1;
            }
            if (tdoBit == 0x80) {
                tdoBit = 0x01;
                tdoIndex++;
                tdoBuf[tdoIndex] = 0;
            }
            else {
                tdoBit <<= 1;
            }
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
    const unsigned char *tmsBuf = usb->tmsBuf;
    const unsigned char *tdiBuf = usb->tdiBuf;
    unsigned char *tdoBuf = usb->tdoBuf;

    if (!fetch32(fp, &nBits)) {
        return 0;
    }
    usb->bitCount += nBits;
    usb->shiftCount++;
    nBytes = (nBits + 7) / 8;
    if (usb->showXVC) {
        printf("shift:%d\n", (int)nBits);
    }
    if (nBytes > XVC_BUFSIZE) {
        fprintf(stderr, "Client requested %d, max is %d\n", nBytes,XVC_BUFSIZE);
        return 0;
    }
    if ((fread(usb->tmsBuf, 1, nBytes, fp) != nBytes)
     || (fread(usb->tdiBuf, 1, nBytes, fp) != nBytes)) {
        return 0;
    }
    if (usb->showXVC) {
        showBuf("TMS", usb->tmsBuf, nBytes);
        showBuf("TDI", usb->tdiBuf, nBytes);
    }
    while (nBits) {
        unsigned int shiftBytes, shiftBits;
        shiftBytes = (nBits + 7) / 8;
        if (shiftBytes > USB_SHIFT_LIMIT) {
            shiftBytes = USB_SHIFT_LIMIT;
        }
        shiftBits = shiftBytes * 8;
        if (shiftBits > nBits) {
            shiftBits = nBits;
        }
        if (!shiftChunk(usb, shiftBits, tmsBuf, tdiBuf, tdoBuf)) {
            return 0;
        }
        nBits -= shiftBits;
        tmsBuf += shiftBytes;
        tdiBuf += shiftBytes;
        tdoBuf += shiftBytes;
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
                len = sprintf(cBuf, "xvcServer_v1.0:%d\n", XVC_BUFSIZE);
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
        if (s) {
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
         "[-d vendor:product[:[serial]]] [-g direction:value[:value...]] [-c frequency] [-q] [-u] [-x] [-B] [-S]\n", name);
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
    long vendor, product;
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
        .productId = 0x6011,
        .ftdiJTAGindex = 1
    };
    usbInfo *usb = &usbWorkspace;

    while ((c = getopt(argc, argv, "a:c:d:g:hp:quxBLS")) >= 0) {
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
        case 'S': usb->statisticsFlag = 1;                  break;
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
            if (usb->statisticsFlag) {
                printf("   Shifts: %" PRIu32 "\n", usb->shiftCount);
                printf("   Chunks: %" PRIu32 "\n", usb->chunkCount);
                printf("     Bits: %" PRIu64 "\n", usb->bitCount);
            }
        }
        libusb_close(usb->handle);
        usb->handle = NULL;
    }
}
