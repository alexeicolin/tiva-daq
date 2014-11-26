#ifndef DAQ_H
#define DAQ_H

#include <xdc/std.h>

#define NUM_ADCS 2
#define NUM_SEQS 4
#define SAMPLE_SIZE 2    /* bytes */

#define MAX_SAMPLES_IN_SEQ 8
#define NUM_BUFS_PER_SEQ 2

/* Defining our own in order to have 0 as invalid value */
enum AdcSample {
    ADC_SAMPLE_NONE = 0,
    ADC_SAMPLE_CH0,
    ADC_SAMPLE_CH1,
    ADC_SAMPLE_CH2,
    ADC_SAMPLE_CH3,
    ADC_SAMPLE_CH4,
    ADC_SAMPLE_CH5,
    ADC_SAMPLE_CH6,
    ADC_SAMPLE_CH7,
    ADC_SAMPLE_CH8,
    ADC_SAMPLE_CH9,
    ADC_SAMPLE_CH10,
    ADC_SAMPLE_CH11,
    ADC_SAMPLE_CH12,
    ADC_SAMPLE_TEMP
};

struct SequenceBuffer {
    UInt8 *addr;
    UInt32 size; 
};

struct SequenceConfig {
    enum AdcSample samples[MAX_SAMPLES_IN_SEQ];
    struct SequenceBuffer buf;
};

struct AdcConfig {
    struct SequenceConfig seqs[NUM_ADCS][NUM_SEQS];
};

Void initDAQ(const struct AdcConfig *conf, UInt32 samplesPerSec);

#endif // DAQ_H
