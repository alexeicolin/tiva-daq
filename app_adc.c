#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <xdc/runtime/Assert.h>
#include <ti/sysbios/knl/Task.h>

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
#include "Board.h"

#define CONCAT_INNER(prefix, idx) prefix ## idx
#define CONCAT(prefix, idx) CONCAT_INNER(prefix, idx)

uint8_t dmaControlTable[1024] __attribute__ ((aligned(1024)));

#define NUM_SAMPLE_BUFFERS 2
#define SAMPLE_BUFFER_SIZE 256
#define SAMPLE_SEQ_LEN 4
#define SAMPLE_SIZE 2 /* bytes */

enum BufferState {
    BUFFER_FREE,
    BUFFER_FULL,
};

static UInt16 samples[NUM_SAMPLE_BUFFERS][SAMPLE_BUFFER_SIZE][SAMPLE_SEQ_LEN];
static enum BufferState bufferState[NUM_SAMPLE_BUFFERS];

static Int buf = 0; /* buffer index that is currently being filled */
static Int n = 0; /* index into current sample buffer (next sample goes here) */
static Int readingBufIdx = -1;

#define ADC_SEQUENCER_IDX 2
#define ADC_CHANNEL CONCAT(UDMA_CHANNEL_ADC, ADC_SEQUENCER_IDX)

// From driverlib/adc.c
#define ADC_SEQ                 (ADC_O_SSMUX0)
#define ADC_SEQ_STEP            (ADC_O_SSMUX1 - ADC_O_SSMUX0)
#define ADC_SSFIFO              (ADC_O_SSFIFO0 - ADC_O_SSMUX0)

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

static Void setupDMAADCTransfer(UInt32 controlSelect, Int bufIdx)
{
    uDMAChannelTransferSet(ADC_CHANNEL | controlSelect,
                           UDMA_MODE_PINGPONG,
                           (void *)(ADC0_BASE + ADC_SEQ +
                               ADC_SEQ_STEP * ADC_SEQUENCER_IDX + ADC_SSFIFO),
                           samples[bufIdx], SAMPLE_BUFFER_SIZE * SAMPLE_SEQ_LEN);
}

static Void initADC(UInt32 samplesPerSec)
{
    UInt32 divisor = 16; /* appropriate for 100 to 100k samples/sec */
    UInt32 prescaler = divisor - 1;
    UInt32 period = SysCtlClockGet() / divisor / samplesPerSec;

    /* These limits are imposed by the divisor chosen above:
     * 1 <= 80Mhz/divisor/minSamplesPerSec <= 2**16
     * but probably the lower limit counts should be a bunch more than just 1.
     * For example, for divisor 16:
     * 80MHz/16/100 = 50000 and 80MHz/16/100000 = 50 */
    /* TODO: calculate divisor automatically */
    Assert_isTrue(samplesPerSec >= 100 && samplesPerSec <= 100000, NULL);
    Assert_isTrue(SysCtlClockGet() % divisor == 0, NULL);

    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
    shortDelay();

    GPIOPinTypeADC(GPIO_PORTE_BASE,
                   GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_5);

    ADCSequenceDisable(ADC0_BASE, ADC_SEQUENCER_IDX);
    ADCSequenceConfigure(ADC0_BASE, ADC_SEQUENCER_IDX, ADC_TRIGGER_TIMER, 0);

    ADCSequenceStepConfigure(ADC0_BASE, ADC_SEQUENCER_IDX, 0,
                             ADC_CTL_CH0 | ADC_CTL_D);
    ADCSequenceStepConfigure(ADC0_BASE, ADC_SEQUENCER_IDX, 1,
                             ADC_CTL_CH2);
    ADCSequenceStepConfigure(ADC0_BASE, ADC_SEQUENCER_IDX, 2,
                             ADC_CTL_CH3);
    ADCSequenceStepConfigure(ADC0_BASE, ADC_SEQUENCER_IDX, 3,
                             ADC_CTL_CH8 | ADC_CTL_IE | ADC_CTL_END);

    //ADCIntEnable(ADC0_BASE, ADC_SEQUENCER_IDX);
    //ADCIntClear(ADC0_BASE, ADC_SEQUENCER_IDX);
    //

    ADCSequenceEnable(ADC0_BASE, ADC_SEQUENCER_IDX);

    ADCSequenceDMAEnable(ADC0_BASE, ADC_SEQUENCER_IDX);
    ADCIntEnableEx(ADC0_BASE, CONCAT(ADC_INT_DMA_SS, ADC_SEQUENCER_IDX));
    IntEnable(CONCAT(INT_ADC0SS, ADC_SEQUENCER_IDX));

    uDMAChannelAttributeDisable(ADC_CHANNEL,
                                UDMA_ATTR_ALTSELECT | UDMA_ATTR_USEBURST |
                                UDMA_ATTR_HIGH_PRIORITY |
                                UDMA_ATTR_REQMASK);
    uDMAChannelControlSet(ADC_CHANNEL | UDMA_PRI_SELECT,
                          UDMA_SIZE_16 | UDMA_SRC_INC_NONE | UDMA_DST_INC_16 |
                          UDMA_ARB_4);
    uDMAChannelControlSet(ADC_CHANNEL | UDMA_ALT_SELECT,
                          UDMA_SIZE_16 | UDMA_SRC_INC_NONE | UDMA_DST_INC_16 |
                          UDMA_ARB_4);
    setupDMAADCTransfer(UDMA_PRI_SELECT, 0);
    setupDMAADCTransfer(UDMA_ALT_SELECT, 1);

    uDMAChannelEnable(ADC_CHANNEL);

    // Timer that triggers the sample
    TimerPrescaleSet(TIMER1_BASE, TIMER_B, prescaler);
    TimerLoadSet(TIMER1_BASE, TIMER_B, period);
    TimerControlTrigger(TIMER1_BASE, TIMER_B, TRUE);
}

