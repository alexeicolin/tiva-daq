#include <xdc/std.h>
#include <xdc/runtime/Assert.h>
#include <xdc/runtime/Error.h>
#include <ti/sysbios/hal/Hwi.h>

#include <stdbool.h>
#include <stdint.h>

#include <inc/hw_memmap.h>
#include <inc/hw_ints.h>
#include <inc/hw_adc.h>
#include <driverlib/adc.h>
#include <driverlib/gpio.h>
#include <driverlib/pin_map.h>
#include <driverlib/sysctl.h>
#include "driverlib/timer.h"
#include <driverlib/udma.h>
#include <driverlib/interrupt.h>

#include "export.h"

#include "daq.h"

/* Hardware description macros and data structure */

/* The names in driverlib/udma.h are a bit irregular, so fix them up */
#define UDMA_CHANNEL_ADC_0_0 UDMA_CHANNEL_ADC0
#define UDMA_CHANNEL_ADC_0_1 UDMA_CHANNEL_ADC1
#define UDMA_CHANNEL_ADC_0_2 UDMA_CHANNEL_ADC2
#define UDMA_CHANNEL_ADC_0_3 UDMA_CHANNEL_ADC3
#define UDMA_CHANNEL_ADC_1_0 UDMA_SEC_CHANNEL_ADC10
#define UDMA_CHANNEL_ADC_1_1 UDMA_SEC_CHANNEL_ADC11
#define UDMA_CHANNEL_ADC_1_2 UDMA_SEC_CHANNEL_ADC12
#define UDMA_CHANNEL_ADC_1_3 UDMA_SEC_CHANNEL_ADC13

/* From TM4C datasheet (p. 586) */
#define UDMA_CHANNEL_NUM_ADC_0_0 14
#define UDMA_CHANNEL_NUM_ADC_0_1 15
#define UDMA_CHANNEL_NUM_ADC_0_2 16
#define UDMA_CHANNEL_NUM_ADC_0_3 17
#define UDMA_CHANNEL_NUM_ADC_1_0 24
#define UDMA_CHANNEL_NUM_ADC_1_1 25
#define UDMA_CHANNEL_NUM_ADC_1_2 26
#define UDMA_CHANNEL_NUM_ADC_1_3 27

/* From driverlib/adc.c */
#define ADC_SEQ                 (ADC_O_SSMUX0)
#define ADC_SEQ_STEP            (ADC_O_SSMUX1 - ADC_O_SSMUX0)
#define ADC_SSFIFO              (ADC_O_SSFIFO0 - ADC_O_SSMUX0)

#define ADC_SEQ_DATA_ADDR(adc, seq) \
    ((void *)(ADC_BASE(adc) + ADC_SEQ + ADC_SEQ_STEP * seq + ADC_SSFIFO))
#define ADC_SEQ_DMA_INT(seq) CONCAT(ADC_INT_DMA_SS, seq)

/* Platform specific assumption: continuity and total num channels */
#define IS_ANALOG_INPUT_CHAN(chan) (ADC_CTL_CH0 <= chan && chan <= ADC_CTL_CH11)

#define ADC_DMA_CHAN(adc, seq) CONCATU3(UDMA_CHANNEL_ADC, adc, seq)
#define ADC_DMA_CHAN_NUM(adc, seq) \
    CONCATU3(UDMA_CHANNEL_NUM_ADC, adc, seq)
#define ADC_DMA_CHAN_ASSIGN(adc, seq) \
    CONCAT6(UDMA_CH, ADC_DMA_CHAN_NUM(adc, seq), _ADC, adc, _, seq)

struct AdcSeqDev {
    void *dataAddr;
    UInt intNum;
    UInt32 dmaInt;
    UInt32 dmaChanNum;
    UInt32 dmaChanAssign;
};

struct AdcDev {
    UInt32 periph;
    UInt32 baseAddr;
    struct AdcSeqDev seq[NUM_SEQS];
};

#define ADC_SEQ_DEV_ENTRY(adc, seq)   \
    {                                 \
        ADC_SEQ_DATA_ADDR(adc, seq),  \
        ADC_SEQ_INT(adc, seq),        \
        ADC_SEQ_DMA_INT(seq),         \
        ADC_DMA_CHAN(adc, seq),       \
        ADC_DMA_CHAN_ASSIGN(adc, seq) \
    }

#define ADC_DEV_ENTRY(adc)             \
    {                                  \
        ADC_PERIPH(adc),               \
        ADC_BASE(adc),                 \
        {                              \
            ADC_SEQ_DEV_ENTRY(adc, 0), \
            ADC_SEQ_DEV_ENTRY(adc, 1), \
            ADC_SEQ_DEV_ENTRY(adc, 2), \
            ADC_SEQ_DEV_ENTRY(adc, 3)  \
        }                              \
    }

