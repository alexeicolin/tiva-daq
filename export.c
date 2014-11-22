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

/* From TM4C datasheet */
#define MAX_UDMA_TRANSFER_SIZE 1024

static struct ExportBuffer *expBuffers = NULL;

/* Buffer which is currently being transfered  */
static struct ExportBuffer *curExpBuffer = NULL;

static const UInt32 marker = 0xf00dcafe;
static UInt32 bufferSeqNum = 0;

static inline UInt bufWriteUInt(UInt8 *buf, UInt32 n, Int bytes)
{
    Int i;
    for (i = bytes - 1; i >= 0; --i) {
        buf[i] = n & 0xff;
        n >>= 8;
    }
    return bytes;
}

static Void bufWriteFixedHeader(UInt8 *buf, UInt8 idx, UInt16 size)
{
    UInt8 *bufp = buf;
    bufp += bufWriteUInt(bufp, marker, EXPBUF_HEADER_MARKER_SIZE);
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

static Void initUART()
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
    GPIOPinConfigure(GPIO_PA0_U0RX);
    GPIOPinConfigure(GPIO_PA1_U0TX);
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    UARTConfigSetExpClk(UART0_BASE, SysCtlClockGet(), 115200,
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
    UARTFIFOLevelSet(UART0_BASE, UART_FIFO_TX4_8, UART_FIFO_RX4_8);
    UARTDMAEnable(UART0_BASE, UART_DMA_TX);
    IntEnable(INT_UART0);

    uDMAChannelAttributeDisable(UDMA_CHANNEL_UART0TX,
                                UDMA_ATTR_ALTSELECT |
                                UDMA_ATTR_HIGH_PRIORITY |
                                UDMA_ATTR_REQMASK);
    uDMAChannelAttributeEnable(UDMA_CHANNEL_UART0TX, UDMA_ATTR_USEBURST);

    uDMAChannelAttributeEnable(UDMA_CHANNEL_UART0TX, UDMA_ATTR_USEBURST);
    uDMAChannelControlSet(UDMA_CHANNEL_UART0TX | UDMA_PRI_SELECT,
                          UDMA_SIZE_8 | UDMA_SRC_INC_8 | UDMA_DST_INC_NONE |
                          UDMA_ARB_4);
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

            uDMAChannelTransferSet(UDMA_CHANNEL_UART0TX | UDMA_PRI_SELECT,
               UDMA_MODE_BASIC,
               expBuffer->addr,
               (void *)(UART0_BASE + UART_O_DR),
               expBuffer->size);

            uDMAChannelEnable(UDMA_CHANNEL_UART0TX);
        }
    }
}

Void onExportComplete(UArg arg)
{
    UInt32 status;
    status = UARTIntStatus(UART0_BASE, 1);
    UARTIntClear(UART0_BASE, status);

    /* Disabled channel means transfer is done */
    Assert_isTrue(!uDMAChannelIsEnabled(UDMA_CHANNEL_UART0TX), NULL);

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
