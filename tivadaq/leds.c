#include <xdc/std.h>
#include <ti/drivers/GPIO.h>

#include <stdint.h>
#include <stdbool.h>
#include <inc/hw_memmap.h>
#include <driverlib/gpio.h>
#include <driverlib/sysctl.h>

#include "leds.h"

#define LED_OFF (0)
#define LED_ON  (~0)

static const GPIO_HWAttrs gpioHWAttrs[LED_COUNT] = {
    {GPIO_PORTF_BASE, GPIO_PIN_1, GPIO_OUTPUT}, /* LED_RED */
    {GPIO_PORTF_BASE, GPIO_PIN_2, GPIO_OUTPUT}, /* LED_BLUE */
    {GPIO_PORTF_BASE, GPIO_PIN_3, GPIO_OUTPUT}, /* LED_GREEN */
};

// Globally visible symbol for ti.drivers.GPIO
const GPIO_Config GPIO_config[] = {
    {&gpioHWAttrs[0]},
    {&gpioHWAttrs[1]},
    {&gpioHWAttrs[2]},
    {NULL},
};

Void setLed(Led led, Bool on)
{
    GPIO_write(led, on ? LED_ON : LED_OFF);
}

Void initLeds(Void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);

    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_1); /* LED_RED */
    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_2); /* LED_GREEN */
    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_3); /* LED_BLUE */

    GPIO_init(); /* Once GPIO_init is called, GPIO_config cannot be changed */

    GPIO_write(LED_RED, LED_OFF);
    GPIO_write(LED_GREEN, LED_OFF);
    GPIO_write(LED_BLUE, LED_OFF);
}
