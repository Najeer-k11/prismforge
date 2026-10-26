#include "app/ChannelsPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace pf {

namespace {

constexpr int kThumbnailSize = 72;
constexpr int kPickerIconSize = 112;

QPixmap thumbnail(const QImage& image, int size)
{
    return QPixmap::fromImage(image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

QString displayName(const ChannelBinding& binding)
{
    switch (binding.source) {
    case ChannelBinding::Source::None: return QObject::tr("No texture");
    case ChannelBinding::Source::Builtin:
        if (const BuiltinTexture* t = findBuiltinTexture(binding.texture))
            return t->name;
        return binding.texture;
    case ChannelBinding::Source::File: return QFileInfo(binding.texture).fileName();
    }
    return {};
}

QString imageFileFilter()
{
    QStringList patterns;
    for (const QByteArray& format : QImageReader::supportedImageFormats())
        patterns << QStringLiteral("*.") + QString::fromLatin1(format);
    return QObject::tr("Images (%1);;All files (*)").arg(patterns.join(u' '));
}

} // namespace

ChannelsPanel::ChannelsPanel(QWidget* parent) : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll);

    auto* content = new QWidget(scroll);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(10);

    for (int i = 0; i < kChannelCount; ++i) {
        Slot& slot = m_slots[std::size_t(i)];
        auto* grid = new QGridLayout();
        grid->setHorizontalSpacing(8);
        grid->setVerticalSpacing(2);
        grid->setColumnStretch(1, 1);

        slot.thumbnail = new QToolButton(content);
        slot.thumbnail->setFixedSize(kThumbnailSize + 6, kThumbnailSize + 6);
        slot.thumbnail->setIconSize(QSize(kThumbnailSize, kThumbnailSize));
        slot.thumbnail->setToolTip(tr("Choose the texture for %1").arg(channelUniformName(i)));
        connect(slot.thumbnail, &QToolButton::clicked, this, [this, i] { choose(i); });
        grid->addWidget(slot.thumbnail, 0, 0, 4, 1, Qt::AlignTop);

        slot.title = new QLabel(content);
        slot.title->setTextInteractionFlags(Qt::TextSelectableByMouse);
        grid->addWidget(slot.title, 0, 1);

        slot.clear = new QToolButton(content);
        slot.clear->setIcon(style()->standardIcon(QStyle::SP_DialogDiscardButton));
        slot.clear->setAutoRaise(true);
        slot.clear->setToolTip(tr("Remove the texture from %1").arg(channelUniformName(i)));
        connect(slot.clear, &QToolButton::clicked, this, [this, i] { emit channelChanged(i, ChannelBinding{}); });
        grid->addWidget(slot.clear, 0, 2, Qt::AlignRight);

        slot.detail = new QLabel(content);
        slot.detail->setWordWrap(true);
        grid->addWidget(slot.detail, 1, 1, 1, 2);

        auto* controls = new QHBoxLayout();
        controls->setSpacing(4);
        slot.filter = new QComboBox(content);
        slot.filter->addItem(tr("Mipmap"), int(TextureFilter::Mipmap));
        slot.filter->addItem(tr("Linear"), int(TextureFilter::Linear));
        slot.filter->addItem(tr("Nearest"), int(TextureFilter::Nearest));
        slot.filter->setToolTip(tr("Filtering: Mipmap (trilinear + anisotropic) for pictures seen at a distance, "
                                   "Linear for noise lookups, Nearest for exact texels"));
        slot.wrap = new QComboBox(content);
        slot.wrap->addItem(tr("Repeat"), int(TextureWrap::Repeat));
        slot.wrap->addItem(tr("Clamp"), int(TextureWrap::Clamp));
        slot.wrap->addItem(tr("Mirror"), int(TextureWrap::Mirror));
        slot.wrap->setToolTip(tr("What UVs outside [0, 1] read"));
        slot.flipY = new QCheckBox(tr("Flip Y"), content);
        slot.flipY->setToolTip(tr("Put the picture's bottom row at v = 0, so it appears upright with "
                                  "gl_FragCoord / vUV coordinates (Shadertoy's default)"));
        for (QComboBox* box : {slot.filter, slot.wrap})
            connect(box, &QComboBox::activated, this, [this, i] { emitSampler(i); });
        connect(slot.flipY, &QCheckBox::toggled, this, [this, i] { emitSampler(i); });
        controls->addWidget(slot.filter);
        controls->addWidget(slot.wrap);
        controls->addWidget(slot.flipY);
        controls->addStretch();
        grid->addLayout(controls, 2, 1, 1, 2);
        grid->setRowStretch(3, 1);

        layout->addLayout(grid);
        setChannel(i, {}, {}, {});
    }

