#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <xdc/runtime/Assert.h>
#include <ti/sysbios/knl/Task.h>

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
#include <driverlib/interrupt.h>

#include "Board.h"

#include "delay.h"
#include "debounce.h"
#include "pwm.h"
#include "profiles.h"
#include "daq.h"

/* Load profile config */

#define PROFILE_TICKS_PER_SEC     10
#define PWM_FREQ_HZ           100000

#define PROFILE_LED_BLINK_RATE_TICKS (PROFILE_TICKS_PER_SEC / 2) /* 0.5 sec */
#define PROFILE_LED_ON_TICKS         (PROFILE_TICKS_PER_SEC / 10) /* 0.1 sec */

#define NUM_PROFILES 2

/* ADC data buffers */

#define BUF_SIZE_VPOS 1024
#define BUF_SIZE_VNEG 1024

static UInt8 bufVpos[NUM_BUFS_PER_SEQ][BUF_SIZE_VPOS];
static UInt8 bufVneg[NUM_BUFS_PER_SEQ][BUF_SIZE_VPOS];

#define SAMPLES_PER_SEC 10

/* Sequencer configuration: (adc, seq) -> input samples, output buffer */
static const struct AdcConfig adcConfig = {
    {
        {
            [1] = {
                    {
                        ADC_SAMPLE_CH0,
                        ADC_SAMPLE_CH1,
                        ADC_SAMPLE_CH2,
                        ADC_SAMPLE_CH3
                    },
                    { &bufVpos[0][0], BUF_SIZE_VPOS }
            }
        },
        {
            [2] = {
                    {
                        ADC_SAMPLE_CH0,
                        ADC_SAMPLE_CH1,
                        ADC_SAMPLE_CH2,
                        ADC_SAMPLE_CH3
                    },
                    { &bufVneg[0][0], BUF_SIZE_VNEG }
            }
        }
    }
};

static Void initADCandProfileGenTimers()
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER1);
    TimerConfigure(TIMER1_BASE, TIMER_CFG_SPLIT_PAIR |
                                TIMER_CFG_A_PERIODIC | TIMER_CFG_B_PERIODIC);
}

static Void startADCandProfileGen()
{
    /* TODO: synchronize timers */
    startPwm();
    TimerEnable(TIMER1_BASE, TIMER_BOTH);
}

static Void initProfileGen(UInt32 samplesPerSec)
{
    /* See discurssion about prescaler in initADC */
    UInt32 divisor = 128; /* appropriate for 10 to 1000 ticks/sec */
    UInt32 prescaler = divisor - 1;
    UInt32 period = SysCtlClockGet() / divisor / samplesPerSec;

    Assert_isTrue(samplesPerSec >= 10 && samplesPerSec <= 1000, NULL);
    Assert_isTrue(SysCtlClockGet() % divisor == 0, NULL);

    TimerPrescaleSet(TIMER1_BASE, TIMER_A, prescaler);
    TimerLoadSet(TIMER1_BASE, TIMER_A, period);
    TimerIntEnable(TIMER1_BASE, TIMER_TIMA_TIMEOUT);

    initPwm(PWM_FREQ_HZ, 1 /* % */);
}

Void onProfileTick(UArg arg)
{

    /* Cursors into the profile waveform */
    static Int intervalIdx[NUM_PROFILES] = {0};
    static Int intervalPos[NUM_PROFILES] = {0};
    static UInt32 ticks = 0;
    static UInt32 ledOnTicks = 0;

    Int profileIdx;
    struct ProfileInterval *profile;

    if (ticks % PROFILE_LED_BLINK_RATE_TICKS == 0 && !ledOnTicks) {
        GPIO_write(EK_TM4C123GXL_LED_BLUE, Board_LED_ON);
        ledOnTicks = PROFILE_LED_ON_TICKS;
    }

    for (profileIdx = 0; profileIdx < numProfiles; ++profileIdx) {
        profile = profiles[profileIdx];

        if (profile[intervalIdx[profileIdx]].value !=
                getPwmDutyCycle(profileIdx))
            setPwmDutyCycle(profileIdx, profile[intervalIdx[profileIdx]].value);

        intervalPos[profileIdx]++;
        if (intervalPos[profileIdx] ==
                profile[intervalIdx[profileIdx]].length) {
            intervalIdx[profileIdx]++;
            if (!profile[intervalIdx[profileIdx]].length) /* last interval */
                intervalIdx[profileIdx] = 0;
            intervalPos[profileIdx] = 0;
        }

    }

    TimerIntClear(TIMER1_BASE, TIMER_TIMA_TIMEOUT);

    if (!ledOnTicks)
        GPIO_write(EK_TM4C123GXL_LED_BLUE, Board_LED_OFF);
    if (ledOnTicks)
        ledOnTicks--;

    ticks++;
}

Void onDMAError(UArg arg)
{
    UInt32 status = uDMAErrorStatusGet();
    Assert_isTrue(!status, NULL); /* we don't tolerate errors */
    uDMAErrorStatusClear();
}

Int app(Int argc, Char* argv[])
{
    Assert_isTrue(NUM_PROFILES == numProfiles, NULL);
    convertProfileToTicks(PROFILE_TICKS_PER_SEC);

    initADCandProfileGenTimers();
    initDAQ(&adcConfig, SAMPLES_PER_SEC);
    initProfileGen(PROFILE_TICKS_PER_SEC);

    startADCandProfileGen();
    return 0;
}