static Void startADCandProfileGen()
{
    TimerEnable(TIMER1_BASE, TIMER_BOTH);
}

static Void processBuffer(UInt32 controlSelect, Int idx)
{
    Assert_isTrue(bufferState[idx] == BUFFER_FREE, NULL);
    bufferState[idx] = BUFFER_FULL;
    readingBufIdx = idx;
    startBufferTransfer(idx);
    setupDMAADCTransfer(controlSelect, idx);
}

Void onSampleTransferComplete(UArg arg)
{
    UInt32 mode;
    UInt32 status = ADCIntStatusEx(ADC0_BASE, TRUE);

    ADCIntClearEx(ADC0_BASE, status);

    if (uDMAChannelModeGet(ADC_CHANNEL | UDMA_PRI_SELECT) == UDMA_MODE_STOP)
        processBuffer(UDMA_PRI_SELECT, 0);
    if (uDMAChannelModeGet(ADC_CHANNEL | UDMA_ALT_SELECT) == UDMA_MODE_STOP)
        processBuffer(UDMA_ALT_SELECT, 1);
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
}

Void onProfileTick(UArg arg)
{
    GPIO_write(EK_TM4C123GXL_LED_BLUE, Board_LED_ON);
    shortDelay();
    GPIO_write(EK_TM4C123GXL_LED_BLUE, Board_LED_OFF);

    TimerIntClear(TIMER1_BASE, TIMER_TIMA_TIMEOUT);
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
    GPIO_write(EK_TM4C123GXL_LED_GREEN, Board_LED_ON);

    uDMAChannelTransferSet(UDMA_CHANNEL_UART0TX | UDMA_PRI_SELECT,
                           UDMA_MODE_BASIC, samples[idx],
                           (void *)(UART0_BASE + UART_O_DR),
                           SAMPLE_BUFFER_SIZE * SAMPLE_SEQ_LEN * SAMPLE_SIZE);
    uDMAChannelEnable(UDMA_CHANNEL_UART0TX);
}

