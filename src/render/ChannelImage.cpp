#include "render/ChannelImage.h"

#include <QImageReader>

namespace pf {

QImage loadChannelImage(const ChannelBinding& binding, QString* error)
{
    QString path;
    switch (binding.source) {
    case ChannelBinding::Source::None:
        return {};
    case ChannelBinding::Source::Builtin:
        if (!findBuiltinTexture(binding.texture)) {
            if (error)
                *error = QStringLiteral("Unknown built-in texture '%1'").arg(binding.texture);
            return {};
        }
        path = findBuiltinTexture(binding.texture)->resourcePath();
        break;
    case ChannelBinding::Source::File:
        path = binding.texture;
        break;
    }

    QImageReader reader(path);
    reader.setAutoTransform(true); // honour EXIF orientation of photos
    QImage image = reader.read();
    if (image.isNull()) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(path, reader.errorString());
        return {};
    }
    return image.convertToFormat(QImage::Format_RGBA8888);
}

} // namespace pf
