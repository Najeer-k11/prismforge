#pragma once

#include "texture/TextureChannel.h"

#include <QDialog>
#include <QImage>
#include <QWidget>

#include <array>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QToolButton;

namespace pf {

// Texture inputs uChannel0-3: a thumbnail per channel (click to choose a
// built-in texture or an image file) with filter, wrap and flip settings.
class ChannelsPanel final : public QWidget {
    Q_OBJECT
public:
    explicit ChannelsPanel(QWidget* parent = nullptr);

    // Shows a binding. `image` is the loaded picture (top row first) for the
    // thumbnail; `error` explains why it could not be loaded, if it couldn't.
    void setChannel(int index, const ChannelBinding& binding, const QImage& image, const QString& error);

    // Directory the file dialog starts in (the project directory).
    void setBrowseDirectory(const QString& directory) { m_browseDirectory = directory; }

signals:
    void channelChanged(int index, const pf::ChannelBinding& binding);

private:
    struct Slot {
        ChannelBinding binding;
        QToolButton* thumbnail = nullptr;
        QLabel* title = nullptr;
        QLabel* detail = nullptr;
        QComboBox* filter = nullptr;
        QComboBox* wrap = nullptr;
        QCheckBox* flipY = nullptr;
        QToolButton* clear = nullptr;
    };

    void choose(int index);
    void emitSampler(int index);

    std::array<Slot, kChannelCount> m_slots;
    QString m_browseDirectory;
};

// Grid of the built-in textures plus "Image File…"; used by ChannelsPanel.
class TexturePickerDialog final : public QDialog {
    Q_OBJECT
public:
    TexturePickerDialog(int channel, const ChannelBinding& current, const QString& browseDirectory,
                        QWidget* parent = nullptr);

    // Valid after exec() returned Accepted.
    ChannelBinding binding() const { return m_result; }

private:
    void acceptBuiltin();

    QListWidget* m_list = nullptr;
    ChannelBinding m_current;
    ChannelBinding m_result;
    QString m_browseDirectory;
};

} // namespace pf
