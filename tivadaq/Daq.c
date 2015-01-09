#include <xdc/std.h>
#include <xdc/runtime/Assert.h>
#include <xdc/runtime/Error.h>
#include <xdc/runtime/Startup.h>

#include <platforms/tiva/GpioPeriph.h>
#include <platforms/tiva/GpioPort.h>
#include <platforms/tiva/Adc.h>
#include <platforms/tiva/AdcSeq.h>
#include <platforms/tiva/AdcChan.h>

#include <Export.h>

#include <stdbool.h>
#include <stdint.h>

#include <inc/hw_memmap.h>
#include <inc/hw_ints.h>
#include <inc/hw_adc.h>
#include <driverlib/adc.h>
#include <driverlib/gpio.h>
#include <driverlib/pin_map.h>
#include <driverlib/sysctl.h>
#include <driverlib/timer.h>
#include <driverlib/udma.h>
#include <driverlib/interrupt.h>

#include "package/internal/Daq.xdc.h"

/* The argument to sample transfer Hwi packs adc and seq number */
static inline Int adcIdxFromHwiArg(UArg arg)
{
    return ((UInt32)arg >> 8) & 0xff;
}
static inline Int seqIdxFromHwiArg(UArg arg)
{
    return ((UInt32)arg >> 0) & 0xff;
}

static inline Bool isAnalogAdcInChan(Daq_AdcInChanName chan)
{
    return Daq_AdcInChan_A_FIRST <= chan && chan <= Daq_AdcInChan_A_LAST;
}

static Void setupDMAADCTransfer(Int adc, Int seq, Int idx)
{
    const Daq_AdcState *adcState = &module->daqState.adcs[adc];
    const Daq_SeqState *seqState = &adcState->seqs[seq];
    const AdcSeq_Info *seqDev = AdcSeq_getInfo(seqState->seqDev);

    // static assert NUM_BUFS_PER_SEQ == 2
    UInt32 select = idx ? UDMA_ALT_SELECT : UDMA_PRI_SELECT;

    uDMAChannelTransferSet(seqDev->dmaChanNum | select, UDMA_MODE_PINGPONG,
        seqDev->dataAddr, seqState->payloadAddr[idx], seqState->numSamples);
}

static Void setupDMAADCControlSet(Int adc, Int seq, UInt32 bufSelect)
{
    const Daq_AdcState *adcState = &module->daqState.adcs[adc];
    const Daq_SeqState *seqState = &adcState->seqs[seq];
    const AdcSeq_Info *seqDev = AdcSeq_getInfo(seqState->seqDev);

    uDMAChannelControlSet(seqDev->dmaChanNum | bufSelect,
                          UDMA_SIZE_16 |
                          UDMA_SRC_INC_NONE |
                          UDMA_DST_INC_16 |
                          seqState->arbSize);
}

static Void processBuffer(Int adc, Int seq, Int idx)
{
    const Daq_AdcState *adcState = &module->daqState.adcs[adc];
    const Daq_SeqState *seqState = &adcState->seqs[seq];

    /* Setup the next transfer into this buffer that was just filled */
    setupDMAADCTransfer(adc, seq, idx);
    Export_exportBuffer(seqState->exportBufIdx);
}

Void onSampleTransferComplete(UArg arg)
{
    UInt adc = adcIdxFromHwiArg(arg);
    UInt seq = seqIdxFromHwiArg(arg);

    const Daq_AdcState *adcState = &module->daqState.adcs[adc];
    const Daq_SeqState *seqState = &adcState->seqs[seq];
    const Adc_Info *adcDev = Adc_getInfo(adcState->adcDev);
    const AdcSeq_Info *seqDev = AdcSeq_getInfo(seqState->seqDev);

    Bool primaryMode, altMode;

    UInt32 status = ADCIntStatusEx(adcDev->base, TRUE);
    ADCIntClearEx(adcDev->base, status);

    Assert_isTrue(!ADCSequenceUnderflow(adcDev->base, seq), NULL);

    primaryMode = uDMAChannelModeGet(seqDev->dmaChanNum | UDMA_PRI_SELECT);
    altMode = uDMAChannelModeGet(seqDev->dmaChanNum | UDMA_ALT_SELECT);

    /* If both have stopped, then we didn't process them in time */
    Assert_isTrue(!(primaryMode == UDMA_MODE_STOP &&
                    altMode == UDMA_MODE_STOP), NULL);

    if (primaryMode == UDMA_MODE_STOP)
        processBuffer(adc, seq, 0);
    else if (altMode == UDMA_MODE_STOP)
        processBuffer(adc, seq, 1);
    else
        Assert_isTrue(FALSE, NULL); /* shouldn't get here if neither is ready */
}

