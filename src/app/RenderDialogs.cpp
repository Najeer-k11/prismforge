#include "app/RenderDialogs.h"

#include "render/RenderPresets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSpinBox>
#include <QToolButton>

#include <algorithm>
#include <cmath>

namespace pf {

namespace {

constexpr int kCustom = -1;
constexpr int kScreen = -2;

QComboBox* makeSamplesCombo(QWidget* parent, int maxSamples, int preferred)
{
    auto* combo = new QComboBox(parent);
    combo->addItem(QObject::tr("Off"), 0);
    for (int s : {2, 4, 8, 16})
        if (s <= maxSamples)
            combo->addItem(QObject::tr("%1× MSAA").arg(s), s);
    combo->setCurrentIndex(std::max(0, combo->findData(std::min(preferred, maxSamples))));
    return combo;
}

QWidget* pathRow(QWidget* parent, QLineEdit* edit, const std::function<void()>& browse)
{
    auto* row = new QWidget(parent);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(edit, 1);
    auto* button = new QToolButton(row);
    button->setText(QObject::tr("Browse…"));
    QObject::connect(button, &QToolButton::clicked, parent, browse);
    h->addWidget(button);
    return row;
}

QString settingsKey(const char* key) { return QStringLiteral("render/%1").arg(QLatin1StringView(key)); }

} // namespace

// ---------------------------------------------------------------- ResolutionPicker

ResolutionPicker::ResolutionPicker(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    m_preset = new QComboBox(this);
    for (int i = 0; i < resolutionPresets().size(); ++i)
        m_preset->addItem(resolutionPresets()[i].label, i);
    if (const QScreen* screen = QGuiApplication::primaryScreen()) {
        const QSize native = evenSize(screen->size() * screen->devicePixelRatio());
        m_preset->addItem(tr("This screen  %1 × %2").arg(native.width()).arg(native.height()), kScreen);
    }
    m_preset->addItem(tr("Custom…"), kCustom);
    layout->addWidget(m_preset);

    auto* row = new QWidget(this);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    m_width = new QSpinBox(row);
    m_height = new QSpinBox(row);
    for (QSpinBox* s : {m_width, m_height}) {
        s->setRange(16, 16384);
        s->setSingleStep(2);
        s->setSuffix(tr(" px"));
    }
    m_portrait = new QCheckBox(tr("Portrait"), row);
    m_portrait->setToolTip(tr("Swap width and height (phone wallpapers)"));
    h->addWidget(m_width, 1);
    h->addWidget(new QLabel(QStringLiteral("×"), row));
    h->addWidget(m_height, 1);
    h->addWidget(m_portrait);
    layout->addWidget(row);

    m_note = new QLabel(this);
    m_note->setWordWrap(true);
    m_note->setStyleSheet(QStringLiteral("color: #80848b"));
    layout->addWidget(m_note);

    connect(m_preset, &QComboBox::currentIndexChanged, this, &ResolutionPicker::update);
    connect(m_portrait, &QCheckBox::toggled, this, &ResolutionPicker::update);
    connect(m_width, &QSpinBox::valueChanged, this, &ResolutionPicker::update);
    connect(m_height, &QSpinBox::valueChanged, this, &ResolutionPicker::update);
    setResolution({3840, 2160});
}

void ResolutionPicker::setResolution(QSize size)
{
    const bool portrait = size.height() > size.width();
    const QSize landscape = portrait ? size.transposed() : size;
    int index = m_preset->findData(kCustom);
    for (int i = 0; i < resolutionPresets().size(); ++i)
        if (resolutionPresets()[i].size == landscape)
            index = m_preset->findData(i);
    const QSignalBlocker b1(m_preset), b2(m_portrait), b3(m_width), b4(m_height);
    m_preset->setCurrentIndex(index);
    m_portrait->setChecked(portrait && m_preset->currentData().toInt() != kCustom);
    m_width->setValue(size.width());
    m_height->setValue(size.height());
    update();
}

void ResolutionPicker::setMaximum(QSize maximum)
{
    m_maximum = maximum;
    update();
}

QSize ResolutionPicker::resolution() const
{
    return {m_width->value(), m_height->value()};
}

bool ResolutionPicker::isValid() const
{
    const QSize s = resolution();
    return m_maximum.isEmpty() || (s.width() <= m_maximum.width() && s.height() <= m_maximum.height());
}

void ResolutionPicker::update()
{
    const int choice = m_preset->currentData().toInt();
    const bool custom = choice == kCustom;
    m_width->setEnabled(custom);
    m_height->setEnabled(custom);
    m_portrait->setEnabled(!custom);
    if (!custom) {
        QSize size;
        if (choice == kScreen) {
            const QScreen* screen = QGuiApplication::primaryScreen();
            size = evenSize(screen->size() * screen->devicePixelRatio());
        } else {
            size = resolutionPresets()[choice].size;
        }
        if (m_portrait->isChecked() != (size.height() > size.width()))
            size.transpose();
        const QSignalBlocker b1(m_width), b2(m_height);
        m_width->setValue(size.width());
        m_height->setValue(size.height());
    }
    const QSize s = resolution();
    const double megapixels = double(s.width()) * s.height() / 1.0e6;
    if (!isValid())
        m_note->setText(tr("<span style='color:#e06c6c'>Exceeds this GPU's limit of %1 × %2.</span>")
                            .arg(m_maximum.width()).arg(m_maximum.height()));
    else
        m_note->setText(tr("%1 megapixels").arg(megapixels, 0, 'f', 1));
    emit changed();
}

// ---------------------------------------------------------------- RenderImageDialog

RenderImageDialog::RenderImageDialog(QSize maxSize, int maxSamples, const QString& suggestedPath, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Render Image"));
    QSettings settings;
    auto* form = new QFormLayout(this);

    m_resolution = new ResolutionPicker(this);
    m_resolution->setMaximum(maxSize);
    m_resolution->setResolution(settings.value(settingsKey("imageSize"), QSize(3840, 2160)).toSize());
    form->addRow(tr("Resolution"), m_resolution);

    m_samples = makeSamplesCombo(this, maxSamples, settings.value(settingsKey("imageSamples"), 8).toInt());
    form->addRow(tr("Antialiasing"), m_samples);

    auto* timeRow = new QWidget(this);
    auto* th = new QHBoxLayout(timeRow);
    th->setContentsMargins(0, 0, 0, 0);
    m_currentTime = new QCheckBox(tr("Current preview time"), timeRow);
    m_currentTime->setChecked(true);
    m_time = new QDoubleSpinBox(timeRow);
    m_time->setRange(0, 100000);
    m_time->setSuffix(tr(" s"));
    m_time->setEnabled(false);
    th->addWidget(m_currentTime);
    th->addWidget(m_time, 1);
    connect(m_currentTime, &QCheckBox::toggled, m_time, [this](bool on) { m_time->setEnabled(!on); });
    form->addRow(tr("uTime"), timeRow);

    m_path = new QLineEdit(suggestedPath, this);
    form->addRow(tr("File"), pathRow(this, m_path, [this] {
                     const QString path = QFileDialog::getSaveFileName(
                         this, tr("Render Image"), m_path->text(),
                         tr("PNG image (*.png);;JPEG image (*.jpg *.jpeg);;WebP image (*.webp)"));
                     if (!path.isEmpty())
                         m_path->setText(QFileInfo(path).suffix().isEmpty() ? path + QStringLiteral(".png") : path);
                 }));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Render"));
    m_ok = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        QSettings s;
        s.setValue(settingsKey("imageSize"), m_resolution->resolution());
        s.setValue(settingsKey("imageSamples"), m_samples->currentData());
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);