    auto* hint = new QLabel(tr("Shaders read channels through <code>uniform sampler2D uChannel0;</code> … "
                               "<code>uChannel3</code>; texture sizes are in "
                               "<code>uniform vec3 uChannelResolution[4];</code>"),
                            content);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #80848b"));
    layout->addWidget(hint);
    layout->addStretch();
    scroll->setWidget(content);
}

void ChannelsPanel::setChannel(int index, const ChannelBinding& binding, const QImage& image, const QString& error)
{
    if (index < 0 || index >= kChannelCount)
        return;
    Slot& slot = m_slots[std::size_t(index)];
    slot.binding = binding;
    const bool bound = !binding.isEmpty();

    slot.title->setText(QStringLiteral("<b>%1</b> &nbsp;%2")
                            .arg(channelUniformName(index), displayName(binding).toHtmlEscaped()));
    QString detail;
    QString tooltip;
    if (!error.isEmpty()) {
        detail = QStringLiteral("<span style='color:#d8a64a'>%1</span>").arg(error.toHtmlEscaped());
    } else if (bound) {
        detail = QStringLiteral("%1 × %2 · %3")
                     .arg(image.width())
                     .arg(image.height())
                     .arg(binding.source == ChannelBinding::Source::Builtin ? tr("built-in") : tr("image file"));
        if (binding.source == ChannelBinding::Source::Builtin) {
            if (const BuiltinTexture* t = findBuiltinTexture(binding.texture))
                tooltip = t->description;
        } else {
            tooltip = binding.texture;
        }
    } else {
        detail = tr("Unbound: reads black");
    }
    slot.detail->setText(QStringLiteral("<span style='color:#80848b'>%1</span>").arg(detail));
    slot.detail->setToolTip(tooltip);
    slot.title->setToolTip(tooltip);

    if (!image.isNull()) {
        slot.thumbnail->setIcon(thumbnail(image, kThumbnailSize));
        slot.thumbnail->setText({});
        slot.thumbnail->setToolButtonStyle(Qt::ToolButtonIconOnly);
    } else {
        slot.thumbnail->setIcon({});
        slot.thumbnail->setText(bound ? tr("Missing") : tr("Choose…"));
        slot.thumbnail->setToolButtonStyle(Qt::ToolButtonTextOnly);
    }

    const QSignalBlocker b1(slot.filter), b2(slot.wrap), b3(slot.flipY);
    slot.filter->setCurrentIndex(slot.filter->findData(int(binding.sampler.filter)));
    slot.wrap->setCurrentIndex(slot.wrap->findData(int(binding.sampler.wrap)));
    slot.flipY->setChecked(binding.sampler.flipY);
    for (QWidget* w : std::initializer_list<QWidget*>{slot.filter, slot.wrap, slot.flipY, slot.clear})
        w->setEnabled(bound);
}

void ChannelsPanel::choose(int index)
{
    TexturePickerDialog dialog(index, m_slots[std::size_t(index)].binding, m_browseDirectory, this);
    if (dialog.exec() == QDialog::Accepted)
        emit channelChanged(index, dialog.binding());
}

