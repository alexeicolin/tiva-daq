var Log;
var Diags;
var BIOS;
var Hwi;
var Swi;
var HwAttrs;
var UartPort;

function module$meta$init()
{
    Log = xdc.useModule('xdc.runtime.Log');
    Diags = xdc.useModule('xdc.runtime.Diags');
    BIOS = xdc.useModule('ti.sysbios.BIOS');
    Hwi = xdc.useModule('ti.sysbios.hal.Hwi');
    Swi = xdc.useModule('ti.sysbios.knl.Swi');
    HwAttrs = xdc.useModule('platforms.tiva.hw.HwAttrs');
    UartPort = xdc.useModule('platforms.tiva.hw.UartPort');

    // Header size
    this.headerSize = 0;
    this.headerFixedSize = 0;
    this.headerVarSize = 0;
    for (var i = 0; i < this.header.length; ++i) {
        if (this.header[i].fixed)
            this.headerFixedSize += this.header[i].size; 
        else
            this.headerVarSize += this.header[i].size; 
        this.headerSize += this.header[i].size; 
    }
}

function module$use()
{
    this.systemClockHz = BIOS.getCpuFreqMeta();
}

function module$validate()
{
    if (this.systemClockHz == undefined)
        this.$logError("System clock freq not set", this, "systemClockHz");

    if (this.systemClockHz.lo == 0 || this.systemClockHz.hi != 0)
        this.$logError("Clock freq (" +
                       this.systemClockHz.hi + ":" + this.systemClockHz.lo + ") " +
                       "outside the supported range: 0:0xffffffff",
                       this, "systemClockHz");

    for (var i = 0; i < this.exportBuffers.length; ++i) {
        if (this.exportBuffers[i].size > HwAttrs.MAX_UDMA_TRANSFER_SIZE) {
            this.$logError("Buffer size (" + this.exportBuffers[i].size + ") " +
                           "exceeds max uDMA transfer size " +
                           "(" + this.MAX_UDMA_TRANSFER_SIZE + ")",
                           this, "exportBuffers");
        }
    }
}

function module$static$init(state, mod)
{
    state.uartPort = UartPort.create(mod.uartPortIdx);

    state.exportBuffers.length = mod.exportBuffers.length;
    for (var i = 0; i < mod.exportBuffers.length; ++i)
        state.exportBuffers[i] = mod.exportBuffers[i];

    var uartIntNum = HwAttrs['INT_UART' + this.uartPortIdx];
    Hwi.create(uartIntNum, '&Export_onExportComplete');

    var swiParams = new Swi.Params;
    swiParams.priority = 2;
    state.exportBuffersSwi =
        Swi.create('&tivadaq_Export_processBuffers', swiParams);

    state.curExpBuffer = null;
    state.bufferSeqNum = 0;
}

function addBuffer(size)
{
    // Addr is set in target-domain (unfortunately, no choice)
    var buf = {addr: null, size: size, full: false};
    this.exportBuffers.length += 1;
    this.exportBuffers[this.exportBuffers.length - 1] = buf;
    return this.exportBuffers.length - 1;
}
