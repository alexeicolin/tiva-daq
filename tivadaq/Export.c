#include <xdc/std.h>
#include <xdc/runtime/Assert.h>
#include <xdc/runtime/Error.h>
#include <xdc/runtime/Startup.h>
#include <xdc/runtime/Log.h>
#include <ti/sysbios/knl/Swi.h>

#include <platforms/hw/tiva/UartPort.h>
#include <platforms/hw/tiva/GpioPort.h>
#include <platforms/hw/tiva/GpioPeriph.h>

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

static inline UInt bufWriteBytes(UInt8 *buf, UInt8 *bytes, Int numBytes)
{
    Int i;
    for (i = 0; i < numBytes; ++i)
        buf[i] = bytes[i];
    return numBytes;
}

static inline Void bufWriteFixedHeader(UInt8 *buf, UInt32 size, UInt8 userId)
{
    UInt8 *bufp = buf;
    bufp += bufWriteBytes(bufp, Export_marker,
                          Export_header[Export_HeaderFieldIndex_MARKER].size);
    bufp += bufWriteUInt(bufp, size,
                          Export_header[Export_HeaderFieldIndex_SIZE].size);
    bufp += bufWriteUInt(bufp, userId,
                          Export_header[Export_HeaderFieldIndex_USER_ID].size);
    Assert_isTrue(bufp - buf <= Export_headerFixedSize, NULL);
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
    const UartPort_Info *uartPort = UartPort_getInfo(module->uartPort);
    UInt32 *debugDataPtr;

    /* If not currently transfering, look for a full buffer to transfer */
    if (!module->curExpBuffer) {

        /* Find first full buffer */
        for (i = 0; i < module->exportBuffers.length; ++i)
            if (module->exportBuffers.elem[i].full)
                break;

        if (i < module->exportBuffers.length) {
            expBuffer = &module->exportBuffers.elem[i];
            module->curExpBuffer = expBuffer;

            Log_write4(Export_LM_transferStarted, i,
                       (IArg)expBuffer->addr,
                       (IArg)((void *)uartPort->base + UART_O_DR),
                       expBuffer->size);

            if (Export_txQueuedCallback)
                Export_txQueuedCallback();

            bufWriteVarHeader(expBuffer->addr);

            debugDataPtr = (UInt32*)expBuffer->addr;
            Log_write8(Export_LM_bufferContent, (IArg)expBuffer->addr,
                       (IArg)debugDataPtr[0],  (IArg)debugDataPtr[1],
                       (IArg)debugDataPtr[2],  (IArg)debugDataPtr[3],
                       (IArg)debugDataPtr[4],  (IArg)debugDataPtr[5],
                       (IArg)debugDataPtr[6]);

            uDMAChannelTransferSet(
               uartPort->udmaChanTx | UDMA_PRI_SELECT,
               UDMA_MODE_BASIC,
               expBuffer->addr,
               (void *)(uartPort->base + UART_O_DR),
               expBuffer->size);

            uDMAChannelEnable(uartPort->udmaChanTx);
        } else {
            Log_write0(Export_LM_noFullBuffers);
        }
    } else {
        Log_write0(Export_LM_transferInProgress);
    }
}

Void Export_onExportComplete(UArg arg)
{
    const UartPort_Info *uartPort = UartPort_getInfo(module->uartPort);
    UInt32 status;

    status = UARTIntStatus(uartPort->base, 1);
    UARTIntClear(uartPort->base, status);

    /* Disabled channel means transfer is done */
    Assert_isTrue(!uDMAChannelIsEnabled(uartPort->udmaChanTx), NULL);

    Assert_isTrue(module->curExpBuffer != NULL, NULL);

    Log_write1(Export_LM_transferCompleted, (IArg)module->curExpBuffer->addr);

    module->curExpBuffer->full = FALSE;
    module->curExpBuffer = NULL;

    Swi_post(module->exportBuffersSwi);

    if (Export_txCompletedCallback)
        Export_txCompletedCallback();
}

Void Export_exportBuffer(UInt idx)
{
    Assert_isTrue(idx < module->exportBuffers.length, NULL);
    Assert_isTrue(!module->exportBuffers.elem[idx].full, NULL);
    Log_write2(Export_LM_exportBuffer, idx, (IArg)module->exportBuffers.elem[idx].addr);
    module->exportBuffers.elem[idx].full = TRUE;
    Swi_post(module->exportBuffersSwi);
}

