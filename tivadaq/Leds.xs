var Clock;
var GPIO;
var GpioPort;

function module$meta$init()
{
    Clock = xdc.useModule('ti.sysbios.knl.Clock');
    GPIO = xdc.useModule('ti.drivers.GPIO');
    GpioPort = xdc.useModule('platforms.tiva.hw.GpioPort');
}

function module$static$init(state, mod)
{
    var blinkClockObj = Clock.create('&Leds_blinkTick', 1);
    blinkClockObj.period = msecToClockTicks(mod.blinkTickPeriodMs);
    blinkClockObj.startFlag = true;

    state.blinkTicks = 0;
    state.ledState.length = mod.gpioPorts.length;
    for (var i = 0; i < mod.gpioPorts.length; ++i) {

        var gpioPort = mod.gpioPorts[i];
        state.ledState[i] =
            {
             gpioPort: GpioPort.create(gpioPort.port, gpioPort.pin),
             on: false,
             blinkRate: 0,
            };
    }
}

function msecToClockTicks(ms)
{
    var usPerTick = Clock.tickPeriod;
    return (ms * 1000) / usPerTick;
}
