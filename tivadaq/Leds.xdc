package tivadaq;

import platforms.tiva.hw.GpioPort;

@ModuleStartup
module Leds {
    metaonly config UInt32 blinkTickPeriodMs = 500;
    config UInt32 pulseDelayIters = 100000;


    Void setLed(UInt led, Bool on);
    Void pulseLed(UInt led);
    Void blinkLed(UInt led, UInt32 rate);

    metaonly config Any gpioPorts;

  internal:

    struct LedState {
        GpioPort.Handle gpioPort;
        Bool on;
        UInt32 blinkRate;
    };

    struct Module_State {
        LedState ledState[length];
        UInt32 blinkTicks;
    };
}
