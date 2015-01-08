#include <xdc/std.h>
#include <xdc/runtime/Assert.h>
#include <xdc/runtime/Error.h>
#include <xdc/runtime/Startup.h>
#include <ti/sysbios/knl/Swi.h>

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

#include "package/internal/Export.xdc.h"

#define DMA_CONTROL_TABLE_SIZE 1024
static uint8_t dmaControlTable[DMA_CONTROL_TABLE_SIZE]
    __attribute__ ((aligned(1024)));

static inline UInt bufWriteUInt(UInt8 *buf, UInt32 n, Int numBytes)
{
    Int i;
    for (i = 0; i < numBytes; ++i) {
        buf[i] = n & 0xff;
        n >>= 8;
    }
    return numBytes;
}

static inline Void bufWriteVarHeader(UInt8 *buf)
{
    UInt8 *bufp = buf + Export_headerFixedSize;
    bufp += bufWriteUInt(bufp, module->bufferSeqNum++,
                         Export_header[Export_HeaderFieldIndex_SEQ_NUM].size);
    Assert_isTrue(bufp - buf <= Export_headerSize, NULL);
}

Void Export_processBuffers(UArg arg1, UArg arg2)
{
    Int i;
    Export_ExportBuffer *expBuffer;

    /* If not currently transfering, look for a full buffer to transfer */
    if (!module->curExpBuffer) {

        /* Find first full buffer */
        i = 0;
        for (i = 0; i < module->expBuffers.length; ++i)
            if (module->expBuffers.elem[i].full)
                break;

        if (i < module->expBuffers.length) {
            expBuffer = &module->expBuffers.elem[i];
            module->curExpBuffer = expBuffer;

            if (Export_txQueuedCallback)
                Export_txQueuedCallback();

            bufWriteVarHeader(expBuffer->addr);

            uDMAChannelTransferSet(
               module->uartPort.udmaChanTx | UDMA_PRI_SELECT,
               UDMA_MODE_BASIC,
               expBuffer->addr,
               (void *)(module->uartPort.base + UART_O_DR),
               expBuffer->size);

            uDMAChannelEnable(module->uartPort.udmaChanTx);
        }
    }
}

Void Export_onExportComplete(UArg arg)
{
    UInt32 status;
    status = UARTIntStatus(module->uartPort.base, 1);
    UARTIntClear(module->uartPort.base, status);

    /* Disabled channel means transfer is done */
    Assert_isTrue(!uDMAChannelIsEnabled(module->uartPort.udmaChanTx), NULL);

    Assert_isTrue(module->curExpBuffer != NULL, NULL);

    module->curExpBuffer->full = FALSE;
    module->curExpBuffer = NULL;

    Swi_post(module->exportBuffersSwi);

    if (Export_txCompletedCallback)
        Export_txCompletedCallback();
}

Void Export_exportBuffer(UInt idx)
{
    Assert_isTrue(idx < module->expBuffers.length, NULL);
    Assert_isTrue(!module->expBuffers.elem[idx].full, NULL);
    module->expBuffers.elem[idx].full = TRUE;
    Swi_post(module->exportBuffersSwi);
}

Void Export_exportAllBuffers()
{
    UInt8 i;
    for (i = 0; i < module->expBuffers.length; ++i)
        Export_exportBuffer(i);
}

Void Export_resetBufferSequenceNum()
{
    module->bufferSeqNum = 0;
}

static Void initUART()
{
    const Export_UartPort *uartPort = &module->uartPort;

    SysCtlPeripheralEnable(uartPort->gpioPeriph);
    SysCtlPeripheralEnable(uartPort->periph);
    GPIOPinConfigure(uartPort->pinAssignRx);
    GPIOPinConfigure(uartPort->pinAssignTx);
    GPIOPinTypeUART(uartPort->base, uartPort->gpioPins);
    UARTConfigSetExpClk(uartPort->base, SysCtlClockGet(), Export_uartBaudRate,
                        UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE |
                        UART_CONFIG_PAR_NONE);
}

static Void initUDMA()
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UDMA);
    IntEnable(INT_UDMAERR);
    uDMAEnable();
    uDMAControlBaseSet(dmaControlTable);

    // Set both the TX and RX trigger thresholds to 4.  This will be used by
    // the uDMA controller to signal when more data should be transferred.  The
    // uDMA TX and RX channels will be configured so that it can transfer 4
    // bytes in a burst when the UART is ready to transfer more data.
    UARTFIFOLevelSet(module->uartPort.base, UART_FIFO_TX4_8, UART_FIFO_RX4_8);
    UARTDMAEnable(module->uartPort.base, UART_DMA_TX);
    // IntEnable(module->uartPort.interrupt); // TODO: should not be needed

    uDMAChannelAttributeDisable(module->uartPort.udmaChanTx,
                                UDMA_ATTR_ALTSELECT |
                                UDMA_ATTR_HIGH_PRIORITY |
                                UDMA_ATTR_REQMASK);
    uDMAChannelAttributeEnable(module->uartPort.udmaChanTx, UDMA_ATTR_USEBURST);

    uDMAChannelAttributeEnable(module->uartPort.udmaChanTx, UDMA_ATTR_USEBURST);
    uDMAChannelControlSet(module->uartPort.udmaChanTx | UDMA_PRI_SELECT,
                          UDMA_SIZE_8 | UDMA_SRC_INC_8 | UDMA_DST_INC_NONE |
                          UDMA_ARB_4);
}

Void Export_setBufferPointer(UInt bufId, UInt8 *addr)
{
    Assert_isTrue(bufId < module->expBuffers.length, NULL);
    Assert_isTrue(addr != NULL, NULL);
    module->expBuffers.elem[bufId].addr = addr;
}

Int Export_Module_startup(Int state)
{
    initUART();
    initUDMA();
    return Startup_DONE;
}