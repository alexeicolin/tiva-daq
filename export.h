#ifndef EXPORT_H
#define EXPORT_H

#include <xdc/std.h>

#define EXPBUF_HEADER_MARKER_SIZE 4
#define EXPBUF_HEADER_SIZE_SIZE   2
#define EXPBUF_HEADER_IDX_SIZE    1
#define EXPBUF_HEADER_SEQ_SIZE    1

#define EXPBUF_FIXED_HEADER_SIZE ( \
    EXPBUF_HEADER_MARKER_SIZE    + \
    EXPBUF_HEADER_SIZE_SIZE      + \
    EXPBUF_HEADER_IDX_SIZE         \
)

#define EXPBUF_VAR_HEADER_SIZE   ( \
    EXPBUF_HEADER_SEQ_SIZE         \
)

#define EXPBUF_HEADER_SIZE (EXPBUF_FIXED_HEADER_SIZE + EXPBUF_VAR_HEADER_SIZE)

struct ExportBuffer {
    UInt8 *addr;
    UInt16 size;
    Bool full;
};

// Export buffer list terminated by NULL addr entry
Void initExport(struct ExportBuffer *expBufferList);
Void processBuffers(UArg arg);
Void exportBuffer(Int idx);
Void exportAllBuffers();
UInt8 findExportBufferIdx(UInt8 *addr);
Void resetBufferSequenceNum();

#endif // EXPORT_H
