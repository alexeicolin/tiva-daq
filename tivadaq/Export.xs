var Hwi;
var Swi;
var PlatformInfo;
var UartPort;

function module$meta$init()
{
    Hwi = xdc.useModule('ti.sysbios.hal.Hwi');
    Swi = xdc.useModule('ti.sysbios.knl.Swi');
    PlatformInfo = xdc.useModule('platforms.tiva.PlatformInfo');
    UartPort = xdc.useModule('platforms.tiva.UartPort');

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

    for (var i = 0; i < this.exportBuffers.length; ++i) {
        var buf = this.exportBuffers[i];
        bufWriteFixedHeader(buf.addr, i, buf.size);
    }

    //this.header[0] = {size: 3, fixed: true};
}

function module$use()
{
}

function module$validate()
{
    for (var i = 0; i < this.exportBuffers.length; ++i) {
        if (this.exportBuffers[i].size > PlatformInfo.MAX_UDMA_TRANSFER_SIZE) {
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

    for (var i = 0; i < mod.exportBuffers.length; ++i)
        state.exportBuffers.push(mod.exportBuffers[i]);

    var uartIntNum = PlatformInfo['INT_UART' + this.uartPortIdx];
    Hwi.create(uartIntNum, '&Export_onExportComplete');

    var swiParams = new Swi.Params;
    swiParams.priority = 2;
    state.exportBuffersSwi =
        Swi.create('&tivadaq_Export_processBuffers', swiParams);

    state.curExpBuffer = null;
    state.bufferSeqNum = 0;
}

function addBuffer(addr, size)
{
    // Addr is set in target-domain (unfortunately, no choice)
    var buf = {addr: null, size: size, full: false};
    this.exportBuffers.length += 1;
    this.exportBuffers[this.exportBuffers.length - 1] = buf;
    return this.exportBuffers.length - 1;
}

function bufWriteFixedHeader(buf, idx, size)
{
    var offset = 0;
    offset += bufWriteBytes(buf, offset, this.MARKER);
    offset += bufWriteUInt(buf, offset, size, header["marker"].size);
    offset += bufWriteUInt(buf, offset, idx, header["index"].size);

    if (offset > this.fixedHeaderSize)
        throw "Internal error: fixed header data exceeded fixed header size";
}

function bufWriteBytes(buf, offset, bytes)
{
    for (var i = 0; i < numBytes; ++i)
        buf[offset + i] = bytes[i];
    return bytes.length;
}

function bufWriteUInt(buf, offset, n, numBytes)
{
    for (var i = 0; i < numBytes; ++i) {
        buf[offset + i] = n & 0xff;
        n >>= 8;
    }
    return numBytes;
}
