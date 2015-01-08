package tivadaq;

@ModuleStartup
module Leds {
    enum Led {
        Led_RED = 0,
        Led_BLUE,
        Led_GREEN,
        Led_COUNT
    };

    metaonly readonly config UInt32 blinkTickPeriodMs = 500;
    readonly config UInt32 pulseDelayIters = 100000;

    Void setLed(Led led, Bool on);
    Void pulseLed(Led led);
    Void blinkLed(Led led, UInt32 rate);

  internal:

    struct LedState {
        Bool on;
        UInt32 blinkRate;
    };

    struct Module_State {
        LedState ledState[Led_COUNT];
        UInt32 blinkTicks;
    };
}
