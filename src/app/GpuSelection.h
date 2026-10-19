#pragma once

#include <QString>

namespace pf {

// Which GPU renders the preview on hybrid (integrated + NVIDIA) laptops.
enum class GpuPreference { Automatic, Nvidia, Integrated };

QString toString(GpuPreference preference);
GpuPreference gpuPreferenceFromString(const QString& text);

GpuPreference savedGpuPreference();
void saveGpuPreference(GpuPreference preference);

// True when the machine has an NVIDIA GPU with the proprietary driver loaded
// alongside another GPU (PRIME render offload applies).
bool isHybridNvidiaSystem();

struct GpuSetupResult {
    bool nvidiaOffload = false; // PrismForge requested NVIDIA render offload
    bool forcedXcb = false;     // switched to XWayland for the offload to work
    QString description;        // for the log
};

// Must run before QApplication is created: sets the PRIME offload variables
// and, on Wayland, selects the XWayland platform, because NVIDIA offload via
// Wayland EGL fails on common hybrid setups. Respects variables the user or
// launcher already set, and an explicit QT_QPA_PLATFORM.
GpuSetupResult applyGpuPreference(GpuPreference preference);

} // namespace pf
