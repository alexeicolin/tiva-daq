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
#include "Board.h"

#define CONCAT_INNER(a, b) a ## b
#define CONCAT(a, b) CONCAT_INNER(a, b)
#define CONCAT3_INNER(a, b, c) a ## b ## c
#define CONCAT3(a, b, c) CONCAT3_INNER(a, b, c)
#define CONCATU3_INNER(a, b, c) a ## _ ## b ## _ ## c
#define CONCATU3(a, b, c) CONCATU3_INNER(a, b, c)

uint8_t dmaControlTable[1024] __attribute__ ((aligned(1024)));

#define NUM_ADCS 2
#define NUM_SAMPLE_BUFFERS 2
// NOTE: Max uDMA transfer is 1024 bytes => max sample sequences per buf is 32
#define SAMPLE_BUFFER_SIZE 32 // sample seqs (1 seq = SAMPLE_SEQ_LEN samples)
#define SAMPLE_HEADER_SIZE 1  // sample seqs (1 seq = SAMPLE_SEQ_LEN samples)
#define SAMPLE_SEQ_LEN 8
#define SAMPLE_SIZE 2 /* bytes */

enum BufferState {
    BUFFER_FREE,
    BUFFER_FULL,
};

static UInt16 samples[NUM_SAMPLE_BUFFERS]
                     [NUM_ADCS]
                     [SAMPLE_BUFFER_SIZE]
                     [SAMPLE_SEQ_LEN];

static enum BufferState bufferState[NUM_ADCS][NUM_SAMPLE_BUFFERS];

static const UInt32 marker = 0xf00dcafe;
static UInt32 bufferSeqNum = 0;

/* index of buffer currently being transferred to UART */
static Int readingBufIdx = -1; 

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

static inline UInt writeUInt32(UChar *buf, UInt32 n)
{
    buf[3] = n & 0xff;
    n >>= 8;
    buf[2] = n & 0xff;
    n >>= 8;
    buf[1] = n & 0xff;
    n >>= 8;
    buf[0] = n & 0xff;
    n >>= 8;
    return 4;
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
        (UInt16 *)samples[bufIdx][adc] + SAMPLE_HEADER_SIZE * SAMPLE_SEQ_LEN,
        (SAMPLE_BUFFER_SIZE - SAMPLE_HEADER_SIZE) * SAMPLE_SEQ_LEN);
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
    TimerEnable(TIMER1_BASE, TIMER_BOTH);
}

static Void processBuffer(UInt32 adc, UInt32 controlSelect, Int idx)
{
    Int adcIdx;
    Bool allADCsFull = TRUE;
    Assert_isTrue(bufferState[adc][idx] == BUFFER_FREE, NULL);
    bufferState[adc][idx] = BUFFER_FULL;

    // Setup the next transfer into this buffer that was just filled
    setupDMAADCTransfer(adc, controlSelect, idx);

    // Launch UART transfer if all ADCs filled their buffer ready
    for (adcIdx = 0; adcIdx < NUM_ADCS; ++adcIdx) {
        if (bufferState[adcIdx][idx] != BUFFER_FULL) {
            allADCsFull = FALSE;
            break;
        }
    }
    if (allADCsFull)
        startBufferTransfer(idx);
}

Void onSampleTransferComplete(UArg arg)
{
    UInt key;
    UInt adc = (UInt)arg;
    UInt adcBase = adcBaseAddr(adc);
    UInt adcChan = adcChanAddr(adc);

    UInt32 status = ADCIntStatusEx(adcBase, TRUE);
    ADCIntClearEx(adcBase, status);

    /* Protect the check of whether both ADCs are ready: prevent the other ADC
     * from interrupting while we do the check */
    key = Hwi_disable();

    if (uDMAChannelModeGet(adcChan | UDMA_PRI_SELECT) == UDMA_MODE_STOP)
        processBuffer(adc, UDMA_PRI_SELECT, 0);
    if (uDMAChannelModeGet(adcChan | UDMA_ALT_SELECT) == UDMA_MODE_STOP)
        processBuffer(adc, UDMA_ALT_SELECT, 1);

    Hwi_restore(key);
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

static Void initUART()
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
    GPIOPinConfigure(GPIO_PA0_U0RX);
    GPIOPinConfigure(GPIO_PA1_U0TX);
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    UARTConfigSetExpClk(UART0_BASE, SysCtlClockGet(), 115200,
                        UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE |
                        UART_CONFIG_PAR_NONE);
}

