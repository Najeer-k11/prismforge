#include "render/ChannelImage.h"
#include "texture/TextureChannel.h"

#include <QFile>
#include <QJsonArray>
#include <QSet>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

using namespace pf;

TEST_CASE("Every built-in texture loads as a square power-of-two RGBA image", "[textures]")
{
    REQUIRE(builtinTextures().size() >= 12);
    QSet<QString> ids;
    for (const BuiltinTexture& t : builtinTextures()) {
        INFO(t.id.toStdString());
        CHECK_FALSE(ids.contains(t.id));
        ids.insert(t.id);
        CHECK_FALSE(t.name.isEmpty());
        CHECK_FALSE(t.description.isEmpty());
        CHECK(findBuiltinTexture(t.id) == &t);

        QString error;
        const QImage image = loadChannelImage(builtinBinding(t.id), &error);
        REQUIRE(error.isEmpty());
        REQUIRE_FALSE(image.isNull());
        CHECK(image.format() == QImage::Format_RGBA8888);
        CHECK(image.width() == image.height());
        CHECK((image.width() & (image.width() - 1)) == 0);
        // Ids end with the size, e.g. "rock-512".
        CHECK(t.id.section(u'-', -1).toInt() == image.width());
    }
}

TEST_CASE("RGBA noise channels are offset by (37, 17) texels", "[textures]")
{
    for (const char* id : {"rgba-noise-256", "rgba-noise-64"}) {
        INFO(id);
        QString error;
        const QImage image = loadChannelImage(builtinBinding(id), &error);
        REQUIRE_FALSE(image.isNull());
        const int n = image.width();
        REQUIRE(builtinBinding(id).sampler.flipY);
        // In GL texel coordinates (Flip Y: texel row y is image row n-1-y),
        // G(x, y) == R(x - 37, y - 17) and A(x, y) == B(x - 37, y - 17).
        auto texel = [&](int x, int y) {
            return image.pixelColor((x % n + n) % n, n - 1 - ((y % n + n) % n));
        };
        int mismatches = 0;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                const QColor here = texel(x, y), source = texel(x - 37, y - 17);
                mismatches += here.green() != source.red() || here.alpha() != source.blue();
            }
        CHECK(mismatches == 0);
    }
}

TEST_CASE("Channel images load from files and report failures", "[textures]")
{
    QString error;
    CHECK(loadChannelImage({}, &error).isNull());
    CHECK(error.isEmpty());

    ChannelBinding unknown = builtinBinding("no-such-texture");
    CHECK(loadChannelImage(unknown, &error).isNull());
    CHECK(error.contains("no-such-texture"));

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    ChannelBinding file;
    file.source = ChannelBinding::Source::File;
    file.texture = dir.filePath("missing.png");
    error.clear();
    CHECK(loadChannelImage(file, &error).isNull());
    CHECK(error.contains("missing.png"));

    QImage picture(3, 5, QImage::Format_RGB32);
    picture.fill(QColor(10, 20, 30));
    file.texture = dir.filePath("picture.png");
    REQUIRE(picture.save(file.texture));
    error.clear();
    const QImage loaded = loadChannelImage(file, &error);
    CHECK(error.isEmpty());
    CHECK(loaded.size() == QSize(3, 5));
    CHECK(loaded.format() == QImage::Format_RGBA8888);
    CHECK(loaded.pixelColor(1, 1) == QColor(10, 20, 30));
}

TEST_CASE("Channel bindings round-trip through JSON", "[textures][project]")
{
    ChannelBindings channels;
    channels[0] = builtinBinding("rgba-noise-256");
    channels[3].source = ChannelBinding::Source::File;
    channels[3].texture = "textures/bricks.png";
    channels[3].sampler = {TextureFilter::Nearest, TextureWrap::Mirror, false};

    const QJsonObject json = channelsToJson(channels);
    CHECK(json.keys() == QStringList{"0", "3"}); // unbound channels are not written
    CHECK(json["0"].toObject()["texture"] == "rgba-noise-256");
    CHECK(json["3"].toObject()["file"] == "textures/bricks.png");
    CHECK(channelsFromJson(json) == channels);
}

TEST_CASE("Malformed channel entries are skipped", "[textures][project]")
{
    const QJsonObject json{
        {"7", QJsonObject{{"texture", "rock-512"}}},              // no such channel
        {"x", QJsonObject{{"texture", "rock-512"}}},              // not an index
        {"1", QJsonObject{{"filter", "linear"}}},                 // no source
        {"2", QJsonArray{}},                                      // not an object
        {"3", QJsonObject{{"texture", "rock-512"}, {"filter", "cubic"}, {"wrap", 4}}},
    };
    const ChannelBindings channels = channelsFromJson(json);
    for (int i = 0; i < 3; ++i)
        CHECK(channels[std::size_t(i)].isEmpty());
    CHECK(channels[3].source == ChannelBinding::Source::Builtin);
    CHECK(channels[3].sampler == ChannelSampler{}); // unknown values fall back to defaults
}

TEST_CASE("Built-in bindings use the texture's recommended sampler", "[textures]")
{
    CHECK(builtinBinding("rgba-noise-256").sampler.filter == TextureFilter::Linear);
    CHECK(builtinBinding("blue-noise-128").sampler.filter == TextureFilter::Nearest);
    CHECK(builtinBinding("rock-512").sampler.filter == TextureFilter::Mipmap);
    CHECK(channelUniformName(2) == "uChannel2");
}
