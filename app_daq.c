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
#include "driverlib/adc.h"
#include <driverlib/interrupt.h>

#include "Board.h"

#include "delay.h"
#include "debounce.h"
#include "profile.h"
#include "daq.h"

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
                    TRUE, /* enabled */
                    0, /* priority */
                    {
                        ADC_CTL_CH0,
                        ADC_CTL_CH1,
                        ADC_CTL_CH2,
                        ADC_CTL_CH3,
                        ADC_SEQ_END
                    },
                    { &bufVpos[0][0], BUF_SIZE_VPOS }
            }
        },
        {
            [2] = {
                    TRUE, /* enabled */
                    0, /* priority */
                    {
                        ADC_CTL_CH0,
                        ADC_CTL_CH1,
                        ADC_CTL_CH2,
                        ADC_CTL_CH3,
                        ADC_SEQ_END
                    },
                    { &bufVneg[0][0], BUF_SIZE_VNEG }
            }
        }
    }
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

Void onDMAError(UArg arg)
{
    UInt32 status = uDMAErrorStatusGet();
    Assert_isTrue(!status, NULL); /* we don't tolerate errors */
    uDMAErrorStatusClear();
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
