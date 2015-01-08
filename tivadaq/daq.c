#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <xdc/runtime/Assert.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Clock.h>

#include <xdc/cfg/global.h>

#include <Leds.h>
#include <Daq.h>

#include "console.h"

#define STATUS_LED Leds_Led_BLUE // blink rate shows DAQ is acquiring or idle
#define FAULT_LED  Leds_Led_RED
#define TX_LED     Leds_Led_GREEN // data buffer being transmitted via UART

/* Different blink rate divisors to encode multiple states onto one LED */
#define BLINK_RATE_STOPPED 2
#define BLINK_RATE_RUNNING 1

static Bool isRunning = FALSE;

Void onException(Void *excp)
{
    Leds_setLed(FAULT_LED, TRUE);
}

Void onAbort()
{
    Leds_setLed(FAULT_LED, TRUE);
    System_abortStd();
}

Void onExit(Int status)
{
    closeConsole();
    System_exitStd(status);
}

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
    Leds_blinkLed(STATUS_LED, BLINK_RATE_RUNNING);
    isRunning = TRUE;
}

static Void stop()
{
    Clock_stop(tempClockObj);
    Daq_stop();
    Leds_blinkLed(STATUS_LED, BLINK_RATE_STOPPED);
    isRunning = FALSE;
}

Void startStop(UArg arg)
{
    if (!isRunning)
        start();
    else
        stop();
}

Void onExportTxQueued()
{
    Leds_setLed(TX_LED, TRUE);
}

Void onExportTxCompleted()
{
    Leds_setLed(TX_LED, FALSE);
}

Int main(Int argc, Char* argv[])
{
    openConsole();
    Leds_pulseLed(STATUS_LED);
    Swi_post(startStopSwi);
    BIOS_start();
    return 0;
}
