#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <xdc/runtime/Assert.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/hal/Hwi.h>

#include <xdc/cfg/global.h>

#include <stdbool.h>
#include <stdint.h>

#include <inc/hw_memmap.h>
#include <inc/hw_uart.h>
#include <inc/hw_ints.h>
#include <inc/hw_adc.h>
#include <driverlib/adc.h>
#include <driverlib/gpio.h>
#include <driverlib/pin_map.h>
#include <driverlib/sysctl.h>
#include "driverlib/timer.h"
#include <driverlib/udma.h>
#include <driverlib/uart.h>
#include <driverlib/interrupt.h>

#include "delay.h"
#include "debounce.h"
#include "pwm.h"
#include "profiles.h"
#include "dma.h"
#include "export.h"
#include "Board.h"

#define CONCAT_INNER(a, b) a ## b
#define CONCAT(a, b) CONCAT_INNER(a, b)
#define CONCAT3_INNER(a, b, c) a ## b ## c
#define CONCAT3(a, b, c) CONCAT3_INNER(a, b, c)
#define CONCATU3_INNER(a, b, c) a ## _ ## b ## _ ## c
#define CONCATU3(a, b, c) CONCATU3_INNER(a, b, c)

#define NUM_ADCS 2
#define NUM_SAMPLE_BUFFERS 2
#define SAMPLE_SEQ_LEN 8 /* samples */
#define SAMPLE_SIZE 2    /* bytes */
#define SAMPLE_BUFFER_SIZE 16 /* in sample seqs (1 seq = SAMPLE_SEQ_LEN samples) */
#define SAMPLE_BUFFER_SIZE_BYTES (SAMPLE_BUFFER_SIZE * SAMPLE_SEQ_LEN * SAMPLE_SIZE)

static UInt16 samples[NUM_ADCS]
                     [NUM_SAMPLE_BUFFERS]
                     [SAMPLE_BUFFER_SIZE]
                     [SAMPLE_SEQ_LEN];

static struct ExportBuffer expBuffers[] = {
    { (UChar *)&samples[0][0], SAMPLE_BUFFER_SIZE_BYTES },
    { (UChar *)&samples[0][1], SAMPLE_BUFFER_SIZE_BYTES },
    { (UChar *)&samples[1][0], SAMPLE_BUFFER_SIZE_BYTES },
    { (UChar *)&samples[1][1], SAMPLE_BUFFER_SIZE_BYTES },
    { NULL, 0 }
};

#define PROFILE_TICKS_PER_SEC     10
#define SAMPLES_PER_SEC           10
#define PWM_FREQ_HZ           100000

#define PROFILE_LED_BLINK_RATE_TICKS (PROFILE_TICKS_PER_SEC / 2) /* 0.5 sec */
#define PROFILE_LED_ON_TICKS         (PROFILE_TICKS_PER_SEC / 10) /* 0.1 sec */

#define NUM_PROFILES 2

#define ADC_SEQUENCER_IDX 0

// The names in driverlip/udma.h are irregular, so fix them up
#define UDMA_CHANNEL_ADC_0_0 UDMA_CHANNEL_ADC0
#define UDMA_CHANNEL_ADC_1_0 UDMA_SEC_CHANNEL_ADC10

// From driverlib/adc.c
#define ADC_SEQ                 (ADC_O_SSMUX0)
#define ADC_SEQ_STEP            (ADC_O_SSMUX1 - ADC_O_SSMUX0)
#define ADC_SSFIFO              (ADC_O_SSFIFO0 - ADC_O_SSMUX0)

#define ADC_BASE(adc) CONCAT3(ADC, adc, _BASE)
#define ADC_CHANNEL(adc) CONCATU3(UDMA_CHANNEL_ADC, adc, ADC_SEQUENCER_IDX)

static inline UInt32 adcBaseAddr(Int adc)
{
    switch (adc) {
        case 0: return ADC_BASE(0);
        case 1: return ADC_BASE(1);
        default: Assert_isTrue(FALSE, NULL);
    }
    return ~0; /* unreachable */
}

static inline UInt32 adcChanAddr(Int adc)
{
    switch (adc) {
        case 0: return ADC_CHANNEL(0);
        case 1: return ADC_CHANNEL(1);
        default: Assert_isTrue(FALSE, NULL);
    }
    return ~0; /* unreachable */
}

#if 0
static inline float singleAdcToV(UInt32 adcOut)
{
    return (float)adcOut * 3.3 / 0xFFF;
}

static inline float diffAdcToV(UInt32 adcOut)
{
    return ((float)adcOut - 0x800) * 3.3 / (0xFFF - 0x800);
}
#endif

