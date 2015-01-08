#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <xdc/runtime/Assert.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Clock.h>

#include <xdc/cfg/global.h>

#include <Daq.h>

#include "leds.h"

/* Different blink rate divisors to encode multiple states onto one LED */
#define BLINK_RATE_STOPPED 2
#define BLINK_RATE_RUNNING 1

#define PULSE_DELAY_ITERS 100000

static Bool isRunning = FALSE;
static UInt32 blinkRateDivisor = BLINK_RATE_STOPPED;

Void onDMAError(UArg arg)
{
    UInt32 status = uDMAErrorStatusGet();
    Assert_isTrue(!status, NULL); /* we don't tolerate errors */
    uDMAErrorStatusClear();
}

Void sampleTemp(UArg arg)
{
    Daq_trigger(g_tempAdc, g_tempSeq);
}

static Void start()
{
    Daq_start();
    Clock_start(tempClockObj);
    blinkRateDivisor = BLINK_RATE_RUNNING;
    isRunning = TRUE;
}

static Void stop()
{
    Clock_stop(tempClockObj);
    Daq_stop();
    blinkRateDivisor = BLINK_RATE_STOPPED;
    isRunning = FALSE;
}

Void startStop(UArg arg)
{
    if (!isRunning)
        start();
    else
        stop();
}

Void blinkLed(UArg arg)
{
    static Bool ledOn = FALSE;
    static UInt32 tick = 0;
    if (tick % blinkRateDivisor == 0) {
        ledOn = !ledOn;
        setLed(LED_BLUE, ledOn);
    }
    tick++;
}

Void onExportTxQueued()
{
    setLed(LED_GREEN, TRUE);
}

Void onExportTxCompleted()
{
    setLed(LED_GREEN, FALSE);
}

Void pulseLed()
{
    Int i = PULSE_DELAY_ITERS;
    setLed(LED_BLUE, TRUE);
    while (i--);
    setLed(LED_BLUE, FALSE);
}

Int main(Int argc, Char* argv[])
{
    initLeds();
    pulseLed();
    Swi_post(startStopSwi);
    BIOS_start();
    return 0;
}
