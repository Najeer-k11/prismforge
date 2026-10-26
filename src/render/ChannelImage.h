#pragma once

#include "texture/TextureChannel.h"

#include <QImage>

namespace pf {

// Loads the picture a channel is bound to (a built-in texture or an image
// file), converted to RGBA8888, top row first. An empty binding yields a null
// image and no error; a failure yields a null image and a reason.
QImage loadChannelImage(const ChannelBinding& binding, QString* error);

} // namespace pf
