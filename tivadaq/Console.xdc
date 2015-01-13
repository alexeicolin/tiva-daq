package tivadaq;

import platforms.tiva.UartPort;

@ModuleStartup
module Console {
    Void open();
    Void close();
    Void output(/* const */ Char *buf, UInt size);
    Void flush();

  internal:

    metaonly config UInt8 uartPortIdx = 0;
    config UInt32 uartPortBaudRate = 115200;
    metaonly config UInt32 bufferSize = 4096;

    // Human-readable map used in constructing UartPort state
    metaonly readonly config Any uartGpioPorts = [
        {letter: 'A', rxPin: 0, txPin: 1}, // UART 0
        {letter: 'C', rxPin: 4, txPin: 5}, // UART 1
        // TODO: rest of ports
    ];

    struct Module_State {
        UartPort.Handle uartPort; // device info
        /* UART_Handle */ Void *uart; // actual "file descriptor"
    };
}
