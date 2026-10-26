#include "app/GpuSelection.h"

#include <QDir>
#include <QFile>
#include <QSettings>

namespace pf {

namespace {
QSettings settings()
{
    // Usable before QApplication exists (organisation/app names not set yet).
    return QSettings(QStringLiteral("PrismForge"), QStringLiteral("PrismForge"));
}
} // namespace

QString toString(GpuPreference preference)
{
    switch (preference) {
    case GpuPreference::Automatic: return QStringLiteral("auto");
    case GpuPreference::Nvidia: return QStringLiteral("nvidia");
    case GpuPreference::Integrated: return QStringLiteral("integrated");
    }
    return {};
}

GpuPreference gpuPreferenceFromString(const QString& text)
{
    const QString t = text.trimmed().toLower();
    if (t == u"nvidia" || t == u"discrete" || t == u"dgpu")
        return GpuPreference::Nvidia;
    if (t == u"integrated" || t == u"igpu" || t == u"intel" || t == u"default")
        return GpuPreference::Integrated;
    return GpuPreference::Automatic;
}

GpuPreference savedGpuPreference()
{
    return gpuPreferenceFromString(settings().value(QStringLiteral("gpu/preference")).toString());
}

void saveGpuPreference(GpuPreference preference)
{
    QSettings s = settings();
    s.setValue(QStringLiteral("gpu/preference"), toString(preference));
}

bool isHybridNvidiaSystem()
{
    if (!QFile::exists(QStringLiteral("/proc/driver/nvidia/version")))
        return false;
    bool nvidia = false, other = false;
    const QDir drm(QStringLiteral("/sys/class/drm"));
    for (const QString& card : drm.entryList({QStringLiteral("card[0-9]*")}, QDir::Dirs | QDir::System)) {
        if (card.contains(u'-'))
            continue; // connectors such as card1-eDP-1
        QFile vendor(drm.filePath(card + QStringLiteral("/device/vendor")));
        if (!vendor.open(QIODevice::ReadOnly))
            continue;
        const QByteArray id = vendor.readAll().trimmed();
        (id == "0x10de" ? nvidia : other) = true;
    }
    return nvidia && other;
}

GpuSetupResult applyGpuPreference(GpuPreference preference)
{
    GpuSetupResult result;
    const bool externallyOffloaded = qgetenv("__NV_PRIME_RENDER_OFFLOAD") == "1";
    const bool wantNvidia = preference == GpuPreference::Nvidia ||
                            (preference == GpuPreference::Automatic && isHybridNvidiaSystem());

    if (preference == GpuPreference::Integrated) {
        result.description = QStringLiteral("GPU preference: integrated");
    } else if (externallyOffloaded) {
        result.nvidiaOffload = true;
        result.description = QStringLiteral("NVIDIA render offload requested by the environment");
    } else if (wantNvidia && QFile::exists(QStringLiteral("/proc/driver/nvidia/version"))) {
        qputenv("__NV_PRIME_RENDER_OFFLOAD", "1");
        qputenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia");
        qputenv("__VK_LAYER_NV_optimus", "NVIDIA_only");
        result.nvidiaOffload = true;
        result.description = QStringLiteral("GPU preference %1: rendering on the NVIDIA GPU (PRIME offload)")
                                 .arg(toString(preference));
    } else {
        result.description = QStringLiteral("GPU preference %1: default GPU").arg(toString(preference));
    }

    if (result.nvidiaOffload && qgetenv("XDG_SESSION_TYPE") == "wayland" &&
        qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "xcb");
        result.forcedXcb = true;
        result.description += QStringLiteral("; using XWayland, which NVIDIA offload requires here");
    }
    return result;
}

} // namespace pf