    connect(m_resolution, &ResolutionPicker::changed, this, &RenderImageDialog::validate);
    connect(m_path, &QLineEdit::textChanged, this, &RenderImageDialog::validate);
    validate();
}

void RenderImageDialog::validate()
{
    m_ok->setEnabled(m_resolution->isValid() && !m_path->text().trimmed().isEmpty());
}

ImageRenderOptions RenderImageDialog::options() const
{
    ImageRenderOptions o;
    o.path = m_path->text().trimmed();
    o.size = m_resolution->resolution();
    o.samples = m_samples->currentData().toInt();
    o.useCurrentTime = m_currentTime->isChecked();
    o.time = float(m_time->value());
    return o;
}

// ---------------------------------------------------------------- RenderVideoDialog

RenderVideoDialog::RenderVideoDialog(QSize maxSize, int maxSamples, const QString& suggestedBase, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Render Video"));
    QSettings settings;
    auto* form = new QFormLayout(this);

    m_resolution = new ResolutionPicker(this);
    m_resolution->setMaximum(maxSize);
    m_resolution->setResolution(settings.value(settingsKey("videoSize"), QSize(1920, 1080)).toSize());
    form->addRow(tr("Resolution"), m_resolution);

    m_format = new QComboBox(this);
    for (const media::VideoFormatInfo& f : media::videoFormats()) {
        m_format->addItem(f.available ? f.label : tr("%1 — unavailable").arg(f.label), f.key);
        if (!f.available) {
            const int i = m_format->count() - 1;
            m_format->setItemData(i, f.reason, Qt::ToolTipRole);
            m_format->setItemData(i, 0, Qt::UserRole - 1); // disables the item
        }
    }
    const int savedFormat = m_format->findData(settings.value(settingsKey("videoFormat"), "h264"));
    for (int i : {savedFormat, 0, 1, 2, 3, 4, 5}) {
        const media::VideoFormatInfo* f = i >= 0 ? media::findVideoFormat(m_format->itemData(i).toString()) : nullptr;
        if (f && f->available) {
            m_format->setCurrentIndex(i);
            break;
        }
    }
    form->addRow(tr("Format"), m_format);

    m_fps = new QComboBox(this);
    for (int fps : {24, 25, 30, 50, 60})
        m_fps->addItem(tr("%1 fps").arg(fps), fps);
    m_fps->setCurrentIndex(std::max(0, m_fps->findData(settings.value(settingsKey("videoFps"), 30))));
    form->addRow(tr("Frame rate"), m_fps);

    m_duration = new QDoubleSpinBox(this);
    m_duration->setRange(0.1, 3600);
    m_duration->setDecimals(1);
    m_duration->setSuffix(tr(" s"));
    m_duration->setValue(settings.value(settingsKey("videoDuration"), 10.0).toDouble());
    form->addRow(tr("Duration"), m_duration);

    m_start = new QDoubleSpinBox(this);
    m_start->setRange(0, 100000);
    m_start->setSuffix(tr(" s"));
    m_start->setToolTip(tr("uTime of the first frame"));
    form->addRow(tr("Start time"), m_start);

    m_quality = new QComboBox(this);
    m_quality->addItem(tr("High"), int(media::VideoQuality::High));
    m_quality->addItem(tr("Medium"), int(media::VideoQuality::Medium));
    m_quality->addItem(tr("Small file"), int(media::VideoQuality::Small));
    form->addRow(tr("Quality"), m_quality);

    m_samples = makeSamplesCombo(this, maxSamples, 4);
    form->addRow(tr("Antialiasing"), m_samples);

    m_turntable = new QCheckBox(tr("Orbit the camera 360° over the clip (loops seamlessly)"), this);
    form->addRow(tr("Camera"), m_turntable);

    m_path = new QLineEdit(suggestedBase, this);
    form->addRow(tr("Output"), pathRow(this, m_path, [this] {
                     const media::VideoFormatInfo* f = currentFormat();
                     QString path;
                     if (f && f->format == media::VideoFormat::PngSequence)
                         path = QFileDialog::getExistingDirectory(this, tr("Folder for PNG frames"), m_path->text());
                     else
                         path = QFileDialog::getSaveFileName(this, tr("Render Video"), m_path->text(),
                                                             tr("Video (*.%1)").arg(f ? f->extension : QString()));
                     if (!path.isEmpty())
                         m_path->setText(path);
                 }));

    m_summary = new QLabel(this);
    m_summary->setStyleSheet(QStringLiteral("color: #80848b"));
    form->addRow(QString(), m_summary);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Render"));
    m_ok = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        QSettings s;
        s.setValue(settingsKey("videoSize"), m_resolution->resolution());
        s.setValue(settingsKey("videoFormat"), m_format->currentData());
        s.setValue(settingsKey("videoFps"), m_fps->currentData());
        s.setValue(settingsKey("videoDuration"), m_duration->value());
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);

    connect(m_resolution, &ResolutionPicker::changed, this, &RenderVideoDialog::validate);
    connect(m_format, &QComboBox::currentIndexChanged, this, &RenderVideoDialog::formatChanged);
    connect(m_fps, &QComboBox::currentIndexChanged, this, &RenderVideoDialog::validate);
    connect(m_duration, &QDoubleSpinBox::valueChanged, this, &RenderVideoDialog::validate);
    connect(m_path, &QLineEdit::textChanged, this, &RenderVideoDialog::validate);
    formatChanged();
}

