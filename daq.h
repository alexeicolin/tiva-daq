#ifndef DAQ_H
#define DAQ_H

#include <xdc/std.h>

#define CONCAT_INNER(a, b) a ## b
#define CONCAT(a, b) CONCAT_INNER(a, b)
#define CONCAT3_INNER(a, b, c) a ## b ## c
#define CONCAT3(a, b, c) CONCAT3_INNER(a, b, c)
#define CONCAT4_INNER(a, b, c, d) a ## b ## c ## d
#define CONCAT4(a, b, c, d) CONCAT4_INNER(a, b, c, d)
#define CONCAT6_INNER(a, b, c, d, e, f) a ## b ## c ## d ## e ## f
#define CONCAT6(a, b, c, d, e, f) CONCAT6_INNER(a, b, c, d, e, f)
#define CONCATU3_INNER(a, b, c) a ## _ ## b ## _ ## c
#define CONCATU3(a, b, c) CONCATU3_INNER(a, b, c)

#define NUM_ADCS 2
#define NUM_SEQS 4
#define SAMPLE_SIZE 2    /* bytes */

#define MAX_SAMPLES_IN_SEQ 8
#define NUM_BUFS_PER_SEQ 2

#define ADC_SEQ_END ~0 /* an invalid ADC_CTL_ value (note 0 is taken) */

#define ADC_BASE(adc) CONCAT3(ADC, adc, _BASE)
#define ADC_PERIPH(adc) CONCAT(SYSCTL_PERIPH_ADC, adc)
#define ADC_SEQ_INT(adc, seq) CONCAT4(INT_ADC, adc, SS, seq)

struct SequenceBuffer {
    UInt8 *addr;
    UInt32 size; 
};

struct SequenceConfig {
    Bool enabled;
    UInt8 priority;
    UInt32 trigger;
    UInt32 samples[MAX_SAMPLES_IN_SEQ];
    struct SequenceBuffer buf;
};

struct AdcConfig {
    struct SequenceConfig seqs[NUM_ADCS][NUM_SEQS];

    /* TODO: the timer should be per ADC,
     * so replace seqs 2D array with ADC config obj */
    UInt32 triggerTimerBase;
    UInt32 triggerTimerHalf;
};

Void initDAQ(const struct AdcConfig *conf, UInt32 samplesPerSec);

#endif // DAQ_H
