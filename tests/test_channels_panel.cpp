// Channels panel and texture picker on the offscreen Qt platform.

#include "app/ChannelsPanel.h"
#include "render/ChannelImage.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QTimer>
#include <QToolButton>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace pf;

namespace {

ChannelBinding lastBinding(const QSignalSpy& spy)
{
    return spy.last().at(1).value<ChannelBinding>();
}

QListWidgetItem* pickerItem(QListWidget* list, const QString& id)
{
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(Qt::UserRole).toString() == id)
            return list->item(i);
    return nullptr;
}

} // namespace

TEST_CASE("The panel shows bindings and edits their sampler settings", "[channels][widgets]")
{
    ChannelsPanel panel;
    QSignalSpy spy(&panel, &ChannelsPanel::channelChanged);

    const QList<QComboBox*> combos = panel.findChildren<QComboBox*>();
    const QList<QCheckBox*> flips = panel.findChildren<QCheckBox*>();
    REQUIRE(combos.size() == 2 * kChannelCount); // filter, wrap per channel
    REQUIRE(flips.size() == kChannelCount);
    QComboBox* filter1 = combos[2];
    QComboBox* wrap1 = combos[3];
    CHECK_FALSE(filter1->isEnabled()); // nothing bound yet

    const ChannelBinding rock = builtinBinding("rock-512");
    QString error;
    panel.setChannel(1, rock, loadChannelImage(rock, &error), {});
    CHECK(spy.isEmpty()); // showing a binding is not an edit
    CHECK(filter1->isEnabled());
    CHECK(filter1->currentData().toInt() == int(TextureFilter::Mipmap));
    bool shown = false;
    for (QLabel* label : panel.findChildren<QLabel*>())
        shown |= label->text().contains("Rock 512") || label->text().contains("512 × 512");
    CHECK(shown);

    wrap1->setCurrentIndex(wrap1->findData(int(TextureWrap::Mirror)));
    emit wrap1->activated(wrap1->currentIndex());
    REQUIRE(spy.size() == 1);
    CHECK(spy.last().at(0).toInt() == 1);
    ChannelBinding edited = lastBinding(spy);
    CHECK(edited.texture == "rock-512");
    CHECK(edited.sampler.wrap == TextureWrap::Mirror);

    flips[1]->setChecked(false);
    REQUIRE(spy.size() == 2);
    CHECK_FALSE(lastBinding(spy).sampler.flipY);
}

TEST_CASE("Clearing a channel emits an empty binding", "[channels][widgets]")
{
    ChannelsPanel panel;
    QSignalSpy spy(&panel, &ChannelsPanel::channelChanged);
    panel.setChannel(2, builtinBinding("wood-512"), QImage(4, 4, QImage::Format_RGBA8888), {});
    QToolButton* clear = nullptr;
    for (QToolButton* b : panel.findChildren<QToolButton*>())
        if (b->toolTip().contains("Remove") && b->toolTip().contains("uChannel2"))
            clear = b;
    REQUIRE(clear);
    clear->click();
    REQUIRE(spy.size() == 1);
    CHECK(spy.last().at(0).toInt() == 2);
    CHECK(lastBinding(spy).isEmpty());
}

TEST_CASE("Load errors are shown in the panel", "[channels][widgets]")
{
    ChannelsPanel panel;
    ChannelBinding missing;
    missing.source = ChannelBinding::Source::File;
    missing.texture = "/nowhere/gone.png";
    panel.setChannel(0, missing, {}, "gone.png: file not found");
    bool shown = false;
    for (QLabel* label : panel.findChildren<QLabel*>())
        shown |= label->text().contains("file not found");
    CHECK(shown);
}

TEST_CASE("The picker lists every built-in texture and returns the choice", "[channels][widgets]")
{
    TexturePickerDialog dialog(0, {}, QString());
    auto* list = dialog.findChild<QListWidget*>();
    REQUIRE(list);
    CHECK(list->count() == builtinTextures().size());
    for (int i = 0; i < list->count(); ++i)
        CHECK_FALSE(list->item(i)->icon().isNull());

    list->setCurrentItem(pickerItem(list, "water-normal-512"));
    dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    CHECK(dialog.result() == QDialog::Accepted);
    CHECK(dialog.binding() == builtinBinding("water-normal-512"));
}

TEST_CASE("Re-picking the current texture keeps its settings", "[channels][widgets]")
{
    ChannelBinding current = builtinBinding("rgba-noise-256");
    current.sampler = {TextureFilter::Nearest, TextureWrap::Clamp, false};
    TexturePickerDialog dialog(3, current, QString());
    auto* list = dialog.findChild<QListWidget*>();
    REQUIRE(list->currentItem());
    CHECK(list->currentItem()->data(Qt::UserRole).toString() == "rgba-noise-256");
    dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    CHECK(dialog.binding() == current);
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    qRegisterMetaType<ChannelBinding>();
    return Catch::Session().run(argc, argv);
}
