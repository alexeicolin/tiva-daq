#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <xdc/runtime/Assert.h>
#include <ti/sysbios/knl/Task.h>

#include <xdc/cfg/global.h>

#include <stdbool.h>
#include <stdint.h>

#include <inc/hw_memmap.h>
#include <driverlib/adc.h>
#include <driverlib/gpio.h>
#include <driverlib/pin_map.h>
#include <driverlib/sysctl.h>
#include "driverlib/timer.h"

#include "delay.h"

#define NUM_SAMPLE_BUFFERS 2
#define SAMPLE_BUFFER_SIZE 32
#define SAMPLE_SIZE 3

static uint32_t samples[NUM_SAMPLE_BUFFERS][SAMPLE_BUFFER_SIZE][SAMPLE_SIZE];
static Bool bufferFree[NUM_SAMPLE_BUFFERS];

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
    TimerEnable(TIMER0_BASE, TIMER_B);
}

Void onSampleReady(UArg arg)
{
    if (bufferFree[buf]) {
        ADCSequenceDataGet(ADC0_BASE, ADC_SEQUENCER_IDX, samples[buf][n++]);
        if (n == SAMPLE_BUFFER_SIZE) {
            bufferFree[buf] = FALSE; /* signal to consumer task */
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
            if (!bufferFree[j]) {
                for (i = 0; i < SAMPLE_BUFFER_SIZE; ++i)
                    System_printf("V_E2-V_E3=%u V_E1=%u V_E0=%u\n",
                                  samples[j][i][0],
                                  samples[j][i][1], samples[j][i][2]);
                System_flush();

                bufferFree[j] = TRUE;
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
        bufferFree[j] = TRUE;

    System_printf("Hello world!\n");

    initADC(2000);
    return 0;
}
