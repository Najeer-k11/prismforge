#pragma once

#include "media/VideoEncoder.h"

#include <QDialog>
#include <QSize>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace pf {

// Resolution chooser: presets, the current screen, or custom; optional portrait.
class ResolutionPicker final : public QWidget {
    Q_OBJECT
public:
    explicit ResolutionPicker(QWidget* parent = nullptr);
    QSize resolution() const;
    void setMaximum(QSize maximum); // the GPU's limit; larger choices are flagged
    void setResolution(QSize size);
    bool isValid() const;

signals:
    void changed();

private:
    void update();

    QComboBox* m_preset = nullptr;
    QSpinBox* m_width = nullptr;
    QSpinBox* m_height = nullptr;
    QCheckBox* m_portrait = nullptr;
    QLabel* m_note = nullptr;
    QSize m_maximum;
};

struct ImageRenderOptions {
    QString path;
    QSize size;
    int samples = 8;
    bool useCurrentTime = true;
    float time = 0.0f;
};

class RenderImageDialog final : public QDialog {
    Q_OBJECT
public:
    RenderImageDialog(QSize maxSize, int maxSamples, const QString& suggestedPath, QWidget* parent = nullptr);
    ImageRenderOptions options() const;

private:
    void validate();

    ResolutionPicker* m_resolution = nullptr;
    QComboBox* m_samples = nullptr;
    QCheckBox* m_currentTime = nullptr;
    QDoubleSpinBox* m_time = nullptr;
    QLineEdit* m_path = nullptr;
    QWidget* m_ok = nullptr;
};

struct VideoRenderOptions {
    media::VideoSettings video;
    int samples = 4;
    double duration = 10.0;
    double startTime = 0.0;
    bool turntable = false;
};

class RenderVideoDialog final : public QDialog {
    Q_OBJECT
public:
    RenderVideoDialog(QSize maxSize, int maxSamples, const QString& suggestedBase, QWidget* parent = nullptr);
    VideoRenderOptions options() const;

private:
    void validate();
    void formatChanged();
    const media::VideoFormatInfo* currentFormat() const;

    ResolutionPicker* m_resolution = nullptr;
    QComboBox* m_format = nullptr;
    QComboBox* m_fps = nullptr;
    QDoubleSpinBox* m_duration = nullptr;
    QDoubleSpinBox* m_start = nullptr;
    QComboBox* m_quality = nullptr;
    QComboBox* m_samples = nullptr;
    QCheckBox* m_turntable = nullptr;
    QLineEdit* m_path = nullptr;
    QLabel* m_summary = nullptr;
    QWidget* m_ok = nullptr;
};

} // namespace pf
