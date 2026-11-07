#pragma once

#include "FFmpeg.h"

#include <QList>
#include <QString>

#include <atomic>

namespace dv::hw {

// Hardware video decoding through Direct3D 11 (D3D11VA). It is used only when the user allows it, the GPU / driver accept the
// codec, and falls back to the software decoder otherwise (also frame by frame: the picture is always valid).
void setEnabled(bool enabled);
bool enabled();

// A shared D3D11VA device (created once, null when the machine has none). Not owned by the caller.
AVBufferRef* sharedDevice();

// Frames decoded since start-up by each path (for the performance metrics).
quint64 hardwareFrames();
quint64 softwareFrames();
void countFrame(bool hardware);

struct DecoderInfo {
    QString id;        // d3d11va, dxva2, cuda, qsv ...
    QString label;
    bool available = false;
};
// Hardware device types FFmpeg was built with, each probed by really creating a device (cached after the first call).
QList<DecoderInfo> detectDecoders();

} // namespace dv::hw
