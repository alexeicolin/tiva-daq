#include <xdc/std.h>
#include <ti/drivers/GPIO.h>

#include <stdint.h>
#include <stdbool.h>
#include <inc/hw_memmap.h>
#include <driverlib/gpio.h>
#include <driverlib/sysctl.h>

#include "leds.h"

#define PULSE_DELAY_ITERS 100000

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

struct LedState {
    Bool on;
    UInt32 blinkRate;
};

static struct LedState ledState[LED_COUNT];
static UInt32 blinkTicks = 0;

Void setLed(Led led, Bool on)
{
    GPIO_write(led, on ? LED_ON : LED_OFF);
}

Void pulseLed(Led led)
{
    Int i = PULSE_DELAY_ITERS;
    setLed(led, TRUE);
    while (i--);
    setLed(led, FALSE);
}

Void blinkLed(Led led, UInt32 rate)
{
    ledState[led].on = FALSE;
    ledState[led].blinkRate = rate;
}

Void blinkTick(UArg arg)
{
    Int led;
    for (led = 0; led < LED_COUNT; ++led) {
        if (ledState[led].blinkRate &&
            blinkTicks % ledState[led].blinkRate == 0) {
            ledState[led].on = !ledState[led].on;
            setLed(led, ledState[led].on);
        }
    }
    blinkTicks++;
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
