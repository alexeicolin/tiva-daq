var Export;
var Hwi;
var BIOS;
var PlatformInfo;
var GpioPort;

function module$meta$init()
{
    BIOS = xdc.useModule('ti.sysbios.BIOS');
    Hwi = xdc.useModule('ti.sysbios.hal.Hwi');
    PlatformInfo = xdc.useModule('platforms.tiva.PlatformInfo');
    GpioPort = xdc.useModule('platforms.tiva.GpioPort');
    Export = xdc.useModule('tivadaq.Export');

    populateHardwareInfo(this);
}

function module$use()
{
}

function module$validate()
{
    // This is marked readonly, but check it anyway
    if (this.NUM_BUFS_PER_SEQ != 2)
        this.$logError("Only double-buffering is supported",
                       this, "NUM_BUFS_PER_SEQ");

    for (var adc = 0; adc < this.NUM_ADCS; ++adc) {
        if (!(adc in this.daqConfig.adcs)) // ADC not enabled by user
            continue;

        var adcConfig = this.daqConfig.adcs[adc];

        var triggerTimerNeeded = false;

        for (var seq = 0; seq < this.NUM_SEQS; ++seq) {
            if  (!(seq in adcConfig.seqs)) // sequencer not enabled by user
                continue;

            var seqConf = adcConfig.seqs[seq];

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
    // Helpful intermediate values
    var ADC_SEQ = PlatformInfo.ADC_O_SSMUX0;
    var ADC_SEQ_STEP = PlatformInfo.ADC_O_SSMUX1 - PlatformInfo.ADC_O_SSMUX0;
    var ADC_SSFIFO = PlatformInfo.ADC_O_SSFIFO0 - PlatformInfo.ADC_O_SSMUX0;

    for (var adc = 0; adc < this.NUM_ADCS; ++adc) {

        var adcConfig = mod.daqConfig.adcs[adc];
        var adcState = state.daqState.adcs[adc];

        adcState.periph = PlatformInfo['SYSCTL_PERIPH_ADC' + adc];
        adcState.base = PlatformInfo['ADC' + adc + '_BASE'];

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

            // Copy and initialize all unused elements of samples list.
            // This is a workaround due to inability to use '[length]'
            // due to broken generating code (see comments in .xdc).
            var samples = [];
            seqState.samples = {
                samples: [],
            };
            for (var i = 0; i < mod.MAX_SAMPLES_IN_SEQ; ++i) {
                if (i < seqConf.samples.length)
                    seqState.samples.samples[i] = seqConf.samples[i];
                else
                    seqState.samples.samples[i] = mod.AdcInChan_A0;
            }
            seqState.samples.count = seqConf.samples.length;

            var dmaChanNum = PlatformInfo['UDMA_CHANNEL_NUM_ADC_' + adc + '_' + seq];
            seqState.dataAddr =
                adcState.base + ADC_SEQ + ADC_SEQ_STEP * seq + ADC_SSFIFO;
            seqState.dmaInt = PlatformInfo['ADC_INT_DMA_SS' + seq];
            seqState.dmaChanNum = PlatformInfo['UDMA_CHANNEL_ADC_' + adc + '_' + seq];
            seqState.dmaChanAssign = PlatformInfo[
                        'UDMA_CH' + dmaChanNum + '_ADC' + adc + '_' + seq];

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

                // The meta-domain part of adding an export buffer
                seqState.exportBufIdx = Export.addBuffer(seqConf.bufSize);

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
                periph: 0,
                base: 0,
                half: 0,
                cfg: 0,
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
    var timerState = {
        periph: PlatformInfo['SYSCTL_PERIPH_TIMER' + timerCfg.idx],
        base: PlatformInfo['TIMER' + timerCfg.idx + '_BASE'],
        half: timerCfg.half == mod.TimerHalf_A ?
                PlatformInfo['TIMER_A'] : PlatformInfo['TIMER_B'],
        cfg: ((timerCfg.half == mod.TimerHalf_A ?
                    PlatformInfo['TIMER_CFG_A_PERIODIC'] :
                    PlatformInfo['TIMER_CFG_B_PERIODIC']) |
                PlatformInfo['TIMER_CFG_SPLIT_PAIR']),
        prescaler: mod.TRIGGER_TIMER_DIVISOR - 1,
        period: freqHz / mod.TRIGGER_TIMER_DIVISOR / samplesPerSec,
    };
    return timerState;
}

function populateHardwareInfo(mod)
{
    // Build ADC channel map
    // TODO: could optimize mod to include only ports that are actually used

    for (var i = 0; i < mod.adcInChanDescs.length; ++i) {
        var chanDesc = mod.adcInChanDescs[i];
        var adcInChan = {
            gpioPort: GpioPort.create(chanDesc.port, chanDesc.pin),
        };
        mod.adcInChans.length += 1;
        mod.adcInChans[mod.adcInChans.length -1] = adcInChan;
    }

    // Map: (ADC in chan num) -> ADC_CTL_{CH*,TS}
    var chan;
    for (chan = mod.AdcInChan_A_FIRST; chan <= mod.AdcInChan_A_LAST; ++chan) {
        mod.adcInChanToCtl.length += 1; 
        mod.adcInChanToCtl[mod.adcInChanToCtl.length - 1] =
            PlatformInfo['ADC_CTL_CH' + chan];
    }
    mod.adcInChanToCtl.length += 1; 
    mod.adcInChanToCtl[mod.adcInChanToCtl.length - 1] = PlatformInfo['ADC_CTL_TS'];
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

