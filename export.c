#include <xdc/std.h>
#include <xdc/runtime/Assert.h>
#include <ti/sysbios/knl/Swi.h>

#include <xdc/cfg/global.h>

#include <stdbool.h>
#include <stdint.h>

#include <inc/hw_memmap.h>
#include <inc/hw_uart.h>
#include <inc/hw_ints.h>
#include <driverlib/gpio.h>
#include <driverlib/pin_map.h>
#include <driverlib/sysctl.h>
#include <driverlib/udma.h>
#include <driverlib/uart.h>
#include <driverlib/interrupt.h>

#include "Board.h"

#include "export.h"
#include "dma.h"

#define UART_BAUD 115200

#define UART_PORT_BASE       UART0_BASE
#define UART_INTERRUPT       INT_UART0
#define UART_PERIPH          SYSCTL_PERIPH_UART0
#define UART_GPIO_PERIPH     SYSCTL_PERIPH_GPIOA
#define UART_GPIO_PORT_BASE  GPIO_PORTA_BASE
#define UART_GPIO_PINS       (GPIO_PIN_0 | GPIO_PIN_1)
#define UART_PIN_ASSIGN_RX   GPIO_PA0_U0RX
#define UART_PIN_ASSIGN_TX   GPIO_PA1_U0TX
#define UART_UDMA_CHANNEL_TX UDMA_CHANNEL_UART0TX
#define UART_UDMA_CHANNEL_RX UDMA_CHANNEL_UART0RX

/* From TM4C datasheet */
#define MAX_UDMA_TRANSFER_SIZE 1024

static struct ExportBuffer *expBuffers = NULL;

/* Buffer which is currently being transfered  */
static struct ExportBuffer *curExpBuffer = NULL;

static const UInt8 marker[] = { 0xf0, 0x0d, 0xca, 0xfe };
static UInt32 bufferSeqNum = 0;

static inline UInt bufWriteBytes(UInt8 *buf, const UInt8 *bytes, Int numBytes)
{
    Int i;
    for (i = 0; i < numBytes; ++i)
        buf[i] = bytes[i];
    return numBytes;
}

static inline UInt bufWriteUInt(UInt8 *buf, UInt32 n, Int numBytes)
{
    Int i;
    for (i = 0; i < numBytes; ++i) {
        buf[i] = n & 0xff;
        n >>= 8;
    }
    return numBytes;
}

static Void bufWriteFixedHeader(UInt8 *buf, UInt8 idx, UInt16 size)
{
    UInt8 *bufp = buf;
    bufp += bufWriteBytes(bufp, marker, EXPBUF_HEADER_MARKER_SIZE);
    bufp += bufWriteUInt(bufp, size, EXPBUF_HEADER_SIZE_SIZE);
    bufp += bufWriteUInt(bufp, idx, EXPBUF_HEADER_IDX_SIZE);
    Assert_isTrue(bufp - buf <= EXPBUF_FIXED_HEADER_SIZE, NULL);
}

static inline Void bufWriteVarHeader(UInt8 *buf)
{
    UInt8 *bufp = buf + EXPBUF_FIXED_HEADER_SIZE;
    bufp += bufWriteUInt(bufp, bufferSeqNum++, EXPBUF_HEADER_SEQ_SIZE);
    Assert_isTrue(bufp - buf <= EXPBUF_HEADER_SIZE, NULL);
}

Void processBuffers(UArg arg)
{
    Int i;
    struct ExportBuffer *expBuffer;

    /* If not currently transfering, look for a full buffer to transfer */
    if (!curExpBuffer) {

        /* Find first full buffer */
        i = 0;
        while (expBuffers[i].addr && !expBuffers[i].full)
            i++;
        expBuffer = &expBuffers[i];

        if (expBuffer->addr) {
            curExpBuffer = expBuffer;

            /* Turn on LED to indicate start of transfer */
            GPIO_write(EK_TM4C123GXL_LED_GREEN, Board_LED_ON);

            bufWriteVarHeader(expBuffer->addr);

            uDMAChannelTransferSet(UART_UDMA_CHANNEL_TX | UDMA_PRI_SELECT,
               UDMA_MODE_BASIC,
               expBuffer->addr,
               (void *)(UART_PORT_BASE + UART_O_DR),
               expBuffer->size);

            uDMAChannelEnable(UART_UDMA_CHANNEL_TX);
        }
    }
}