static Void initADCandProfileGenTimers()
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER1);
    TimerConfigure(TIMER1_BASE, TIMER_CFG_SPLIT_PAIR |
                                TIMER_CFG_A_PERIODIC | TIMER_CFG_B_PERIODIC);
}

static Void startBufferTransfer(Int idx);

static Void setupDMAADCTransfer(Int adc, UInt32 controlSelect, Int bufIdx)
{
    UInt adcBase = adcBaseAddr(adc);
    UInt adcChan = adcChanAddr(adc);

    uDMAChannelTransferSet(adcChan | controlSelect,
        UDMA_MODE_PINGPONG,
        (void *)(adcBase + ADC_SEQ +
            ADC_SEQ_STEP * ADC_SEQUENCER_IDX + ADC_SSFIFO),
        (UInt16 *)samples[adc][bufIdx],
        SAMPLE_BUFFER_SIZE * SAMPLE_SEQ_LEN);
}

static Void initADCDMA(int adc)
{
    UInt adcBase = adcBaseAddr(adc);
    UInt adcChan = adcChanAddr(adc);

    ADCSequenceDMAEnable(adcBase, ADC_SEQUENCER_IDX);
    ADCIntEnableEx(adcBase, CONCAT(ADC_INT_DMA_SS, ADC_SEQUENCER_IDX));
    //IntEnable(CONCAT(INT_ADC0SS, ADC_SEQUENCER_IDX)); // TODO: adc index

    uDMAChannelAttributeDisable(adcChan,
                                UDMA_ATTR_ALTSELECT | UDMA_ATTR_USEBURST |
                                UDMA_ATTR_HIGH_PRIORITY |
                                UDMA_ATTR_REQMASK);
    uDMAChannelAttributeDisable(adcChan, UDMA_ATTR_HIGH_PRIORITY);
    uDMAChannelControlSet(adcChan | UDMA_PRI_SELECT,
                          UDMA_SIZE_16 | UDMA_SRC_INC_NONE | UDMA_DST_INC_16 |
                          UDMA_ARB_8);
    uDMAChannelControlSet(adcChan | UDMA_ALT_SELECT,
                          UDMA_SIZE_16 | UDMA_SRC_INC_NONE | UDMA_DST_INC_16 |
                          UDMA_ARB_8);
    setupDMAADCTransfer(adc, UDMA_PRI_SELECT, 0);
    setupDMAADCTransfer(adc, UDMA_ALT_SELECT, 1);

    uDMAChannelEnable(adcChan);
}

