#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <xdc/runtime/Assert.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Clock.h>

#include <xdc/cfg/global.h>

#include <stdbool.h>
#include <stdint.h>

#include <inc/hw_memmap.h>
#include <inc/hw_ints.h>
#include <driverlib/gpio.h>
#include <driverlib/pin_map.h>
#include <driverlib/sysctl.h>
#include <driverlib/udma.h>
#include "driverlib/timer.h"
#include "driverlib/adc.h"
#include <driverlib/interrupt.h>

#include "Board.h"

#include "delay.h"
#include "debounce.h"
#include "profile.h"
#include "daq.h"

/* Choose an event to blink the blue LED on */
/* #define BLINK_LED_ON_TEMP_SAMPLE */
#define BLINK_LED_ON_BLINKER

static UInt32 blinkRateDivisor = 1;

/* Load profile config */

#define PROFILE_TICKS_PER_SEC     10
#define PWM_FREQ_HZ           100000

struct ProfileInterval profiles[][MAX_PROFILE_LEN] = {
    /* length (ms), duty cycle (%) */

    /* Profile A */
    {
        { 100, 50 },
    },
    /* Profile B */
    {
        { 100, 25 },
    }
};

#define NUM_PROFILES (sizeof(profiles) / sizeof(profiles[0]))

/* ADC data buffers */

#define BUF_SIZE_VBAT 1024
#define BUF_SIZE_TEMP   16

static UInt8 bufVbat[NUM_BUFS_PER_SEQ][BUF_SIZE_VBAT];
static UInt8 bufTemp[NUM_BUFS_PER_SEQ][BUF_SIZE_TEMP];

#define SAMPLES_PER_SEC 10

#define TEMP_SEQUENCER 3
#define BATT_SEQUENCER 0

/* Sequencer configuration: (adc, seq) -> input samples, output buffer */
static const struct AdcConfig adcConfig = {
    {
        {
            [BATT_SEQUENCER] = {
                    TRUE, /* enabled */
                    0, /* priority */
                    ADC_TRIGGER_TIMER,
                    {
                        ADC_CTL_CH0,
                        ADC_CTL_CH1,
                        ADC_CTL_CH2,
                        ADC_CTL_CH3,
                        ADC_CTL_CH4,
                        ADC_CTL_CH5,
                        ADC_CTL_CH6,
                        ADC_CTL_CH7,
                        ADC_SEQ_END
                    },
                    { &bufVbat[0][0], BUF_SIZE_VBAT }
            }
        },
        {
            [TEMP_SEQUENCER] = {
                    TRUE, /* enabled */
                    0, /* priority */
                    ADC_TRIGGER_PROCESSOR,
                    {
                       ADC_CTL_TS,
                       ADC_SEQ_END
                    },
                    { &bufTemp[0][0], BUF_SIZE_TEMP }
            }
        }
    },

    TIMER1_BASE, TIMER_B /* trigger timer */
};

/* TODO: split this up somehow */
static Void initADCandProfileGenTimers()
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER1);
    TimerConfigure(TIMER1_BASE, TIMER_CFG_SPLIT_PAIR |
                                TIMER_CFG_A_PERIODIC | TIMER_CFG_B_PERIODIC);
}

static Void startADCandProfileGen()
{
    TimerEnable(TIMER1_BASE, TIMER_BOTH);
    startProfileGen();
}

static Void stopADCandProfileGen()
{
    TimerDisable(TIMER1_BASE, TIMER_BOTH);
    stopProfileGen();
}

Void onDMAError(UArg arg)
{
    UInt32 status = uDMAErrorStatusGet();
    Assert_isTrue(!status, NULL); /* we don't tolerate errors */
    uDMAErrorStatusClear();
}

Void sampleTemp(UArg arg)
{
#ifdef BLINK_LED_ON_TEMP_SAMPLE
    static Bool ledOn = FALSE;
    GPIO_write(EK_TM4C123GXL_LED_BLUE, ledOn ? Board_LED_ON : Board_LED_OFF);
    ledOn = !ledOn;
#endif

    ADCProcessorTrigger(ADC0_BASE, TEMP_SEQUENCER);
}


Void stop(UArg arg)
{
    stopADCandProfileGen();
    Clock_stop(tempClockObj);
    Clock_stop(drainedClockObj);
    GPIO_write(EK_TM4C123GXL_LED_BLUE, Board_LED_ON);
}

Void blinkLed(UArg arg)
{
#ifdef BLINK_LED_ON_BLINKER
    static Bool ledOn = FALSE;
    static UInt32 tick = 0;
    if (tick % blinkRateDivisor == 0) {
        GPIO_write(EK_TM4C123GXL_LED_BLUE,
                   ledOn ? Board_LED_ON : Board_LED_OFF);
        ledOn = !ledOn;
    }
    tick++;
#endif
}

Int app(Int argc, Char* argv[])
{
    initADCandProfileGenTimers();
    initDAQ(&adcConfig, SAMPLES_PER_SEC);
    initProfileGen(profiles, NUM_PROFILES,
                   PROFILE_TICKS_PER_SEC, PWM_FREQ_HZ);

    startADCandProfileGen();
    return 0;
}
