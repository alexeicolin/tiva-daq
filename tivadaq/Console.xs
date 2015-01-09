var UART;
var UartPort;

function module$meta$init()
{
    UART = xdc.useModule('ti.drivers.UART');
    UartPort = xdc.useModule('platforms.tiva.UartPort');

    var SysMin = xdc.useModule('xdc.runtime.SysMin');
    SysMin.bufSize = 0x400;
    SysMin.outputFxn = '&tivadaq_Console_output';

    var System = xdc.useModule('xdc.runtime.System');
    System.SupportProxy = SysMin;
}

function module$static$init(state, mod)
{
    state.uartPort = UartPort.create(mod.uartPortIdx);
    state.uart = null;
}