void ChannelsPanel::emitSampler(int index)
{
    Slot& slot = m_slots[std::size_t(index)];
    if (slot.binding.isEmpty())
        return;
    ChannelBinding binding = slot.binding;
    binding.sampler.filter = TextureFilter(slot.filter->currentData().toInt());
    binding.sampler.wrap = TextureWrap(slot.wrap->currentData().toInt());
    binding.sampler.flipY = slot.flipY->isChecked();
    emit channelChanged(index, binding);
}

// ---------------------------------------------------------------- picker

TexturePickerDialog::TexturePickerDialog(int channel, const ChannelBinding& current, const QString& browseDirectory,
                                         QWidget* parent)
    : QDialog(parent), m_current(current), m_browseDirectory(browseDirectory)
{
    setWindowTitle(tr("Texture for %1").arg(channelUniformName(channel)));
    resize(720, 560);
    auto* layout = new QVBoxLayout(this);

    m_list = new QListWidget(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(kPickerIconSize, kPickerIconSize));
    m_list->setGridSize(QSize(kPickerIconSize + 64, kPickerIconSize + 40));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setWordWrap(true);
    m_list->setUniformItemSizes(true);
    for (const BuiltinTexture& t : builtinTextures()) {
        const QImage image(t.resourcePath());
        auto* item = new QListWidgetItem(QIcon(thumbnail(image, kPickerIconSize)), t.name, m_list);
        item->setData(Qt::UserRole, t.id);
        // Full cell width for the label (it would otherwise be elided to the icon's width).
        item->setSizeHint(m_list->gridSize() - QSize(8, 4));
        item->setToolTip(QStringLiteral("<b>%1</b> (%2 × %3)<br>%4")
                             .arg(t.name.toHtmlEscaped())
                             .arg(image.width())
                             .arg(image.height())
                             .arg(t.description.toHtmlEscaped()));
        if (current.source == ChannelBinding::Source::Builtin && current.texture == t.id)
            m_list->setCurrentItem(item);
    }
    connect(m_list, &QListWidget::itemActivated, this, &TexturePickerDialog::acceptBuiltin);
    layout->addWidget(m_list);

    auto* description = new QLabel(this);
    description->setWordWrap(true);
    description->setMinimumHeight(40);
    description->setStyleSheet(QStringLiteral("color: #80848b"));
    connect(m_list, &QListWidget::currentItemChanged, this, [description](QListWidgetItem* item) {
        const BuiltinTexture* t = item ? findBuiltinTexture(item->data(Qt::UserRole).toString()) : nullptr;
        description->setText(t ? t->description : QString());
    });
    if (QListWidgetItem* item = m_list->currentItem())
        description->setText(findBuiltinTexture(item->data(Qt::UserRole).toString())->description);
    layout->addWidget(description);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton* fromFile = buttons->addButton(tr("Image File…"), QDialogButtonBox::ActionRole);
    connect(fromFile, &QPushButton::clicked, this, [this] {
        const QString start = m_current.source == ChannelBinding::Source::File
                                  ? QFileInfo(m_current.texture).absolutePath()
                                  : m_browseDirectory;
        const QString path = QFileDialog::getOpenFileName(this, tr("Open Texture Image"), start, imageFileFilter());
        if (path.isEmpty())
            return;
        m_result = ChannelBinding{};
        m_result.source = ChannelBinding::Source::File;
        m_result.texture = QFileInfo(path).absoluteFilePath();
        accept();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &TexturePickerDialog::acceptBuiltin);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void TexturePickerDialog::acceptBuiltin()
{
    QListWidgetItem* item = m_list->currentItem();
    if (!item)
        return;
    const QString id = item->data(Qt::UserRole).toString();
    // Re-picking the current texture keeps the user's sampler settings.
    m_result = m_current.source == ChannelBinding::Source::Builtin && m_current.texture == id ? m_current
                                                                                               : builtinBinding(id);
    accept();
}

} // namespace pf
