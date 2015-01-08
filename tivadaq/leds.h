// Indices into gpioHwAttrs array
typedef enum {
    LED_RED = 0,
    LED_BLUE,
    LED_GREEN,
    LED_COUNT,
} Led;

Void initLeds(Void);
Void setLed(Led led, Bool on);
