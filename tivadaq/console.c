#include <xdc/std.h>
#include <xdc/runtime/Assert.h>
#include <ti/drivers/UART.h>
#include <ti/drivers/uart/UARTTiva.h>

#include <stdint.h>
#include <stdbool.h>
#include <inc/hw_memmap.h>
#include <inc/hw_ints.h>
#include <inc/hw_gpio.h>
#include <driverlib/gpio.h>
#include <driverlib/sysctl.h>
#include <driverlib/pin_map.h>

#include "console.h"

#define UART_COUNT 1

#define UART_INDEX 0 // index into UART_config
#define UART_PERIPH SYSCTL_PERIPH_UART0
#define UART_BASE UART0_BASE
#define UART_INT INT_UART0
#define UART_GPIO_PERIPH SYSCTL_PERIPH_GPIOA
#define UART_GPIO_BASE GPIO_PORTA_BASE
#define UART_PIN_TX GPIO_PA0_U0RX
#define UART_PIN_RX GPIO_PA1_U0TX
#define UART_PINS (GPIO_PIN_0 | GPIO_PIN_1)

static UART_Handle uart;
static UARTTiva_Object uartTivaObjects[UART_COUNT];

static const UARTTiva_HWAttrs uartTivaHWAttrs[UART_COUNT] = {
    {UART_BASE, UART_INT},
};

/* Global symbol for ti.drivers.UART */
const UART_Config UART_config[] = {
    {
        &UARTTiva_fxnTable,
        &uartTivaObjects[0],
        &uartTivaHWAttrs[0]
    },
    {NULL, NULL, NULL}
};

Void openConsole()
{
    UART_Params uartParams;

    SysCtlPeripheralEnable(UART_GPIO_PERIPH);
    SysCtlPeripheralEnable(UART_PERIPH);
    SysCtlPeripheralSleepEnable(UART_PERIPH);
    SysCtlPeripheralDeepSleepEnable(UART_PERIPH);

    GPIOPinConfigure(UART_PIN_RX);
    GPIOPinConfigure(UART_PIN_TX);
    GPIOPinTypeUART(UART_GPIO_BASE, UART_PINS);

    UART_init();

    UART_Params_init(&uartParams);
    uartParams.writeDataMode = UART_DATA_TEXT;
    uart = UART_open(UART_INDEX, &uartParams);
    Assert_isTrue(uart != NULL, NULL);
}

Void closeConsole()
{
    Assert_isTrue(uart != NULL, NULL);
    UART_close(uart);
}

Void outputToConsole(const Char *buf, UInt size)
{
    UART_writePolling(uart, buf, size);
}