Void onExportComplete(UArg arg)
{
    UInt32 status;
    status = UARTIntStatus(UART_PORT_BASE, 1);
    UARTIntClear(UART_PORT_BASE, status);

    /* Disabled channel means transfer is done */
    Assert_isTrue(!uDMAChannelIsEnabled(UART_UDMA_CHANNEL_TX), NULL);

    Assert_isTrue(curExpBuffer != NULL, NULL);

    curExpBuffer->full = FALSE;
    curExpBuffer = NULL;

    Swi_post(exportBuffersSwi);

    /* Turn off LED to indicate transfer is done */
    GPIO_write(EK_TM4C123GXL_LED_GREEN, Board_LED_OFF);
}

Void exportBuffer(Int idx)
{
    Assert_isTrue(!expBuffers[idx].full, NULL);
    expBuffers[idx].full = TRUE;
    Swi_post(exportBuffersSwi);
}

Void exportAllBuffers()
{
    UInt8 i = 0;
    while (expBuffers[i].addr) {
        exportBuffer(i);
        i++;
    }
}

UInt8 findExportBufferIdx(UInt8 *addr)
{
    UInt8 i = 0;
    while (expBuffers[i].addr && expBuffers[i].addr != addr)
        i++;
    Assert_isTrue(expBuffers[i].addr, NULL);
    return i;
}

Void resetBufferSequenceNum()
{
    bufferSeqNum = 0;
}

static Void initUART()
{
    SysCtlPeripheralEnable(UART_GPIO_PERIPH);
    SysCtlPeripheralEnable(UART_PERIPH);
    GPIOPinConfigure(UART_PIN_ASSIGN_RX);
    GPIOPinConfigure(UART_PIN_ASSIGN_TX);
    GPIOPinTypeUART(UART_GPIO_PORT_BASE, UART_GPIO_PINS);
    UARTConfigSetExpClk(UART_PORT_BASE, SysCtlClockGet(), UART_BAUD,
                        UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE |
                        UART_CONFIG_PAR_NONE);
}

Void initExport(struct ExportBuffer *expBufferList)
{
    Int i = 0;
    struct ExportBuffer *expBuffer = expBufferList;

    expBuffers = expBufferList;

    while (expBuffer->addr) {
        Assert_isTrue(expBuffer->size <= MAX_UDMA_TRANSFER_SIZE, NULL);
        bufWriteFixedHeader(expBuffer->addr, i++, expBuffer->size);
        expBuffer++;
    }

    initUART();

    SysCtlPeripheralEnable(SYSCTL_PERIPH_UDMA);
    IntEnable(INT_UDMAERR);
    uDMAEnable();
    uDMAControlBaseSet(dmaControlTable);

    // Set both the TX and RX trigger thresholds to 4.  This will be used by
    // the uDMA controller to signal when more data should be transferred.  The
    // uDMA TX and RX channels will be configured so that it can transfer 4
    // bytes in a burst when the UART is ready to transfer more data.
    UARTFIFOLevelSet(UART_PORT_BASE, UART_FIFO_TX4_8, UART_FIFO_RX4_8);
    UARTDMAEnable(UART_PORT_BASE, UART_DMA_TX);
    IntEnable(UART_INTERRUPT);

    uDMAChannelAttributeDisable(UART_UDMA_CHANNEL_TX,
                                UDMA_ATTR_ALTSELECT |
                                UDMA_ATTR_HIGH_PRIORITY |
                                UDMA_ATTR_REQMASK);
    uDMAChannelAttributeEnable(UART_UDMA_CHANNEL_TX, UDMA_ATTR_USEBURST);

    uDMAChannelAttributeEnable(UART_UDMA_CHANNEL_TX, UDMA_ATTR_USEBURST);
    uDMAChannelControlSet(UART_UDMA_CHANNEL_TX | UDMA_PRI_SELECT,
                          UDMA_SIZE_8 | UDMA_SRC_INC_8 | UDMA_DST_INC_NONE |
                          UDMA_ARB_4);
}
