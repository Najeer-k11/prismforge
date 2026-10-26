#pragma once

#include <QList>
#include <QSize>
#include <QString>

#include <optional>

namespace pf {

struct ResolutionPreset {
    QString key;   // short name usable on the command line, e.g. "4k"
    QString label; // shown in dialogs
    QSize size;
};

// Common output sizes, smallest first. All dimensions are even, as required
// by 4:2:0 video encoding.
const QList<ResolutionPreset>& resolutionPresets();

// Accepts "WIDTHxHEIGHT" (also with '×' or 'X') or a preset key/alias such
// as "1080p", "2k", "qhd", "4k", "uhd", "8k".
std::optional<QSize> parseResolution(const QString& text);

// Video encoders need even dimensions; rounds up to the next even number.
QSize evenSize(QSize size);

} // namespace pf
