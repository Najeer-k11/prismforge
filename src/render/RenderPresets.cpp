#include "render/RenderPresets.h"

#include <QHash>
#include <QRegularExpression>

namespace pf {

const QList<ResolutionPreset>& resolutionPresets()
{
    static const QList<ResolutionPreset> presets = {
        {"720p", QStringLiteral("HD  1280 × 720"), {1280, 720}},
        {"1080p", QStringLiteral("Full HD  1920 × 1080"), {1920, 1080}},
        {"dci2k", QStringLiteral("DCI 2K (cinema)  2048 × 1080"), {2048, 1080}},
        {"1440p", QStringLiteral("QHD / 2K  2560 × 1440"), {2560, 1440}},
        {"ultrawide", QStringLiteral("Ultrawide QHD  3440 × 1440"), {3440, 1440}},
        {"4k", QStringLiteral("4K UHD  3840 × 2160"), {3840, 2160}},
        {"dci4k", QStringLiteral("DCI 4K (cinema)  4096 × 2160"), {4096, 2160}},
        {"5k", QStringLiteral("5K  5120 × 2880"), {5120, 2880}},
        {"8k", QStringLiteral("8K UHD  7680 × 4320"), {7680, 4320}},
    };
    return presets;
}

std::optional<QSize> parseResolution(const QString& text)
{
    const QString t = text.trimmed().toLower();
    static const QHash<QString, QString> aliases = {
        {"hd", "720p"},   {"fhd", "1080p"}, {"fullhd", "1080p"}, {"2k", "1440p"},  {"qhd", "1440p"},
        {"2160p", "4k"},  {"uhd", "4k"},    {"4320p", "8k"},     {"wqhd", "1440p"},
    };
    const QString key = aliases.value(t, t);
    for (const ResolutionPreset& p : resolutionPresets())
        if (p.key == key)
            return p.size;

    static const QRegularExpression re(QStringLiteral(R"(^(\d{1,5})\s*[x×]\s*(\d{1,5})$)"));
    const auto m = re.match(t);
    if (!m.hasMatch())
        return std::nullopt;
    const QSize size(m.captured(1).toInt(), m.captured(2).toInt());
    if (size.width() < 16 || size.height() < 16)
        return std::nullopt;
    return size;
}

QSize evenSize(QSize size)
{
    return {size.width() + (size.width() & 1), size.height() + (size.height() & 1)};
}

} // namespace pf
