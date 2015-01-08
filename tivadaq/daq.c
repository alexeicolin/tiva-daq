#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <xdc/runtime/Assert.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Clock.h>

#include <xdc/cfg/global.h> // globals defined in .cfg file, prefixed with 'g_' 

#include <Leds.h>
#include <Daq.h>

#include "console.h"

static Bool isRunning = FALSE;

Void onException(Void *excp)
{
    Leds_setLed(g_faultLed, TRUE);
}

Void onAbort()
{
    Leds_setLed(g_faultLed, TRUE);
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
    Leds_blinkLed(g_statusLed, g_blinkRateRunning);
    isRunning = TRUE;
}

static Void stop()
{
    Clock_stop(tempClockObj);
    Daq_stop();
    Leds_blinkLed(g_statusLed, g_blinkRateStopped);
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
    Leds_setLed(g_txLed, TRUE);
}

Void onExportTxCompleted()
{
    Leds_setLed(g_txLed, FALSE);
}

Int main(Int argc, Char* argv[])
{
    openConsole();
    Leds_pulseLed(g_statusLed);
    Swi_post(startStopSwi);
    BIOS_start();
    return 0;
}