Void Export_exportAllBuffers()
{
    UInt8 i;
    Log_write0(Export_LM_exportAllBuffers);
    for (i = 0; i < module->exportBuffers.length; ++i)
        Export_exportBuffer(i);
}

Void Export_resetBufferSequenceNum()
{
    Log_write0(Export_LM_resetBufferSequenceNum);
    module->bufferSeqNum = 0;
}

static Void initUART()
{
    const UartPort_Info *uartPort = UartPort_getInfo(module->uartPort);
    const GpioPort_Info *rxPin = GpioPort_getInfo(uartPort->rxPin);
    const GpioPort_Info *txPin = GpioPort_getInfo(uartPort->txPin);
    const GpioPeriph_Info *rxPeriph = GpioPeriph_getInfo(rxPin->periph);
    const GpioPeriph_Info *txPeriph = GpioPeriph_getInfo(txPin->periph);

    Log_write8(Export_LM_initUART, (IArg)uartPort->base, uartPort->periph,
               (IArg)txPeriph->base, txPeriph->periph, txPin->pin,
               uartPort->pinAssignTx, Export_uartBaudRate, Export_systemClockHz.lo);

    SysCtlPeripheralEnable(rxPeriph->periph);
    SysCtlPeripheralEnable(txPeriph->periph);
    SysCtlPeripheralEnable(uartPort->periph);
    GPIOPinConfigure(uartPort->pinAssignRx);
    GPIOPinConfigure(uartPort->pinAssignTx);
    GPIOPinTypeUART(rxPeriph->base, rxPin->pin);
    GPIOPinTypeUART(txPeriph->base, txPin->pin);

    UARTConfigSetExpClk(uartPort->base, Export_systemClockHz.lo, Export_uartBaudRate,
                        UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE |
                        UART_CONFIG_PAR_NONE);
}

static Void initUDMA()
{
    const UartPort_Info *uartPort = UartPort_getInfo(module->uartPort);

    Log_write2(Export_LM_initUDMA, uartPort->base, uartPort->udmaChanTx);

    SysCtlPeripheralEnable(SYSCTL_PERIPH_UDMA);
    IntEnable(INT_UDMAERR);
    uDMAEnable();
    uDMAControlBaseSet(dmaControlTable);

    // Set both the TX and RX trigger thresholds to 4.  This will be used by
    // the uDMA controller to signal when more data should be transferred.  The
    // uDMA TX and RX channels will be configured so that it can transfer 4
    // bytes in a burst when the UART is ready to transfer more data.
    UARTFIFOLevelSet(uartPort->base, UART_FIFO_TX4_8, UART_FIFO_RX4_8);
    UARTDMAEnable(uartPort->base, UART_DMA_TX);
    // IntEnable(uartPort->interrupt); // TODO: should not be needed

    uDMAChannelAttributeDisable(uartPort->udmaChanTx,
                                UDMA_ATTR_ALTSELECT |
                                UDMA_ATTR_HIGH_PRIORITY |
                                UDMA_ATTR_REQMASK);
    uDMAChannelAttributeEnable(uartPort->udmaChanTx, UDMA_ATTR_USEBURST);

    uDMAChannelAttributeEnable(uartPort->udmaChanTx, UDMA_ATTR_USEBURST);
    uDMAChannelControlSet(uartPort->udmaChanTx | UDMA_PRI_SELECT,
                          UDMA_SIZE_8 | UDMA_SRC_INC_8 | UDMA_DST_INC_NONE |
                          UDMA_ARB_4);
}

Void Export_initBuffer(UInt bufId, UInt8 *addr)
{
    Export_ExportBuffer *buf;

    Assert_isTrue(bufId < module->exportBuffers.length, NULL);
    Assert_isTrue(addr != NULL, NULL);

    Log_write2(Export_LM_initBuffer, bufId, (IArg)addr);

    buf = &module->exportBuffers.elem[bufId];
    bufWriteFixedHeader(addr, buf->size, buf->userId);
    buf->addr = addr;
}

Int Export_Module_startup(Int state)
{
    Log_write0(Export_LM_startup);
    initUART();
    initUDMA();
    return Startup_DONE;
}
