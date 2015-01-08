#include <xdc/std.h>
#include <xdc/runtime/Startup.h>
#include <ti/drivers/GPIO.h>

#include <stdint.h>
#include <stdbool.h>
#include <inc/hw_memmap.h>
#include <driverlib/gpio.h>
#include <driverlib/sysctl.h>

#include "package/internal/Leds.xdc.h"

#define LED_OFF (0)
#define LED_ON  (~0)

static const GPIO_HWAttrs gpioHWAttrs[Leds_Led_COUNT] = {
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

Void Leds_setLed(Leds_Led led, Bool on)
{
    GPIO_write(led, on ? LED_ON : LED_OFF);
}

Void Leds_pulseLed(Leds_Led led)
{
    Int i = Leds_pulseDelayIters;
    Leds_setLed(led, TRUE);
    while (i--);
    Leds_setLed(led, FALSE);
}

Void Leds_blinkLed(Leds_Led led, UInt32 rate)
{
    Leds_LedState *ledState = &module->ledState[led];
    ledState->on = FALSE;
    ledState->blinkRate = rate;
}

Void Leds_blinkTick(UArg arg)
{
    Int led;
    Leds_LedState *ledState;
    for (led = 0; led < Leds_Led_COUNT; ++led) {
        ledState = &module->ledState[led];
        if (ledState->blinkRate &&
            module->blinkTicks % ledState->blinkRate == 0) {
            ledState->on = !ledState->on;
            Leds_setLed(led, ledState->on);
        }
    }
    module->blinkTicks++;
}

Int Leds_Module_startup(Int state)
{
    Leds_Led led;

    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);

    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_1); /* LED_RED */
    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_2); /* LED_GREEN */
    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_3); /* LED_BLUE */

    GPIO_init(); /* Once GPIO_init is called, GPIO_config cannot be changed */

    for (led = 0; led < Leds_Led_COUNT; ++led)
        GPIO_write(led, LED_OFF);

    return Startup_DONE;
}
