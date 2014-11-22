#ifndef EXPORT_H
#define EXPORT_H

#include <xdc/std.h>

struct ExportBuffer {
    UChar *addr;
    UInt16 size;
    Bool full;
};

// Export buffer list terminated by NULL addr entry
Void initExport(struct ExportBuffer *expBufferList);
Void processBuffers(UArg arg);
Void onExportComplete(UArg arg);
Void exportBuffer(Int idx);

#endif // EXPORT_H
