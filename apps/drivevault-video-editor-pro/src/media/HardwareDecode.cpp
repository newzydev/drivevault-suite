#include "HardwareDecode.h"

#include <QMutex>
#include <QMutexLocker>

namespace dv::hw {

namespace {
std::atomic_bool g_enabled{true};
std::atomic<quint64> g_hw{0};
std::atomic<quint64> g_sw{0};
QMutex g_mutex;
AVBufferRef* g_device = nullptr;
bool g_deviceTried = false;
} // namespace

void setEnabled(bool e) { g_enabled = e; }
bool enabled() { return g_enabled; }

AVBufferRef* sharedDevice()
{
    QMutexLocker lock(&g_mutex);
    if (!g_deviceTried) {
        g_deviceTried = true;
        if (av_hwdevice_ctx_create(&g_device, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0) < 0)
            g_device = nullptr;
    }
    return g_device;
}

quint64 hardwareFrames() { return g_hw; }
quint64 softwareFrames() { return g_sw; }
void countFrame(bool hardware) { ++(hardware ? g_hw : g_sw); }

QList<DecoderInfo> detectDecoders()
{
    static QMutex m;
    static QList<DecoderInfo> cached;
    static bool done = false;
    QMutexLocker lock(&m);
    if (done)
        return cached;
    AVHWDeviceType t = AV_HWDEVICE_TYPE_NONE;
    while ((t = av_hwdevice_iterate_types(t)) != AV_HWDEVICE_TYPE_NONE) {
        DecoderInfo d;
        d.id = QString::fromUtf8(av_hwdevice_get_type_name(t));
        switch (t) {
        case AV_HWDEVICE_TYPE_D3D11VA: d.label = QStringLiteral("Direct3D 11 (D3D11VA) - NVIDIA / AMD / Intel"); break;
        case AV_HWDEVICE_TYPE_DXVA2: d.label = QStringLiteral("DirectX VA 2 (DXVA2)"); break;
        case AV_HWDEVICE_TYPE_CUDA: d.label = QStringLiteral("NVIDIA NVDEC (CUDA)"); break;
        case AV_HWDEVICE_TYPE_QSV: d.label = QStringLiteral("Intel Quick Sync (QSV)"); break;
        case AV_HWDEVICE_TYPE_OPENCL: d.label = QStringLiteral("OpenCL"); break;
        case AV_HWDEVICE_TYPE_VULKAN: d.label = QStringLiteral("Vulkan"); break;
        default: d.label = d.id; break;
        }
        if (t == AV_HWDEVICE_TYPE_D3D11VA || t == AV_HWDEVICE_TYPE_DXVA2 || t == AV_HWDEVICE_TYPE_CUDA || t == AV_HWDEVICE_TYPE_QSV) {
            AVBufferRef* ref = nullptr;
            d.available = av_hwdevice_ctx_create(&ref, t, nullptr, nullptr, 0) >= 0;
            av_buffer_unref(&ref);
            cached.append(d);       // only decode-capable types are listed
        }
    }
    done = true;
    return cached;
}

} // namespace dv::hw
