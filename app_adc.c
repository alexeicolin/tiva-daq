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

static uint32_t samples[NUM_SAMPLE_BUFFERS][SAMPLE_BUFFER_SIZE];
static Bool bufferFree[NUM_SAMPLE_BUFFERS];

static Int buf = 0; /* buffer index that is currently being filled */
static Int n = 0; /* index into current sample buffer (next sample goes here) */

#define ADC_SEQUENCER_IDX 3

static Void initADC()
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
    shortDelay();

    GPIOPinTypeADC(GPIO_PORTE_BASE, GPIO_PIN_3);

    ADCSequenceDisable(ADC0_BASE, ADC_SEQUENCER_IDX);

    // Enable sample sequence 3 with a processor signal trigger.  Sequence 3
    // will do a single sample when the processor sends a signal to start the
    // conversion.  Each ADC module has 4 programmable sequences, sequence 0
    // to sequence 3.  This example is arbitrarily using sequence 3.
    ADCSequenceConfigure(ADC0_BASE, ADC_SEQUENCER_IDX, ADC_TRIGGER_PROCESSOR, 0);

    // Configure step 0 on sequence 3.  Sample channel 0 (ADC_CTL_CH0) in
    // single-ended mode (default) and configure the interrupt flag
    // (ADC_CTL_IE) to be set when the sample is done.  Tell the ADC logic
    // that this is the last conversion on sequence 3 (ADC_CTL_END).  Sequence
    // 3 has only one programmable step.  Sequence 1 and 2 have 4 steps, and
    // sequence 0 has 8 programmable steps.  Since we are only doing a single
    // conversion using sequence 3 we will only configure step 0.  For more
    // information on the ADC sequences and steps, reference the datasheet.
    ADCSequenceStepConfigure(ADC0_BASE, ADC_SEQUENCER_IDX, 0, ADC_CTL_CH0 | ADC_CTL_IE |
                             ADC_CTL_END);

    ADCIntEnable(ADC0_BASE, ADC_SEQUENCER_IDX);
    //ADCIntClear(ADC0_BASE, ADC_SEQUENCER_IDX);

    ADCSequenceEnable(ADC0_BASE, ADC_SEQUENCER_IDX);

}

Void onSampleReady(UArg arg)
{
    if (bufferFree[buf]) {
        ADCSequenceDataGet(ADC0_BASE, ADC_SEQUENCER_IDX, &samples[buf][n++]);
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
                    System_printf("%u\n", samples[j][i]);
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