static struct AdcDev adcDevices[] = {
    ADC_DEV_ENTRY(0),
    ADC_DEV_ENTRY(1)
};

/* analog input channel num -> (gpio port, pin): map in form of a list */
/* From TM4C datasheet Table 13-1 on p. 798 */

struct AdcPinMap {
    UInt32 ch;
    UInt32 port;
    UInt8  pin;
};

#define ADC_PIN_MAP_ENTRY(ch, port, pin) \
    {                                    \
        CONCAT(ADC_CTL_CH, ch),          \
        CONCAT3(GPIO_PORT, port, _BASE), \
        CONCAT(GPIO_PIN_, pin)           \
    }

static struct AdcPinMap adcPinMap[] = {
    ADC_PIN_MAP_ENTRY( 0, E, 3),
    ADC_PIN_MAP_ENTRY( 1, E, 2),
    ADC_PIN_MAP_ENTRY( 2, E, 1),
    ADC_PIN_MAP_ENTRY( 3, E, 0),
    ADC_PIN_MAP_ENTRY( 4, D, 3),
    ADC_PIN_MAP_ENTRY( 5, D, 2),
    ADC_PIN_MAP_ENTRY( 6, D, 1),
    ADC_PIN_MAP_ENTRY( 7, D, 0),
    ADC_PIN_MAP_ENTRY( 8, E, 5),
    ADC_PIN_MAP_ENTRY( 9, E, 4),
    ADC_PIN_MAP_ENTRY(10, B, 4),
    ADC_PIN_MAP_ENTRY(11, B, 5),
    { 0, NULL, 0 }
};

static const struct DaqConfig *daqConfig;

#if NUM_BUFS_PER_SEQ != 2
#error Only two buffers per sequence are supported.
#endif

#define MAX_EXP_BUFS (NUM_ADCS * NUM_SEQS * NUM_BUFS_PER_SEQ)
static struct ExportBuffer exportBuffers[MAX_EXP_BUFS];
static UInt8 exportBufIdx[NUM_ADCS][NUM_SEQS][NUM_BUFS_PER_SEQ];

/* These "convert" a plain buffer to an exportable buffer */
#define NUM_SAMPLES_PER_BUF(size) ((size - EXPBUF_HEADER_SIZE) / SAMPLE_SIZE)
#define BUF_PAYLOAD_ADDR(bufAddr) (bufAddr + EXPBUF_HEADER_SIZE)

/* The argument to sample transfer Hwi packs adc and seq number */
#define ADC_HWI_ARG(adc, seq) ((UArg)((UInt32)adc << 8) | (UInt32)seq)
#define ADC_FROM_ADC_HWI_ARG(arg) (((UInt32)arg >> 8) & 0xff)
#define SEQ_FROM_ADC_HWI_ARG(arg) (((UInt32)arg >> 0) & 0xff)

/* Used once at init time only, so not making a lookup table */
static inline UInt32 arbSizeFromSeqLen(UInt seqLen)
{
    if (seqLen == 1)
        return UDMA_ARB_1;
    if (seqLen == 2)
        return UDMA_ARB_2;
    if (seqLen == 4)
        return UDMA_ARB_4;
    if (seqLen == 8)
        return UDMA_ARB_8;
    Assert_isTrue(FALSE, NULL);
    return 0; /* unreachable */
}

static Void setupDMAADCTransfer(Int adc, Int seq, Int idx)
{
    const struct AdcConfig *adcConfig = &daqConfig->adcConfigs[adc];
    const struct AdcDev *adcDev = &adcDevices[adc];
    const struct AdcSeqDev *seqDev = &adcDev->seq[seq];

    const struct SequenceBuffer *seqBuf = &adcConfig->seqs[seq].buf;
    UInt8 *bufAddr = seqBuf->addr + idx * seqBuf->size;

    // static assert NUM_BUFS_PER_SEQ == 2
    UInt32 select = idx ? UDMA_ALT_SELECT : UDMA_PRI_SELECT;

    uDMAChannelTransferSet(seqDev->dmaChanNum | select,
        UDMA_MODE_PINGPONG,
        seqDev->dataAddr,
        BUF_PAYLOAD_ADDR(bufAddr), NUM_SAMPLES_PER_BUF(seqBuf->size));
}