static Void initAdcInputPin(const AdcChan_Info *adcChan)
{
    const GpioPort_Info *gpioPort = GpioPort_getInfo(adcChan->gpioPort);
    const GpioPeriph_Info *gpioPeriph = GpioPeriph_getInfo(gpioPort->periph);

    SysCtlPeripheralEnable(gpioPeriph->periph);
    GPIOPinTypeADC(gpioPeriph->base, gpioPort->pin);
}

static UInt initADCSequence(Int adc, Int seq)
{
    Int sample;
    UInt32 sampleChanCtl;
    const Daq_AdcState *adcState = &module->daqState.adcs[adc];
    const Daq_SeqState *seqState = &adcState->seqs[seq];
    const Adc_Info *adcDev = Adc_getInfo(adcState->adcDev);
    const AdcChan_Info *sampleChan;

    ADCSequenceDisable(adcDev->base, seq);
    ADCSequenceConfigure(adcDev->base, seq, seqState->trigger, seqState->priority);

    for (sample = 0; sample < seqState->samples.count; ++sample) {
        sampleChan = AdcChan_getInfo(seqState->samples.samples[sample]);
        sampleChanCtl = sampleChan->ctlValue;
        // TODO: move this IE, END logic to meta-domain
        if (sample == seqState->samples.count - 1)
            sampleChanCtl |= ADC_CTL_IE | ADC_CTL_END;
        ADCSequenceStepConfigure(adcDev->base, seq, sample, sampleChanCtl);
        if (sampleChan->type == AdcChan_Type_ANALOG)
            initAdcInputPin(sampleChan);
    }

    ADCSequenceEnable(adcDev->base, seq);
    return sample; /* sequence length */
}

static Void initADCDMA(Int adc, Int seq)
{
    const Daq_AdcState *adcState = &module->daqState.adcs[adc];
    const Daq_SeqState *seqState = &adcState->seqs[seq];
    const Adc_Info *adcDev = Adc_getInfo(adcState->adcDev);
    const AdcSeq_Info *seqDev = AdcSeq_getInfo(seqState->seqDev);

    ADCSequenceDMAEnable(adcDev->base, seq);
    ADCIntEnableEx(adcDev->base, seqDev->dmaInt);

    uDMAChannelAssign(seqDev->dmaChanAssign);

    uDMAChannelAttributeDisable(seqDev->dmaChanNum,
                                UDMA_ATTR_ALTSELECT |
                                UDMA_ATTR_REQMASK);
    uDMAChannelAttributeDisable(seqDev->dmaChanNum,
                                UDMA_ATTR_HIGH_PRIORITY |
                                UDMA_ATTR_USEBURST);
    setupDMAADCControlSet(adc, seq, UDMA_PRI_SELECT);
    setupDMAADCControlSet(adc, seq, UDMA_ALT_SELECT);
    setupDMAADCTransfer(adc, seq, 0);
    setupDMAADCTransfer(adc, seq, 1);

    uDMAChannelEnable(seqDev->dmaChanNum);
}

static Void initADCTimer(Int adc)
{
    const Daq_TimerState *triggerTimer =
        &module->daqState.adcs[adc].triggerTimer;

    SysCtlPeripheralEnable(triggerTimer->periph);
    TimerConfigure(triggerTimer->base, triggerTimer->cfg);

    TimerPrescaleSet(triggerTimer->base, triggerTimer->half,
                     triggerTimer->prescaler);
    TimerLoadSet(triggerTimer->base, triggerTimer->half, triggerTimer->period);
    TimerControlTrigger(triggerTimer->base, triggerTimer->half, TRUE);

    /* stop in debug mode */
    TimerControlStall(triggerTimer->base, triggerTimer->half, true);
}

