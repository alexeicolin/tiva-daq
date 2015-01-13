package tivadaq;

import xdc.runtime.Log;
import xdc.runtime.Diags;
import xdc.runtime.Types;

import ti.sysbios.knl.Swi;
import platforms.tiva.UartPort;

@ModuleStartup
module Export {

    typedef Void (*TxCallback)(Void);

    struct HeaderField {
        UInt8 size; // bytes
        Bool fixed; // whether unchanged from packet to packet
    };

    enum HeaderFieldIndex { // order must match with 'header' config below
        HeaderFieldIndex_MARKER = 0,
        HeaderFieldIndex_SIZE,
        HeaderFieldIndex_IDX,
        HeaderFieldIndex_SEQ_NUM
    };

    // Length must match size in 'header' config below
    config UInt8 marker[] = [0xf0, 0x0d, 0xca, 0xfe];

    config HeaderField header[] = [
        { size: 4, fixed: true},  // marker
        { size: 2, fixed: true},  // buf size
        { size: 1, fixed: true},  // buf idx
        { size: 1, fixed: false}, // pkg seq num
    ];

    readonly config UInt headerFixedSize;
    readonly config UInt headerVarSize;
    readonly config UInt headerSize;

    config UInt uartPortIdx = 0;
    config UInt uartBaudRate = 115200;

    // Useful for flashing an LED for example
    config TxCallback txQueuedCallback = null;
    config TxCallback txCompletedCallback = null;

    // All buffers are added at compile time. Ideally, this would be done
    // by one 'add' function in meta-domain, however, we do not have the
    // buffer pointers in the meta domain (despite having allocated the
    // buffers in meta domain). Hence, the add operation is split across
    // meta domain and target domain:
    //    meta: id = addBuffer(size)
    //    target: setBufferPtr(id, addr)
    metaonly UInt addBuffer(UInt size);  // Returns buffer id
    Void initBuffer(UInt bufId, UInt8 *addr);

    Void processBuffers(UArg arg1, UArg arg2);
    Void exportBuffer(UInt id);
    Void exportAllBuffers();
    Void resetBufferSequenceNum();

    config Log.Event LM_startup = {
        mask: Diags.INFO,
        msg: "LM_startup: initializing Export"
    };
    config Log.Event LM_initBuffer = {
        mask: Diags.INFO,
        msg: "LM_initBuffer: id %u -> addr %p"
    };
    config Log.Event LM_initUART = {
        mask: Diags.INFO,
        msg: "LM_initUART: (base/periph) uart %p/0x%08x gpio %p/0x%08x, tx pin/assign 0x%02x/0x%08x, baud %u, sysclk %u hz"
    };
    config Log.Event LM_initUDMA = {
        mask: Diags.INFO,
        msg: "LM_initUDMA: uart base %p udma chan %u"
    };
    config Log.Event LM_exportBuffer = {
        mask: Diags.USER1,
        msg: "LM_exportBuffer: id %u, addr %p"
    };
    config Log.Event LM_exportAllBuffers = {
        mask: Diags.USER1,
        msg: "LM_exportAllBuffers"
    };
    config Log.Event LM_resetBufferSequenceNum = {
        mask: Diags.USER1,
        msg: "LM_resetBufferSequenceNum"
    };
    config Log.Event LM_transferStarted = {
        mask: Diags.USER1,
        msg: "LM_transferStarted: id %u, src %p -> dest %p, len %u"
    };
    config Log.Event LM_transferCompleted = {
        mask: Diags.USER1,
        msg: "LM_transferCompleted: addr %p"
    };
    config Log.Event LM_bufferContent = {
        mask: Diags.USER2,
        msg: "LM_expBufferContent: addr %p: %08x %08x %08x %08x %08x %08x %08x ..."
    };
    config Log.Event LM_noFullBuffers = {
        mask: Diags.USER1,
        msg: "LM_noFullBuffers: processBuffers returns"
    };
    config Log.Event LM_transferInProgress = {
        mask: Diags.USER1,
        msg: "LM_transferInProgress: processBuffers returns"
    };

  internal:

    config Types.FreqHz systemClockHz; // taken from BIOS

    struct ExportBuffer {
        UInt8 *addr;
        UInt16 size;
        Bool full;
    };

    metaonly config ExportBuffer exportBuffers[];

    struct Module_State {
        ExportBuffer expBuffers[length];
        ExportBuffer *curExpBuffer; // currently transfered buffer
        UInt32 bufferSeqNum;
        UartPort.Handle uartPort;
        Swi.Handle exportBuffersSwi;
    };
}