static Void setupDMAADCControlSet(Int adc, Int seq,
                                  UInt32 bufSelect, UInt seqLen)
{
    const struct AdcDev *adcDev = &adcDevices[adc];
    const struct AdcSeqDev *seqDev = &adcDev->seq[seq];
    UInt32 arbSize = arbSizeFromSeqLen(seqLen); 

    uDMAChannelControlSet(seqDev->dmaChanNum | bufSelect,
                          UDMA_SIZE_16 |
                          UDMA_SRC_INC_NONE |
                          UDMA_DST_INC_16 |
                          arbSize);
}


static Void processBuffer(Int adc, Int seq, Int idx)
{
    Assert_isTrue(0 <= idx && idx < NUM_BUFS_PER_SEQ, NULL);

    /* Setup the next transfer into this buffer that was just filled */
    setupDMAADCTransfer(adc, seq, idx);
    exportBuffer(exportBufIdx[adc][seq][idx]);
}

static Void onSampleTransferComplete(UArg arg)
{
    UInt adc = ADC_FROM_ADC_HWI_ARG(arg);
    UInt seq = SEQ_FROM_ADC_HWI_ARG(arg);

    const struct AdcDev *adcDev = &adcDevices[adc];
    const struct AdcSeqDev *seqDev = &adcDev->seq[seq];
    UInt32 adcBase = adcDev->baseAddr;

    Bool primaryMode, altMode;

    UInt32 status = ADCIntStatusEx(adcBase, TRUE);
    ADCIntClearEx(adcBase, status);

    Assert_isTrue(!ADCSequenceUnderflow(adcBase, seq), NULL);

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

static Void initADCHwi(Int adc, Int seq)
{
    Hwi_Handle hwiObj;
    Hwi_Params params;
    Error_Block eb;

    Error_init(&eb);
    Hwi_Params_init(&params);
    params.arg = ADC_HWI_ARG(adc, seq);
    hwiObj = Hwi_create(adcDevices[adc].seq[seq].intNum,
                        &onSampleTransferComplete, &params, &eb);
    Assert_isTrue(hwiObj != NULL, NULL);
}

static Void initAdcInputPin(UInt32 chan)
{
    const struct AdcPinMap *pinMap = &adcPinMap[0];
    while (pinMap->port && pinMap->ch != chan)
        pinMap++;
    Assert_isTrue(pinMap->port, NULL);

    SysCtlPeripheralEnable(pinMap->port);
    GPIOPinTypeADC(pinMap->port, pinMap->pin);
}

static UInt initADCSequence(Int adc, Int seq)
{
    Int sample;
    UInt32 sampleChan;
    const struct AdcConfig *adcConfig = &daqConfig->adcConfigs[adc];
    const struct AdcDev *adcDev = &adcDevices[adc];
    UInt32 adcBase = adcDev->baseAddr;
    const struct SequenceConfig *seqConf = &adcConfig->seqs[seq];

    ADCSequenceDisable(adcBase, seq);
    ADCSequenceConfigure(adcBase, seq, seqConf->trigger, seqConf->priority);

    for (sample = 0; seqConf->samples[sample] != ADC_SEQ_END; ++sample) {
        sampleChan = seqConf->samples[sample];
        if (sample == MAX_SAMPLES_IN_SEQ - 1 ||
            seqConf->samples[sample + 1] == ADC_SEQ_END)
            sampleChan |= ADC_CTL_IE | ADC_CTL_END;
        ADCSequenceStepConfigure(adcBase, seq, sample, sampleChan);
        sampleChan = seqConf->samples[sample]; /* without end flags */
        if (IS_ANALOG_INPUT_CHAN(sampleChan))
            initAdcInputPin(sampleChan);
    }

    ADCSequenceEnable(adcBase, seq);
    return sample; /* sequence length */
}

static Void initADCDMA(Int adc, Int seq, UInt seqLen)
{
    const struct AdcDev *adcDev = &adcDevices[adc];
    const struct AdcSeqDev *seqDev = &adcDev->seq[seq];
    UInt32 adcBase = adcDev->baseAddr;
    UInt32 chanNum = seqDev->dmaChanNum;

    ADCSequenceDMAEnable(adcBase, seq);
    ADCIntEnableEx(adcBase, seqDev->dmaInt);

    uDMAChannelAssign(seqDev->dmaChanAssign);

    uDMAChannelAttributeDisable(chanNum,
                                UDMA_ATTR_ALTSELECT |
                                UDMA_ATTR_REQMASK);
    uDMAChannelAttributeDisable(chanNum,
                                UDMA_ATTR_HIGH_PRIORITY |
                                UDMA_ATTR_USEBURST);
    setupDMAADCControlSet(adc, seq, UDMA_PRI_SELECT, seqLen);
    setupDMAADCControlSet(adc, seq, UDMA_ALT_SELECT, seqLen);
    setupDMAADCTransfer(adc, seq, 0);
    setupDMAADCTransfer(adc, seq, 1);

    uDMAChannelEnable(chanNum);
}

static Void initADCTimer(Int adc, UInt32 samplesPerSec)
{
    const struct AdcConfig *adcConfig = &daqConfig->adcConfigs[adc];
    UInt32 timerBase = adcConfig->triggerTimerBase;
    UInt32 timerHalf = adcConfig->triggerTimerHalf;

    //UInt32 divisor = 16; /* appropriate for 100 to 100k samples/sec */
    UInt32 divisor = 128; /* appropriate for 10 to 10k samples/sec */
    UInt32 prescaler = divisor - 1;
    UInt32 period = SysCtlClockGet() / divisor / samplesPerSec;

    /* These limits are imposed by the divisor chosen above:
     * 1 <= 80Mhz/divisor/minSamplesPerSec <= 2**16
     * but probably the lower limit counts should be a bunch more than just 1.
     * For example, for divisor 16:
     * 80MHz/16/100 = 50000 and 80MHz/16/100000 = 50 */
    /* TODO: calculate divisor automatically */
    //Assert_isTrue(samplesPerSec >= 100 && samplesPerSec <= 100000, NULL);
    Assert_isTrue(samplesPerSec >= 10 && samplesPerSec <= 10000, NULL);
    Assert_isTrue(SysCtlClockGet() % divisor == 0, NULL);

    TimerPrescaleSet(timerBase, timerHalf, prescaler);
    TimerLoadSet(timerBase, timerHalf, period);
    TimerControlTrigger(timerBase, timerHalf, TRUE);
    TimerControlStall(timerBase, timerHalf, true); /* stop in debug mode */
}

static Void initADC(UInt32 samplesPerSec)
{
    Int adc, seq;
    UInt seqLen;

    /* TODO: Specify ports+pin in sequence config */
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
    GPIOPinTypeADC(GPIO_PORTE_BASE,
                   GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_5);
    GPIOPinTypeADC(GPIO_PORTD_BASE,
                   GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);

    for (adc = 0; adc < NUM_ADCS; ++adc) {
        SysCtlPeripheralEnable(adcDevices[adc].periph);
        for (seq = 0; seq < NUM_SEQS; ++seq) {
            if (daqConfig->adcConfigs[adc].seqs[seq].enabled) {
                initADCHwi(adc, seq);
                seqLen = initADCSequence(adc, seq);
                initADCDMA(adc, seq, seqLen);
            }
        }
        initADCTimer(adc, samplesPerSec);
    }
}

static Void initExportBuffers()
{
    Int adc, seq, bufIdx, i = 0;
    struct ExportBuffer *expBuf;
    const struct SequenceConfig *seqConf;

    for (adc = 0; adc < NUM_ADCS; ++adc) {
        for (seq = 0; seq < NUM_SEQS; ++seq) {
            seqConf = &daqConfig->adcConfigs[adc].seqs[seq];
            if (seqConf->enabled) {
                for (bufIdx = 0; bufIdx < NUM_BUFS_PER_SEQ; ++bufIdx) {
                    expBuf = &exportBuffers[i++];

                    expBuf->addr = seqConf->buf.addr + bufIdx * seqConf->buf.size;
                    expBuf->size = seqConf->buf.size;
                }
            }
        }
    }
}

static Void initExportBufferIdxMap()
{
    Int adc, seq, bufIdx;
    UInt8 *addr;
    const struct SequenceConfig *seqConf;

    for (adc = 0; adc < NUM_ADCS; ++adc) {
        for (seq = 0; seq < NUM_SEQS; ++seq) {
            seqConf = &daqConfig->adcConfigs[adc].seqs[seq];
            if (seqConf->enabled) {
                for (bufIdx = 0; bufIdx < NUM_BUFS_PER_SEQ; ++bufIdx) {
                    addr = seqConf->buf.addr + bufIdx * seqConf->buf.size;
                    exportBufIdx[adc][seq][bufIdx] = findExportBufferIdx(addr);
                }
            }
        }
    }
}

Void initDAQ(const struct DaqConfig *conf, UInt32 samplesPerSec)
{
    daqConfig = conf;

    /* Rely on uDMA init done as part of export module */

    initExportBuffers();
    initExport(exportBuffers);
    initExportBufferIdxMap();

    initADC(samplesPerSec);
}
