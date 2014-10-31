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

#include "delay.h"

#define NUM_SAMPLE_BUFFERS 2
#define SAMPLE_BUFFER_SIZE 32
#define SAMPLE_SIZE 2

static uint32_t samples[NUM_SAMPLE_BUFFERS][SAMPLE_BUFFER_SIZE][SAMPLE_SIZE];
static Bool bufferFree[NUM_SAMPLE_BUFFERS];

static Int buf = 0; /* buffer index that is currently being filled */
static Int n = 0; /* index into current sample buffer (next sample goes here) */

#define ADC_SEQUENCER_IDX 2

static Void initADC()
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
    shortDelay();

    GPIOPinTypeADC(GPIO_PORTE_BASE, GPIO_PIN_3);
    GPIOPinTypeADC(GPIO_PORTE_BASE, GPIO_PIN_2);

    ADCSequenceDisable(ADC0_BASE, ADC_SEQUENCER_IDX);
    ADCSequenceConfigure(ADC0_BASE, ADC_SEQUENCER_IDX, ADC_TRIGGER_PROCESSOR, 0);

    ADCSequenceStepConfigure(ADC0_BASE, ADC_SEQUENCER_IDX, 0,
                             ADC_CTL_CH0);
    ADCSequenceStepConfigure(ADC0_BASE, ADC_SEQUENCER_IDX, 1,
                             ADC_CTL_CH1 | ADC_CTL_IE | ADC_CTL_END);

    ADCIntEnable(ADC0_BASE, ADC_SEQUENCER_IDX);
    //ADCIntClear(ADC0_BASE, ADC_SEQUENCER_IDX);

    ADCSequenceEnable(ADC0_BASE, ADC_SEQUENCER_IDX);

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
                    System_printf("%u %u\n",
                                  samples[j][i][0], samples[j][i][1]);
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

    initADC();
    return 0;
}
