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
#include <driverlib/adc.h>
#include <driverlib/gpio.h>
#include <driverlib/pin_map.h>
#include <driverlib/sysctl.h>
#include "driverlib/timer.h"
#include <driverlib/udma.h>
#include <driverlib/uart.h>
#include <driverlib/interrupt.h>

#include "delay.h"
#include "Board.h"

uint8_t dmaControlTable[1024] __attribute__ ((aligned(1024)));

#define NUM_SAMPLE_BUFFERS 2
#define SAMPLE_BUFFER_SIZE 256
#define SAMPLE_SIZE 3

enum BufferState {
    BUFFER_FREE,
    BUFFER_FULL,
    BUFFER_DMA,
};

static UInt16 samples[NUM_SAMPLE_BUFFERS][SAMPLE_BUFFER_SIZE][SAMPLE_SIZE];
static enum BufferState bufferState[NUM_SAMPLE_BUFFERS];

static Int buf = 0; /* buffer index that is currently being filled */
static Int n = 0; /* index into current sample buffer (next sample goes here) */

#define ADC_SEQUENCER_IDX 2

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

static Void initADC(UInt32 samplesPerSec)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
    shortDelay();

    GPIOPinTypeADC(GPIO_PORTE_BASE,
                   GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);

    ADCSequenceDisable(ADC0_BASE, ADC_SEQUENCER_IDX);
    ADCSequenceConfigure(ADC0_BASE, ADC_SEQUENCER_IDX, ADC_TRIGGER_TIMER, 0);

    ADCSequenceStepConfigure(ADC0_BASE, ADC_SEQUENCER_IDX, 0,
                             ADC_CTL_CH0 | ADC_CTL_D);
    ADCSequenceStepConfigure(ADC0_BASE, ADC_SEQUENCER_IDX, 1,
                             ADC_CTL_CH2);
    ADCSequenceStepConfigure(ADC0_BASE, ADC_SEQUENCER_IDX, 2,
                             ADC_CTL_CH3 | ADC_CTL_IE | ADC_CTL_END);

    ADCIntEnable(ADC0_BASE, ADC_SEQUENCER_IDX);
    //ADCIntClear(ADC0_BASE, ADC_SEQUENCER_IDX);

    ADCSequenceEnable(ADC0_BASE, ADC_SEQUENCER_IDX);

    // Timer that triggers the sample
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
    TimerConfigure(TIMER0_BASE, TIMER_CFG_SPLIT_PAIR | TIMER_CFG_B_PERIODIC);
    TimerLoadSet(TIMER0_BASE, TIMER_B, SysCtlClockGet() / samplesPerSec);
    TimerControlTrigger(TIMER0_BASE, TIMER_B, TRUE);
}

Void startADC()
{
    TimerEnable(TIMER0_BASE, TIMER_B);
}

Void initUART()
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

Void initDMA()
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

Void startBufferTransfer(Int idx)
{
#ifdef BLINK_LED
    GPIO_write(Board_LED, Board_LED_ON);
    shortDelay();
    GPIO_write(Board_LED, Board_LED_OFF);
#endif

    uDMAChannelTransferSet(UDMA_CHANNEL_UART0TX | UDMA_PRI_SELECT,
                           UDMA_MODE_BASIC, samples[idx],
                           (void *)(UART0_BASE + UART_O_DR),
                           sizeof(samples[buf]));
    uDMAChannelEnable(UDMA_CHANNEL_UART0TX);
}

Void onBufferTransferComplete(UArg arg)
{
#ifdef BLINK_LED
    GPIO_write(EK_TM4C123GXL_LED_GREEN, Board_LED_ON);
    shortDelay();
    GPIO_write(EK_TM4C123GXL_LED_GREEN, Board_LED_OFF);
#endif

    Int bufIdx = buf ? (buf - 1) % NUM_SAMPLE_BUFFERS : NUM_SAMPLE_BUFFERS - 1;
    UInt32 status;
    status = UARTIntStatus(UART0_BASE, 1);
    UARTIntClear(UART0_BASE, status);

    /* Disabled channel means transfer is done */
    Assert_isTrue(!uDMAChannelIsEnabled(UDMA_CHANNEL_UART0TX), NULL);

    Assert_isTrue(bufferState[bufIdx] == BUFFER_DMA, NULL);
    bufferState[bufIdx] = BUFFER_FREE;
}

Void onSampleReady(UArg arg)
{
    Int i;
    uint32_t sample[SAMPLE_SIZE];

    if (bufferState[buf] == BUFFER_FREE) {
        ADCSequenceDataGet(ADC0_BASE, ADC_SEQUENCER_IDX, sample);

        /* Samples are 16-bit (from 12-bit ADC), so cast UInt32 to UInt16 */
        for (i = 0; i < SAMPLE_SIZE; ++i)
            samples[buf][n][i] = sample[i];
        ++n;

        if (n == SAMPLE_BUFFER_SIZE) {
            bufferState[buf] = BUFFER_DMA; /* signal to consumer task */
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

Int app(Int argc, Char* argv[])
{
    Int j;
    for (j = 0; j < NUM_SAMPLE_BUFFERS; ++j)
        bufferState[j] = BUFFER_FREE;

    initUART();
    initDMA();
    initADC(2000);

    /* Marker for the parser to lock in on the binary data stream */
    UARTCharPut(UART0_BASE, 0xf0);
    UARTCharPut(UART0_BASE, 0x0d);
    UARTCharPut(UART0_BASE, 0xca);
    UARTCharPut(UART0_BASE, 0xfe);

    startADC();
    return 0;
}