const media::VideoFormatInfo* RenderVideoDialog::currentFormat() const
{
    return media::findVideoFormat(m_format->currentData().toString());
}

void RenderVideoDialog::formatChanged()
{
    // Keep the file name, switch the extension to the format's container.
    const media::VideoFormatInfo* f = currentFormat();
    if (f) {
        const QFileInfo info(m_path->text());
        const QString base = info.path() + QLatin1Char('/') + info.completeBaseName();
        m_path->setText(f->extension.isEmpty() ? base + QStringLiteral("_frames")
                                               : base.endsWith(QStringLiteral("_frames"))
                                                     ? base.chopped(7) + QLatin1Char('.') + f->extension
                                                     : base + QLatin1Char('.') + f->extension);
    }
    validate();
}

void RenderVideoDialog::validate()
{
    const media::VideoFormatInfo* f = currentFormat();
    const int frames = int(std::lround(m_duration->value() * m_fps->currentData().toInt()));
    m_summary->setText(tr("%n frame(s)", nullptr, frames));
    m_ok->setEnabled(f && f->available && m_resolution->isValid() && !m_path->text().trimmed().isEmpty());
}

VideoRenderOptions RenderVideoDialog::options() const
{
    VideoRenderOptions o;
    o.video.path = m_path->text().trimmed();
    o.video.format = currentFormat()->format;
    o.video.encoderKey = currentFormat()->key;
    o.video.size = evenSize(m_resolution->resolution());
    o.video.fps = m_fps->currentData().toInt();
    o.video.quality = media::VideoQuality(m_quality->currentData().toInt());
    o.samples = m_samples->currentData().toInt();
    o.duration = m_duration->value();
    o.startTime = m_start->value();
    o.turntable = m_turntable->isChecked();
    return o;
}

} // namespace pf
