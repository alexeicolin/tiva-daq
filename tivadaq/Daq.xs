var Log;
var Diags;
var Export;
var Hwi;
var BIOS;
var PlatformInfo;
var GpioPort;
var Adc;
var AdcSeq;
var AdcChan;
var GpTimer;

function module$meta$init()
{
    Log = xdc.useModule('xdc.runtime.Log');
    Diags = xdc.useModule('xdc.runtime.Diags');
    BIOS = xdc.useModule('ti.sysbios.BIOS');
    Hwi = xdc.useModule('ti.sysbios.hal.Hwi');
    PlatformInfo = xdc.useModule('platforms.tiva.hw.PlatformInfo');
    GpioPort = xdc.useModule('platforms.tiva.hw.GpioPort');
    Adc = xdc.useModule('platforms.tiva.hw.Adc');
    AdcSeq = xdc.useModule('platforms.tiva.hw.AdcSeq');
    AdcChan = xdc.useModule('platforms.tiva.hw.AdcChan');
    GpTimer = xdc.useModule('platforms.tiva.hw.GpTimer');
    Export = xdc.useModule('tivadaq.Export');
}

function module$use()
{
    // Add buffers to Export module and save the assigned IDs
    for (var adc = 0; adc < this.NUM_ADCS; ++adc) {
        var adcConfig = this.daqConfig.adcs[adc];
        for (var seq = 0; seq < this.NUM_SEQS; ++seq) {
            var seqConf = adcConfig.seqs[seq];
            for (var bufIdx = 0; bufIdx < this.NUM_BUFS_PER_SEQ; ++bufIdx) {
                // The meta-domain part of adding an export buffer
                var bufId = 0; // ideally, this should be an invalid index
                if (seqConf.enabled)
                    bufId = Export.addBuffer(seqConf.bufSize);
                this.exportBufIdxes[adc][seq][bufIdx] = bufId;
            }
        }
    }
}

function module$validate()
{
    // This is marked readonly, but check it anyway
    if (this.NUM_BUFS_PER_SEQ != 2)
        this.$logError("Only double-buffering is supported",
                       this, "NUM_BUFS_PER_SEQ");

    for (var adc = 0; adc < this.NUM_ADCS; ++adc) {
        var adcConfig = this.daqConfig.adcs[adc];
        var triggerTimerNeeded = false;

        for (var seq = 0; seq < this.NUM_SEQS; ++seq) {
            var seqConf = adcConfig.seqs[seq];

            if (!seqConf.enabled)
                continue;

            if (seqConf.enabled && !isPowerOf2(seqConf.samples.length))
                this.$logError("Number of samples in a sequence must " +
                               "be a power of 2: adc " + adc + " seq " + seq + 
                               " length " + seqConf.samples.length,
                               this, "daqConfig");


            if (seqConf.trigger == this.AdcTrigger_TIMER)
                triggerTimerNeeded = true;
        }

        if (triggerTimerNeeded) {
            var samplesPerSec = adcConfig.samplesPerSec;
            
            var freqObj = BIOS.getCpuFreqMeta();
            if (freqObj.hi)
                this.$logError("Frequencies above 2^32-1 are not supported",
                               this, "TRIGGER_TIMER_DIVISOR");
            var freqHz = freqObj.lo;
            if (freqHz % this.TRIGGER_TIMER_DIVISOR != 0)
                this.$logError("Trigger timer divisor must be a multiple of " +
                               "timer clock frequency (system clock)", this,
                               "TRIGGER_TIMER_DIVISOR");

            if (!(samplesPerSec >= this.MIN_SAMPLES_PER_SEC &&
                  samplesPerSec <= this.MAX_SAMPLES_PER_SEC)) {
                this.$logError("Sample rate (" + samplesPerSec + ") outside " +
                               "the supported range for the current divisor " + 
                               "(" + this.TRIGGER_TIMER_DIVISOR + "): " +
                               "supported range " +
                               "[" + this.MIN_SAMPLES_PER_SEC + ", " +
                                     this.MAX_SAMPLES_PER_SEC + "] " +
                               "samples per sec", this, "daqConfig");
            }
        }
    }

    // TODO: validate per-sequencer maximum num samples

}