static Void initOutputDMA()
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UDMA);
    IntEnable(INT_UDMAERR);
    uDMAEnable();
    uDMAControlBaseSet(dmaControlTable);

    // Set both the TX and RX trigger thresholds to 4.  This will be used by
    // the uDMA controller to signal when more data should be transferred.  The
    // uDMA TX and RX channels will be configured so that it can transfer 4
    // bytes in a burst when the UART is ready to transfer more data.
    UARTFIFOLevelSet(UART0_BASE, UART_FIFO_TX4_8, UART_FIFO_RX4_8);
    UARTDMAEnable(UART0_BASE, UART_DMA_TX);
    IntEnable(INT_UART0);

    uDMAChannelAttributeDisable(UDMA_CHANNEL_UART0TX,
                                UDMA_ATTR_ALTSELECT |
                                UDMA_ATTR_HIGH_PRIORITY |
                                UDMA_ATTR_REQMASK);
    uDMAChannelAttributeEnable(UDMA_CHANNEL_UART0TX, UDMA_ATTR_USEBURST);

    uDMAChannelAttributeEnable(UDMA_CHANNEL_UART0TX, UDMA_ATTR_USEBURST);
    uDMAChannelControlSet(UDMA_CHANNEL_UART0TX | UDMA_PRI_SELECT,
                          UDMA_SIZE_8 | UDMA_SRC_INC_8 | UDMA_DST_INC_NONE |
                          UDMA_ARB_4);
}

Void onDMAError(UArg arg)
{
    UInt32 status = uDMAErrorStatusGet();
    Assert_isTrue(!status, NULL); /* we don't tolerate errors */
    uDMAErrorStatusClear();
}

static Void startBufferTransfer(Int idx)
{
    Int adc;
    UChar *adcBuf;

    GPIO_write(EK_TM4C123GXL_LED_GREEN, Board_LED_ON);

    readingBufIdx = idx;

    for (adc = 0; adc < NUM_ADCS; ++adc) {
        Assert_isTrue(bufferState[adc][idx] == BUFFER_FULL, NULL);

        /* Write header */
        adcBuf = (UChar *)&samples[idx][adc][0];
        adcBuf += writeUInt32(adcBuf, marker);
        adcBuf += writeUInt32(adcBuf, bufferSeqNum++);
    }

    uDMAChannelTransferSet(UDMA_CHANNEL_UART0TX | UDMA_PRI_SELECT,
       UDMA_MODE_BASIC,
       samples[idx],
       (void *)(UART0_BASE + UART_O_DR),
       NUM_ADCS * SAMPLE_BUFFER_SIZE * SAMPLE_SEQ_LEN * SAMPLE_SIZE);
    uDMAChannelEnable(UDMA_CHANNEL_UART0TX);
}

Void onBufferTransferComplete(UArg arg)
{
    Int adc;
    UInt32 status;
    status = UARTIntStatus(UART0_BASE, 1);
    UARTIntClear(UART0_BASE, status);

    Assert_isTrue(readingBufIdx >= 0, NULL);

    /* Disabled channel means transfer is done */
    Assert_isTrue(!uDMAChannelIsEnabled(UDMA_CHANNEL_UART0TX), NULL);

    for (adc = 0; adc < NUM_ADCS; ++adc) {
        Assert_isTrue(bufferState[adc][readingBufIdx] == BUFFER_FULL, NULL);
        bufferState[adc][readingBufIdx] = BUFFER_FREE;
    }
    readingBufIdx = -1;

    GPIO_write(EK_TM4C123GXL_LED_GREEN, Board_LED_OFF);
}

Int app(Int argc, Char* argv[])
{
    Int i, j;

    for (i = 0; i < NUM_ADCS; ++i)
        for (j = 0; j < NUM_SAMPLE_BUFFERS; ++j)
            bufferState[i][j] = BUFFER_FREE;

    Assert_isTrue(NUM_PROFILES == numProfiles, NULL);
    convertProfileToTicks(PROFILE_TICKS_PER_SEC);

    initADCandProfileGenTimers();
    initUART();
    initOutputDMA();
    initADC(SAMPLES_PER_SEC);
    initProfileGen(PROFILE_TICKS_PER_SEC);

    /* TODO: synchronize timers */
    startPwm();
    startADCandProfileGen();
    return 0;
}
