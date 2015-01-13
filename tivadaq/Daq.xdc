package tivadaq;

import xdc.runtime.Log;
import xdc.runtime.Diags;

import platforms.tiva.GpioPort;
import platforms.tiva.Adc;
import platforms.tiva.AdcSeq;
import platforms.tiva.AdcChan;
import platforms.tiva.GpTimer;

@ModuleStartup
module Daq {

    // Hardware characteristics
    readonly config UInt NUM_ADCS = 2;
    readonly config UInt NUM_SEQS = 4;
    readonly config UInt NUM_BUFS_PER_SEQ = 2; // double-buffering

    metaonly readonly config UInt MAX_SAMPLES_IN_SEQ = 8;
    metaonly readonly config UInt SAMPLE_SIZE = 2; // bytes

    // Not metaonly since used on target at runtime
    enum AdcInChanName {
        AdcInChan_A_FIRST = 0,
        AdcInChan_A0 = AdcInChan_A_FIRST,
        AdcInChan_A1,
        AdcInChan_A2,
        AdcInChan_A3,
        AdcInChan_A4,
        AdcInChan_A5,
        AdcInChan_A6,
        AdcInChan_A7,
        AdcInChan_A8,
        AdcInChan_A9,
        AdcInChan_A10,
        AdcInChan_A11,
        AdcInChan_A_LAST = AdcInChan_A11, // last analog channel
        AdcInChan_TS // temperature sensor
    };

    metaonly enum AdcTrigger {
        AdcTrigger_PROCESSOR,
        AdcTrigger_TIMER
        // TODO: investigate the others
    };

    metaonly struct SeqConfig {
        Bool enabled;
        UInt8 priority;
        AdcTrigger trigger;
        AdcInChanName samples[length];
        UInt32 arbSize; // UDMA_ARB_*, auto-selected based on length if null
        UInt32 bufSize; 
    };

    metaonly enum TimerHalf {
        TimerHalf_A,
        TimerHalf_B
    };

    metaonly struct Timer {
        UInt8 idx;
        TimerHalf half;
    };

    metaonly struct AdcConfig {
        Timer triggerTimer;
        UInt32 samplesPerSec;
        UInt8 hwAvgFactor;

        SeqConfig seqs[NUM_SEQS];
    };

    metaonly struct DaqConfig {
        AdcConfig adcs[NUM_ADCS];
    };

    metaonly config DaqConfig daqConfig;

    Void start();
    Void stop();
    Void trigger(Int adc, Int seq);

    config Log.Event LM_startup = {
        mask: Diags.INFO,
        msg: "LM_startup: initializing Daq"
    };
    config Log.Event LM_initBuffer = {
        mask: Diags.INFO,
        msg: "LM_initBuffer: adc.seq/buf %d.%d/%d, addr %p, payload %p, exp idx %u"
    };
    config Log.Event LM_acquisitionStarted = {
        mask: Diags.USER1,
        msg: "LM_acquisitionStarted: adc.seq/buf %d.%d/%d, src %p -> dest %p, len %u"
    };
    config Log.Event LM_acquisitionCompleted = {
        mask: Diags.USER1,
        msg: "LM_acquisitionCompleted: adc.seq/buf %d.%d/%d"
    };
    config Log.Event LM_bufferContent = {
        mask: Diags.USER2,
        msg: "LM_adcBufferContent: addr %p: %08x %08x %08x %08x %08x %08x %08x ..."
    };

  internal:

    /* These state structures mirror the config, but dropping user-friendliness,
     * and include relevant hardware parameters for the devices.
     *
     * All fields, incl. hardware params such as base address, for *all*
     * ADCs and sequencers are needed at runtime (we are *not* choosing one and
     * using only it, but using potentially all). Hence, they are not metaonly.
     *
     * This could be optimized to include into the target binary only the ones
     * actually configured to be used, but this would complicate things greatly
     * since the lookups are currently all by ADC/sequencer index.
     *
     * Hardware parameters, like base addresses could be declared here, but
     * we populated the values in code leverages regularity in macro names from
     * PlatformInfo. This helps reduce repetative boiler-plate.
     */

    // Can't use '[length]' due to bug in XDC (see below)
    struct SampleList {
        UInt count;
        AdcChan.Handle samples[MAX_SAMPLES_IN_SEQ];
    };

    struct SeqState {
        AdcSeq.Handle seqDev;

        Bool enabled;
        UInt8 priority;
        UInt32 trigger; // ADC_TRIGGER_*

        // Generated code does not compile with array fields with length
        // variable at meta time ('[]' or '[length]').
        // AdcInChanName samples[length];
        SampleList samples;

        UInt32 arbSize; // UDMA_ARB_*, auto-selected based on length if null

        // Workaround for the same issue as above. Can't have '[length]',
        // here because it would be nested, but we can have then directly
        // in Module_State struct.
        // UInt8 bufs[NUM_BUFS_PER_SEQ][length];

        UInt8 *payloadAddr[NUM_BUFS_PER_SEQ]; // optional, could recompute this

        UInt8 numSamples;
        UInt8 exportBufIdx[NUM_BUFS_PER_SEQ]; // buf id's assigned by Export
    };

    struct TimerState {
        GpTimer.Handle timerDev;
        UInt32 prescaler;
        UInt32 period;
    };

    struct AdcState {
        Adc.Handle adcDev;
        TimerState triggerTimer;
        UInt8 hwAvgFactor;
        SeqState seqs[NUM_SEQS];
    };

    struct DaqState {
        AdcState adcs[NUM_ADCS];
    };

    // Trigger timer settings
    // TODO: calculate divisor automatically from sample rate

    /* The timer clock divisor imposes limit on sample rate:
     * 1 <= 80Mhz/divisor/minSamplesPerSec <= 2**16
     * but probably the lower limit counts should be a bunch more than just 1.
     * For example, for divisor 16:
     * 80MHz/16/100 = 50000 and 80MHz/16/100000 = 50
     */

    // metaonly readonly config TRIGGER_TIMER_DIVISOR = 16;
    // metaonly readonly config MIN_SAMPLES_PER_SEC = 100;
    // metaonly readonly config MAX_SAMPLES_PER_SEC = 100000;

    metaonly readonly config UInt32 TRIGGER_TIMER_DIVISOR = 128;
    metaonly readonly config UInt32 MIN_SAMPLES_PER_SEC = 10;
    metaonly readonly config UInt32 MAX_SAMPLES_PER_SEC = 10000;

    // Internal map (adc, seq, bufIdx) -> bufId that temporarily holds the
    // buffer ids assigned by the Export module, util they are transfered
    // to the the adcState->seqState->exportBufIdx upon static$init.
    metaonly config UInt32 exportBufIdxes[NUM_ADCS][NUM_SEQS][NUM_BUFS_PER_SEQ];

    struct Module_State {
        DaqState daqState;

        // Workaround: can't have them in nested structs, since
        // generated code does not compile with variable length
        // vector types ('[]', '[length]') in nested structs.
        UInt8 bufs[NUM_ADCS][NUM_SEQS][NUM_BUFS_PER_SEQ][length];
    };
}