function module$static$init(state, mod)
{
    for (var adc = 0; adc < this.NUM_ADCS; ++adc) {

        var adcConfig = mod.daqConfig.adcs[adc];
        var adcState = state.daqState.adcs[adc];

        adcState.adcDev = Adc.create(adc);
        adcState.hwAvgFactor = adcConfig.hwAvgFactor;

        var triggerTimerNeeded = false;

        for (var seq = 0; seq < this.NUM_SEQS; ++seq) {

            var seqConf = adcConfig.seqs[seq];
            var seqState = adcState.seqs[seq];

            // Module state needs to be initialized to defined values
            if (!seqConf.enabled)
                seqConf.bufSize = 0;
            if (seqConf.priority == undefined)
                seqConf.priority = 0;

            seqState.enabled = seqConf.enabled;
            seqState.priority = seqConf.priority;

            seqState.seqDev = seqState.enabled ? AdcSeq.create(adc, seq) : null;

            // Copy and initialize all unused elements of samples list.
            // This is a workaround due to inability to use '[length]'
            // due to broken generating code (see comments in .xdc).
            var samples = [];
            seqState.samples = {
                samples: [],
            };
            for (var i = 0; i < mod.MAX_SAMPLES_IN_SEQ; ++i) {
                if (i < seqConf.samples.length) {
                    var type, idx;
                    if (seqConf.samples[i] == mod.AdcInChan_TS) {
                        type = AdcChan.Type_TEMPERATURE;
                        idx = undefined;
                    } else {
                        type = AdcChan.Type_ANALOG;
                        idx = seqConf.samples[i];
                    }
                    seqState.samples.samples[i] = AdcChan.create(type, idx);
                } else {
                    seqState.samples.samples[i] = null;
                }
            }
            seqState.samples.count = seqConf.samples.length;

            for (var bufIdx = 0; bufIdx < this.NUM_BUFS_PER_SEQ; ++bufIdx) {

                // Allocate buffer
                //var seqBuf = seqState.bufs[bufIdx];
                //seqBuf.buf.length = seqConf.bufSize;
                state.bufs[adc][seq][bufIdx].length = seqConf.bufSize;

                // Unfortunately can't do this in meta domain (don't have the addr)
                //seqState.payloadAddr[bufIdx] =
                //    seqConf.bufs[bufIdx].elem + Export.headerSize;
                seqState.payloadAddr[bufIdx] = null;
                seqState.numSamples =
                   (seqConf.bufSize - Export.headerSize) / this.SAMPLE_SIZE;

                seqState.exportBufIdx[bufIdx] = mod.exportBufIdxes[adc][seq][bufIdx];

                var arbSize = seqConf.arbSize;
                if (!arbSize)
                    arbSize = seqConf.samples.length;
                if (arbSize)
                    seqState.arbSize = PlatformInfo['UDMA_ARB_' + arbSize];
                else
                    seqState.arbSize = 0;
            }

            if (seqState.enabled) {
                var intNum = PlatformInfo['INT_ADC' + adc + 'SS' + seq];
                var params = new Hwi.Params;
                params.arg = (adc << 8) | seq;
                Hwi.create(intNum, '&onSampleTransferComplete', params);

                seqState.trigger = adcTriggerFromEnum(mod, seqConf.trigger);
                if (seqConf.trigger == mod.AdcTrigger_TIMER)
                    triggerTimerNeeded = true;
            } else {
                seqState.trigger = 0;
            }
        }

        if (triggerTimerNeeded) {
            adcState.triggerTimer = configTriggerTimer(mod, adcConfig.triggerTimer,
                                                       adcConfig.samplesPerSec);
        } else {
            adcState.triggerTimer = {
                timerDev: null,
                prescaler: 0,
                period: 0
            }
        }
    }
}

function configTriggerTimer(mod, timerCfg, samplesPerSec)
{
    var freqObj = BIOS.getCpuFreqMeta();
    if (freqObj.hi)
        throw "Frequencies above 2^32-1 are not supported";
    var freqHz = freqObj.lo;

    var half = timerCfg.half == mod.TimerHalf_A ?
                    GpTimer.Half_A : GpTimer.Half_B;
    var timerState = {
        timerDev: GpTimer.create(timerCfg.idx, half),
        prescaler: mod.TRIGGER_TIMER_DIVISOR - 1,
        period: freqHz / mod.TRIGGER_TIMER_DIVISOR / samplesPerSec,
    };
    return timerState;
}

function isPowerOf2(n)
{
    // Count bits: extactly one should be set
    bitsOn = 0;
    while (n) {
        if (n & 0x1)
            bitsOn += 1;
        n >>= 1;
    }
    return bitsOn == 1;
}

function adcTriggerFromEnum(mod, triggerEnum)
{
    // One of the few hardware values that cannot leverage a regular pattern
    if (triggerEnum == mod.AdcTrigger_PROCESSOR)
        return PlatformInfo.ADC_TRIGGER_PROCESSOR;
    if (triggerEnum == mod.AdcTrigger_TIMER)
        return PlatformInfo.ADC_TRIGGER_TIMER;
    throw "Unsupported trigger enum value: " + triggerEnum;
};

