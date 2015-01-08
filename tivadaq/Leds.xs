var Clock;
var GPIO;

function module$meta$init()
{
    Clock = xdc.useModule('ti.sysbios.knl.Clock');
    GPIO = xdc.useModule('ti.drivers.GPIO');
}

function module$static$init(state, mod)
{
    var blinkClockObj = Clock.create('&Leds_blinkTick', 1);
    blinkClockObj.period = msecToClockTicks(mod.blinkTickPeriodMs);
    blinkClockObj.startFlag = true;

    state.blinkTicks = 0;
    for (var i = 0; i < mod.Led_COUNT; ++i)
        state.ledState[i] = {on: false, blinkRate: 0};
}

function msecToClockTicks(ms)
{
    var usPerTick = Clock.tickPeriod;
    return (ms * 1000) / usPerTick;
}
