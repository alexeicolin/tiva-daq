#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <xdc/runtime/Assert.h>
#include <xdc/runtime/Startup.h>
#include <ti/drivers/UART.h>
#include <ti/drivers/uart/UARTTiva.h>

#include <platforms/tiva/hw/UartPort.h>
#include <platforms/tiva/hw/GpioPort.h>
#include <platforms/tiva/hw/GpioPeriph.h>

#include <stdint.h>
#include <stdbool.h>
#include <inc/hw_memmap.h>
#include <inc/hw_ints.h>
#include <inc/hw_gpio.h>
#include <driverlib/gpio.h>
#include <driverlib/sysctl.h>
#include <driverlib/pin_map.h>

#include "package/internal/Console.xdc.h"

#define UART_COUNT 1
#define UART_INDEX 0

static UARTTiva_Object uartTivaObjects[UART_COUNT];
static UARTTiva_HWAttrs uartTivaHWAttrs[UART_COUNT];

/* Global symbol for ti.drivers.UART */
const UART_Config UART_config[] = {
    {
        &UARTTiva_fxnTable,
        &uartTivaObjects[UART_INDEX],
        &uartTivaHWAttrs[UART_INDEX]
    },
    {NULL, NULL, NULL}
};

Void Console_open()
{
    UART_Params uartParams;
    const UartPort_Info *uartPort = UartPort_getInfo(module->uartPort);
    const GpioPort_Info *rxPin = GpioPort_getInfo(uartPort->rxPin);
    const GpioPort_Info *txPin = GpioPort_getInfo(uartPort->txPin);
    const GpioPeriph_Info *rxPeriph = GpioPeriph_getInfo(rxPin->periph);
    const GpioPeriph_Info *txPeriph = GpioPeriph_getInfo(txPin->periph);

    uartTivaHWAttrs[UART_INDEX].baseAddr = uartPort->base;
    uartTivaHWAttrs[UART_INDEX].intNum = uartPort->intNum;

    SysCtlPeripheralEnable(rxPeriph->periph);
    SysCtlPeripheralSleepEnable(rxPeriph->periph);
    SysCtlPeripheralDeepSleepEnable(rxPeriph->periph);

    SysCtlPeripheralEnable(txPeriph->periph);
    SysCtlPeripheralSleepEnable(txPeriph->periph);
    SysCtlPeripheralDeepSleepEnable(txPeriph->periph);

    SysCtlPeripheralEnable(uartPort->periph);
    SysCtlPeripheralSleepEnable(uartPort->periph);
    SysCtlPeripheralDeepSleepEnable(uartPort->periph);

    GPIOPinConfigure(uartPort->pinAssignRx);
    GPIOPinConfigure(uartPort->pinAssignTx);

    GPIOPinTypeUART(rxPeriph->base, rxPin->pin);
    GPIOPinTypeUART(txPeriph->base, txPin->pin);

    UART_init();

    UART_Params_init(&uartParams);
    uartParams.baudRate = Console_uartPortBaudRate;
    uartParams.writeDataMode = UART_DATA_TEXT;
    module->uart = UART_open(UART_INDEX, &uartParams);
    Assert_isTrue(module->uart != NULL, NULL);
}

Void Console_close()
{
    Assert_isTrue(module->uart != NULL, NULL);
    UART_close((UART_Handle)module->uart);
}

Void Console_output(/* const */ Char *buf, UInt size)
{
    UART_writePolling((UART_Handle)module->uart, buf, size);
}

Void Console_flush()
{
    System_flush();
}

Int Console_Module_startup(Int state)
{
    Console_open();
    return Startup_DONE;
}