static Void initADC(UInt32 samplesPerSec)
{
    //UInt32 divisor = 16; /* appropriate for 100 to 100k samples/sec */
    UInt32 divisor = 128; /* appropriate for 10 to 10k samples/sec */
    UInt32 prescaler = divisor - 1;
    UInt32 period = SysCtlClockGet() / divisor / samplesPerSec;

    /* These limits are imposed by the divisor chosen above:
     * 1 <= 80Mhz/divisor/minSamplesPerSec <= 2**16
     * but probably the lower limit counts should be a bunch more than just 1.
     * For example, for divisor 16:
     * 80MHz/16/100 = 50000 and 80MHz/16/100000 = 50 */
    /* TODO: calculate divisor automatically */
    //Assert_isTrue(samplesPerSec >= 100 && samplesPerSec <= 100000, NULL);
    Assert_isTrue(samplesPerSec >= 10 && samplesPerSec <= 10000, NULL);
    Assert_isTrue(SysCtlClockGet() % divisor == 0, NULL);

    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC1);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
    shortDelay();

    GPIOPinTypeADC(GPIO_PORTE_BASE,
                   GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_5);
    GPIOPinTypeADC(GPIO_PORTD_BASE,
                   GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);

    ADCSequenceDisable(ADC_BASE(0), ADC_SEQUENCER_IDX);
    ADCSequenceConfigure(ADC_BASE(0), ADC_SEQUENCER_IDX, ADC_TRIGGER_TIMER, 0);

    ADCSequenceStepConfigure(ADC_BASE(0), ADC_SEQUENCER_IDX, 0,
                             ADC_CTL_CH0);
    ADCSequenceStepConfigure(ADC_BASE(0), ADC_SEQUENCER_IDX, 1,
                             ADC_CTL_CH1);
    ADCSequenceStepConfigure(ADC_BASE(0), ADC_SEQUENCER_IDX, 2,
                             ADC_CTL_CH2);
    ADCSequenceStepConfigure(ADC_BASE(0), ADC_SEQUENCER_IDX, 3,
                             ADC_CTL_CH3);
    ADCSequenceStepConfigure(ADC_BASE(0), ADC_SEQUENCER_IDX, 4,
                             ADC_CTL_CH8);
    ADCSequenceStepConfigure(ADC_BASE(0), ADC_SEQUENCER_IDX, 5,
                             ADC_CTL_CH9);
    ADCSequenceStepConfigure(ADC_BASE(0), ADC_SEQUENCER_IDX, 6,
                             ADC_CTL_CH0);
    ADCSequenceStepConfigure(ADC_BASE(0), ADC_SEQUENCER_IDX, 7,
                             ADC_CTL_TS | ADC_CTL_IE | ADC_CTL_END);

    ADCSequenceDisable(ADC_BASE(1), ADC_SEQUENCER_IDX);
    ADCSequenceConfigure(ADC_BASE(1), ADC_SEQUENCER_IDX, ADC_TRIGGER_TIMER, 0);

    ADCSequenceStepConfigure(ADC_BASE(1), ADC_SEQUENCER_IDX, 0,
                             ADC_CTL_CH4);
    ADCSequenceStepConfigure(ADC_BASE(1), ADC_SEQUENCER_IDX, 1,
                             ADC_CTL_CH5);
    ADCSequenceStepConfigure(ADC_BASE(1), ADC_SEQUENCER_IDX, 2,
                             ADC_CTL_CH6);
    ADCSequenceStepConfigure(ADC_BASE(1), ADC_SEQUENCER_IDX, 3,
                             ADC_CTL_CH7);
    ADCSequenceStepConfigure(ADC_BASE(1), ADC_SEQUENCER_IDX, 4,
                             ADC_CTL_CH10);
    ADCSequenceStepConfigure(ADC_BASE(1), ADC_SEQUENCER_IDX, 5,
                             ADC_CTL_CH11);
    ADCSequenceStepConfigure(ADC_BASE(1), ADC_SEQUENCER_IDX, 6,
                             ADC_CTL_CH0);
    ADCSequenceStepConfigure(ADC_BASE(1), ADC_SEQUENCER_IDX, 7,
                             ADC_CTL_TS | ADC_CTL_IE | ADC_CTL_END);

    ADCSequenceEnable(ADC_BASE(0), ADC_SEQUENCER_IDX);
    ADCSequenceEnable(ADC_BASE(1), ADC_SEQUENCER_IDX);

    // DMA from ADC to memory buffer
    uDMAChannelAssign(UDMA_CH14_ADC0_0);
    uDMAChannelAssign(UDMA_CH24_ADC1_0);

    initADCDMA(0);
    initADCDMA(1);

    // Timer that triggers the sample
    TimerPrescaleSet(TIMER1_BASE, TIMER_B, prescaler);
    TimerLoadSet(TIMER1_BASE, TIMER_B, period);
    TimerControlTrigger(TIMER1_BASE, TIMER_B, TRUE);
}

static Void startADCandProfileGen()
{
    /* TODO: synchronize timers */
    //startPwm();
    TimerEnable(TIMER1_BASE, TIMER_BOTH);
}

static Void processBuffer(UInt32 adc, UInt32 controlSelect, Int idx)
{
    /* Setup the next transfer into this buffer that was just filled */
    setupDMAADCTransfer(adc, controlSelect, idx);

    exportBuffer(adc * NUM_SAMPLE_BUFFERS + idx); /* buffer index */
}

Void onSampleTransferComplete(UArg arg)
{
    UInt adc = (UInt)arg;
    UInt adcBase = adcBaseAddr(adc);
    UInt adcChan = adcChanAddr(adc);
    Bool primaryMode, altMode;

    UInt32 status = ADCIntStatusEx(adcBase, TRUE);
    ADCIntClearEx(adcBase, status);

    primaryMode = uDMAChannelModeGet(adcChan | UDMA_PRI_SELECT);
    altMode = uDMAChannelModeGet(adcChan | UDMA_ALT_SELECT);

    /* If both have stopped, then we didn't process them in time */
    Assert_isTrue(!(primaryMode == UDMA_MODE_STOP &&
                    altMode == UDMA_MODE_STOP), NULL);

    if (primaryMode == UDMA_MODE_STOP)
        processBuffer(adc, UDMA_PRI_SELECT, 0);
    if (altMode == UDMA_MODE_STOP)
        processBuffer(adc, UDMA_ALT_SELECT, 1);
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

    initExport(expBuffers);
    initADCandProfileGenTimers();
    initADC(SAMPLES_PER_SEC);
    initProfileGen(PROFILE_TICKS_PER_SEC);

    startADCandProfileGen();
    return 0;
}
