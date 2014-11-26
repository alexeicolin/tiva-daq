#ifndef DAQ_H
#define DAQ_H

#include <xdc/std.h>

#define NUM_ADCS 2
#define NUM_SEQS 4
#define SAMPLE_SIZE 2    /* bytes */

#define MAX_SAMPLES_IN_SEQ 8
#define NUM_BUFS_PER_SEQ 2

#define ADC_SEQ_END ~0 /* an invalid ADC_CTL_ value (note 0 is taken) */

struct SequenceBuffer {
    UInt8 *addr;
    UInt32 size; 
};

struct SequenceConfig {
    UInt32 samples[MAX_SAMPLES_IN_SEQ];
    struct SequenceBuffer buf;
};

struct AdcConfig {
    struct SequenceConfig seqs[NUM_ADCS][NUM_SEQS];
};

Void initDAQ(const struct AdcConfig *conf, UInt32 samplesPerSec);

#endif // DAQ_H
