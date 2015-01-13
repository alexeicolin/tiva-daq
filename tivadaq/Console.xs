var UART;
var UartPort;
var SysMin;

function module$meta$init()
{
    UART = xdc.useModule('ti.drivers.UART');
    UartPort = xdc.useModule('platforms.tiva.hw.UartPort');

    SysMin = xdc.useModule('xdc.runtime.SysMin');

    var System = xdc.useModule('xdc.runtime.System');
    System.SupportProxy = SysMin;
}

function module$use()
{
    SysMin.bufSize = this.bufferSize;
    SysMin.outputFxn = '&tivadaq_Console_output';
}

function module$static$init(state, mod)
{
    state.uartPort = UartPort.create(mod.uartPortIdx);
    state.uart = null;
}