static Void initADCHwAvg(Int adc)
{
    const Daq_AdcState *adcState = &module->daqState.adcs[adc];
    const Adc_Info *adcDev = Adc_getInfo(adcState->adcDev);
    ADCHardwareOversampleConfigure(adcDev->base, adcState->hwAvgFactor);
}

static Void initADC()
{
    const Daq_AdcState *adcState;
    const Adc_Info *adcDev;
    Int adc, seq;
    Bool adcEnabled;

    for (adc = 0; adc < Daq_NUM_ADCS; ++adc) {
        adcState = &module->daqState.adcs[adc];
        adcDev = Adc_getInfo(adcState->adcDev);

        adcEnabled = false;
        for (seq = 0; seq < Daq_NUM_SEQS; ++seq) {
            if (adcState->seqs[seq].enabled) {
                adcEnabled = true;
                break;
            }
        }

        if (adcEnabled)
            SysCtlPeripheralEnable(adcDev->periph);

        for (seq = 0; seq < Daq_NUM_SEQS; ++seq) {
            if (adcState->seqs[seq].enabled) {
                initADCSequence(adc, seq);
                initADCDMA(adc, seq);
            }
        }
        initADCTimer(adc);
        initADCHwAvg(adc);
    }
}

// Ideally this would be done completely in meta domain, but we don't
// have the buffer pointers in meta domain (despite having allocated
// the buffers there).
static Void initBuffers()
{
    Daq_AdcState *adcState;
    Daq_SeqState *seqState;
    Int adc, seq, bufIdx;
    UInt8 *bufAddr;

    for (adc = 0; adc < Daq_NUM_ADCS; ++adc) {
        adcState = &module->daqState.adcs[adc];
        for (seq = 0; seq < Daq_NUM_SEQS; ++seq) {
            seqState = &adcState->seqs[seq];
            if (seqState->enabled) {
                for (bufIdx = 0; bufIdx < Daq_NUM_BUFS_PER_SEQ; ++bufIdx) {
                    bufAddr = module->bufs[adc][seq][bufIdx].elem;
                    seqState->payloadAddr[bufIdx] = bufAddr + Export_headerSize;
                    Export_setBufferPointer(seqState->exportBufIdx, bufAddr);
                }
            }
        }
    }
}

Int Daq_Module_startup(Int state)
{
    /* Rely on uDMA init done as part of export module */
    initADC();
    return Startup_DONE;
}

static Void toggleAdcTimers(Bool enable)
{
    Int adc, seq;
    const Daq_AdcState *adcState;
    const Daq_SeqState *seqState;
    const Daq_TimerState *triggerTimer;

    for (adc = 0; adc < Daq_NUM_ADCS; ++adc) {
        adcState = &module->daqState.adcs[adc];
        for (seq = 0; seq < Daq_NUM_SEQS; ++seq) {
            seqState = &adcState->seqs[seq];
            if (seqState->enabled && seqState->trigger == ADC_TRIGGER_TIMER) {
                triggerTimer = &adcState->triggerTimer;
                if (enable)
                    TimerEnable(triggerTimer->base, triggerTimer->half);
                else
                    TimerDisable(triggerTimer->base, triggerTimer->half);
            }
        }
    }
}

static Void startAdcTimers()
{
    toggleAdcTimers(TRUE);
}

static Void stopAdcTimers()
{
    toggleAdcTimers(FALSE);
}

Void Daq_start()
{
    Export_resetBufferSequenceNum();
    startAdcTimers();
}

Void Daq_stop()
{
    stopAdcTimers();
    Export_exportAllBuffers();
}

Void Daq_trigger(Int adc, Int seq)
{
    const Daq_AdcState *adcState = &module->daqState.adcs[adc];
    const Daq_SeqState *seqState = &adcState->seqs[seq];
    const Adc_Info *adcDev = Adc_getInfo(adcState->adcDev);

    Assert_isTrue(seqState->enabled, NULL);
    Assert_isTrue(seqState->trigger == ADC_TRIGGER_PROCESSOR, NULL);
    
    ADCProcessorTrigger(adcDev->base, seq);
}
