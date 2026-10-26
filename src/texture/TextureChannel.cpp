#include "texture/TextureChannel.h"

namespace pf {

QString toString(TextureFilter filter)
{
    switch (filter) {
    case TextureFilter::Nearest: return QStringLiteral("nearest");
    case TextureFilter::Linear: return QStringLiteral("linear");
    case TextureFilter::Mipmap: return QStringLiteral("mipmap");
    }
    return {};
}

QString toString(TextureWrap wrap)
{
    switch (wrap) {
    case TextureWrap::Repeat: return QStringLiteral("repeat");
    case TextureWrap::Clamp: return QStringLiteral("clamp");
    case TextureWrap::Mirror: return QStringLiteral("mirror");
    }
    return {};
}

std::optional<TextureFilter> textureFilterFromString(const QString& name)
{
    for (TextureFilter f : {TextureFilter::Nearest, TextureFilter::Linear, TextureFilter::Mipmap})
        if (toString(f) == name)
            return f;
    return std::nullopt;
}

std::optional<TextureWrap> textureWrapFromString(const QString& name)
{
    for (TextureWrap w : {TextureWrap::Repeat, TextureWrap::Clamp, TextureWrap::Mirror})
        if (toString(w) == name)
            return w;
    return std::nullopt;
}

QString channelUniformName(int index)
{
    return QStringLiteral("uChannel%1").arg(index);
}

QJsonObject channelsToJson(const ChannelBindings& channels)
{
    QJsonObject json;
    for (int i = 0; i < kChannelCount; ++i) {
        const ChannelBinding& c = channels[std::size_t(i)];
        if (c.isEmpty())
            continue;
        json.insert(QString::number(i), QJsonObject{
            {c.source == ChannelBinding::Source::Builtin ? "texture" : "file", c.texture},
            {"filter", toString(c.sampler.filter)},
            {"wrap", toString(c.sampler.wrap)},
            {"flipY", c.sampler.flipY},
        });
    }
    return json;
}

ChannelBindings channelsFromJson(const QJsonObject& json)
{
    ChannelBindings channels;
    const ChannelSampler defaults;
    for (auto it = json.begin(); it != json.end(); ++it) {
        bool ok = false;
        const int index = it.key().toInt(&ok);
        const QJsonObject o = it.value().toObject();
        if (!ok || index < 0 || index >= kChannelCount)
            continue;
        ChannelBinding c;
        if (const QString id = o.value("texture").toString(); !id.isEmpty()) {
            c.source = ChannelBinding::Source::Builtin;
            c.texture = id;
        } else if (const QString file = o.value("file").toString(); !file.isEmpty()) {
            c.source = ChannelBinding::Source::File;
            c.texture = file;
        } else {
            continue;
        }
        c.sampler.filter = textureFilterFromString(o.value("filter").toString()).value_or(defaults.filter);
        c.sampler.wrap = textureWrapFromString(o.value("wrap").toString()).value_or(defaults.wrap);
        c.sampler.flipY = o.value("flipY").toBool(defaults.flipY);
        channels[std::size_t(index)] = c;
    }
    return channels;
}

const QList<BuiltinTexture>& builtinTextures()
{
    using F = TextureFilter;
    using W = TextureWrap;
    // Noise is read texel by texel (often with textureLod), so it defaults to
    // plain bilinear filtering; pictures get mipmaps.
    static const QList<BuiltinTexture> textures = {
        {"rgba-noise-256", "RGBA Noise 256",
         "256×256 white noise in four channels. G is R shifted by (37, 17) texels and A is B "
         "shifted likewise, for fast 3D value noise from a single lookup.",
         {F::Linear, W::Repeat, true}},
        {"rgba-noise-64", "RGBA Noise 64", "64×64 version of RGBA Noise 256 (same channel offsets).",
         {F::Linear, W::Repeat, true}},
        {"gray-noise-256", "Gray Noise 256", "256×256 uniform white noise, one channel.",
         {F::Linear, W::Repeat, true}},
        {"blue-noise-128", "Blue Noise 128",
         "128×128 void-and-cluster blue noise for dithering and sample jitter. Read it with texelFetch.",
         {F::Nearest, W::Repeat, true}},
        {"fbm-noise-512", "Fractal Noise 512", "Smooth fractal (1/f²) noise; heightmaps, dirt, variation masks.",
         {F::Mipmap, W::Repeat, true}},
        {"clouds-512", "Clouds 512", "Soft cloud cover mask for skies and planets.", {F::Mipmap, W::Repeat, true}},
        {"cells-256", "Voronoi Cells 256",
         "Worley noise: R = distance to the nearest cell centre, G = cell border (F2 − F1), B = random value "
         "per cell.",
         {F::Mipmap, W::Repeat, true}},
        {"rock-512", "Rock 512", "Cracked grey-brown rock with lichen; mountains and cliffs.",
         {F::Mipmap, W::Repeat, true}},
        {"ground-512", "Ground 512", "Grass and dirt patches with pebbles.", {F::Mipmap, W::Repeat, true}},
        {"wood-512", "Wood 512", "Wood grain running along X.", {F::Mipmap, W::Repeat, true}},
        {"water-normal-512", "Water Normals 512",
         "Tangent-space ripple normal map (OpenGL convention, RGB = n × 0.5 + 0.5) for water surfaces.",
         {F::Mipmap, W::Repeat, true}},
        {"uv-grid-512", "UV Grid 512",
         "Labelled 8×8 test grid; A1 is the bottom-left cell. Checks UV layout and orientation.",
         {F::Mipmap, W::Repeat, true}},
    };
    return textures;
}

const BuiltinTexture* findBuiltinTexture(const QString& id)
{
    for (const BuiltinTexture& t : builtinTextures())
        if (t.id == id)
            return &t;
    return nullptr;
}

ChannelBinding builtinBinding(const QString& id)
{
    ChannelBinding binding;
    binding.source = ChannelBinding::Source::Builtin;
    binding.texture = id;
    if (const BuiltinTexture* t = findBuiltinTexture(id))
        binding.sampler = t->sampler;
    return binding;
}

} // namespace pf
