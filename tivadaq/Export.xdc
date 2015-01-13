package tivadaq;

import xdc.runtime.Log;
import xdc.runtime.Diags;

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
    metaonly config UInt8 MARKER[] = [0xf0, 0x0d, 0xca, 0xfe];

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
    Void setBufferPointer(UInt bufId, UInt8 *addr);

    Void processBuffers(UArg arg1, UArg arg2);
    Void exportBuffer(UInt id);
    Void exportAllBuffers();
    Void resetBufferSequenceNum();

    config Log.Event LM_setBufferPointer = {
        mask: Diags.USER1,
        msg: "LM_setBufferPointer: id %u -> addr %p"
    };

  internal:

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