Void onBufferTransferComplete(UArg arg)
{
    UInt32 status;
    status = UARTIntStatus(UART0_BASE, 1);
    UARTIntClear(UART0_BASE, status);

    /* Disabled channel means transfer is done */
    Assert_isTrue(!uDMAChannelIsEnabled(UDMA_CHANNEL_UART0TX), NULL);

    bufferState[readingBufIdx] = BUFFER_FREE;
    readingBufIdx = -1;

    GPIO_write(EK_TM4C123GXL_LED_GREEN, Board_LED_OFF);
}

Void onSampleReady(UArg arg)
{
    Int i;
    uint32_t sample[SAMPLE_SEQ_LEN];

    if (bufferState[buf] == BUFFER_FREE) {
        ADCSequenceDataGet(ADC0_BASE, ADC_SEQUENCER_IDX, sample);

        /* Samples are 16-bit (from 12-bit ADC), so cast UInt32 to UInt16 */
        for (i = 0; i < SAMPLE_SEQ_LEN; ++i)
            samples[buf][n][i] = sample[i];
        ++n;

        if (n == SAMPLE_BUFFER_SIZE) {
            bufferState[buf] = BUFFER_FULL; /* signal to consumer task */
            startBufferTransfer(buf);
            buf = (buf + 1) % NUM_SAMPLE_BUFFERS;
            n = 0;
        }
        ADCIntClear(ADC0_BASE, ADC_SEQUENCER_IDX);
    } else {
        /* then buffer overflow */
        Assert_isTrue(0, NULL);
    }
}

Void triggerSample(UArg arg)
{
    while (1) {
        ADCProcessorTrigger(ADC0_BASE, ADC_SEQUENCER_IDX);
        Task_sleep(50); /* ticks */
    }
}

Void printData(UArg arg)
{
    Int i, j;
    while (1) {

#if 0 /* ADC polling (no interrupt) */
        while(!ADCIntStatus(ADC0_BASE, ADC_SEQUENCER_IDX, FALSE));
        ADCIntClear(ADC0_BASE, ADC_SEQUENCER_IDX);
        ADCSequenceDataGet(ADC0_BASE, ADC_SEQUENCER_IDX, sample);
#else
        for (j = 0; j < NUM_SAMPLE_BUFFERS; ++j) {
            if (bufferState[j] != BUFFER_FREE) {
                for (i = 0; i < SAMPLE_BUFFER_SIZE; ++i)
                    System_printf("V_E2-V_E3=%u V_E1=%u V_E0=%u\n",
                                  samples[j][i][0],
                                  samples[j][i][1], samples[j][i][2]);
                System_flush();

                bufferState[j] = BUFFER_FREE;
            }
        }
#endif
        Task_yield();
    }

}

#define PWM_DUTY_CYCLE_DELTA_PRC 10

/* decrement PWM duty cycle */
Void gpioButton1Fxn()
{
    if (!debounce(0)) return;
    changePwmDutyCycle(-PWM_DUTY_CYCLE_DELTA_PRC);
    GPIO_clearInt(EK_TM4C123GXL_GPIO_SW1);
}

/* increment PWM duty cycle */
Void gpioButton2Fxn()
{
    if (!debounce(1)) return;
    changePwmDutyCycle(PWM_DUTY_CYCLE_DELTA_PRC);
    GPIO_clearInt(EK_TM4C123GXL_GPIO_SW2);
}

Int app(Int argc, Char* argv[])
{
    Int j;
    for (j = 0; j < NUM_SAMPLE_BUFFERS; ++j)
        bufferState[j] = BUFFER_FREE;

    /* Shared by ADC trigger and profile generator */
    initADCandProfileGenTimers();

    initUART();
    initOutputDMA();
    initADC(100 /* samples/sec */);
    initProfileGen(10 /* ticks/sec */);

    /* Marker for the parser to lock in on the binary data stream */
    UARTCharPut(UART0_BASE, 0xf0);
    UARTCharPut(UART0_BASE, 0x0d);
    UARTCharPut(UART0_BASE, 0xca);
    UARTCharPut(UART0_BASE, 0xfe);

    enablePwm(100000 /* Hz */, 1 /* % */);

    startADCandProfileGen();
    return 0;
}
