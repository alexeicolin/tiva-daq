var Log;
var Diags;
var Export;
var Hwi;
var BIOS;
var HwAttrs;
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
    HwAttrs = xdc.useModule('platforms.hw.tiva.HwAttrs');
    GpioPort = xdc.useModule('platforms.hw.tiva.GpioPort');
    Adc = xdc.useModule('platforms.hw.tiva.Adc');
    AdcSeq = xdc.useModule('platforms.hw.tiva.AdcSeq');
    AdcChan = xdc.useModule('platforms.hw.tiva.AdcChan');
    GpTimer = xdc.useModule('platforms.hw.tiva.GpTimer');
    Export = xdc.useModule('tivadaq.Export');
}

function timerHalfFromString(mod, str)
{
    if (str == 'A') return mod.TimerHalf_A;
    if (str == 'B') return mod.TimerHalf_B;
    throw "Unrecognized timer half value: '" + str + "'";
}

function triggerFromString(mod, str)
{
    if (str == "timer") return mod.AdcTrigger_TIMER;
    if (str == "processor") return mod.AdcTrigger_PROCESSOR;
    throw "Unrecognized trigger value: '" + str + "'";
}

function sampleFromString(mod, str)
{
    return mod['AdcInChan_' + str];
}

function configFromObject(mod, daqConfigObj)
{
    for (var adc in daqConfigObj.adcs) {
        var adcConfigObj = daqConfigObj.adcs[adc]; // adc is a key
        var adcConfig = mod.daqConfig.adcs[adc]; // adc is an index

        adcConfig.samplesPerSec = adcConfigObj.samplesPerSec;
        adcConfig.hwAvgFactor = adcConfigObj.hwAvgFactor;
        if (adcConfigObj.triggerTimer) {
            adcConfig.triggerTimer = {
                idx: adcConfigObj.triggerTimer.idx,
                half: timerHalfFromString(mod, adcConfigObj.triggerTimer.half),
            }
        }

        for (var seq in adcConfigObj.seqs) {
            var seqConfigObj = adcConfigObj.seqs[seq]; // seq is a key
            var seqConfig = adcConfig.seqs[seq]; // seq is an index

            seqConfig.enabled = true; // all *listed* seqs are enabled
            seqConfig.priority = seqConfigObj.priority;
            seqConfig.trigger = triggerFromString(mod, seqConfigObj.trigger);
            seqConfig.bufSize = seqConfigObj.bufSize;
            seqConfig.samples.length = seqConfigObj.samples.length;
            for (var j = 0; j < seqConfigObj.samples.length; ++j) {
                var sampleConfig = seqConfigObj.samples[j];
                var name;
                for (name in sampleConfig) break; // name is the only property
                if (!name)
                    throw "Empty sample config for adc.seq.sample " +
                          adc + "." + seq + "." + j;
                var chan = sampleConfig[name];
                seqConfig.samples[j] = sampleFromString(mod, chan);
            }
        }
    }
}

function module$use()
{
    if (this.jsonConfigPath) {
        // Dirty trick for adding the file as a dependency
        xdc.loadTemplate('tivadaq/' + this.jsonConfigPath);

        var daqConfigObj = loadJson(this.jsonConfigPath);
        configFromObject(this, daqConfigObj);
    }

    // Add buffers to Export module and save the assigned IDs
    for (var adc = 0; adc < this.NUM_ADCS; ++adc) {
        var adcConfig = this.daqConfig.adcs[adc];
        for (var seq = 0; seq < this.NUM_SEQS; ++seq) {
            var seqConf = adcConfig.seqs[seq];
            for (var bufIdx = 0; bufIdx < this.NUM_BUFS_PER_SEQ; ++bufIdx) {
                // The meta-domain part of adding an export buffer
                var expBufIdx = 0; // ideally, this should be an invalid index
                if (seqConf.enabled) {
                    var bufId = (adc << 4) | seq; // for use by parser on host
                    expBufIdx = Export.addBuffer(bufId, seqConf.bufSize);
                }
                this.exportBufIdxes[adc][seq][bufIdx] = expBufIdx;
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
        adcState.hwAvgFactor =
            adcConfig.hwAvgFactor ? adcConfig.hwAvgFactor : 1;

        var triggerTimerNeeded = false;

        for (var seq = 0; seq < this.NUM_SEQS; ++seq) {

            var seqConf = adcConfig.seqs[seq];
            var seqState = adcState.seqs[seq];

            // Module state needs to be initialized to defined values
            if (!seqConf.enabled)
                seqConf.bufSize = 0;
            if (seqConf.priority == undefined)
                seqConf.priority = 0;

            seqState.enabled = seqConf.enabled ? true : false; // handle undef
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
                        idx = seqConf.samples[i] - mod.AdcInChan_A0;
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
                    seqState.arbSize = HwAttrs['UDMA_ARB_' + arbSize];
                else
                    seqState.arbSize = 0;
            }

            if (seqState.enabled) {
                var intNum = HwAttrs['INT_ADC' + adc + 'SS' + seq];
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
        return HwAttrs.ADC_TRIGGER_PROCESSOR;
    if (triggerEnum == mod.AdcTrigger_TIMER)
        return HwAttrs.ADC_TRIGGER_TIMER;
    throw "Unsupported trigger enum value: " + triggerEnum;
};

function loadJson(path)
{
    var jsonFile = new java.io.BufferedReader(java.io.FileReader(path));
    var content = "_obj = "; // without assignment eval doesn't return the obj
    while ((line = jsonFile.readLine()) != null)
        content += line + "\n";
    return eval(content);
}
