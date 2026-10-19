#include "app/MaterialPanel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace pf {

namespace {

constexpr int kSliderSteps = 1000;

QColor toColor(const ParamValue& v, int components)
{
    auto c = [](float x) { return int(std::clamp(x, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return QColor(c(v[0]), c(v[1]), c(v[2]), components == 4 ? c(v[3]) : 255);
}

QString swatchStyle(const QColor& color)
{
    return QStringLiteral("QToolButton { background: %1; border: 1px solid #1a1b1e; min-width: 36px; }")
        .arg(color.name(QColor::HexRgb));
}

} // namespace

struct MaterialPanel::Row {
    MaterialParameter param;
    float sliderMin = 0.0f;
    float sliderMax = 1.0f;
    QList<QDoubleSpinBox*> spins;
    QSlider* slider = nullptr;
    QToolButton* swatch = nullptr;
    QCheckBox* check = nullptr;
    QSpinBox* intSpin = nullptr;
};

MaterialPanel::MaterialPanel(QWidget* parent) : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll);

    m_content = new QWidget(scroll);
    auto* contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(8, 6, 8, 6);
    m_grid = new QGridLayout();
    m_grid->setHorizontalSpacing(8);
    m_grid->setVerticalSpacing(4);
    m_grid->setColumnStretch(1, 1);
    contentLayout->addLayout(m_grid);
    m_empty = new QLabel(tr("No editable uniforms.\n\nDeclare e.g.\n"
                            "uniform float uRoughness = 0.5; // @range(0, 1)\n"
                            "uniform vec3 uTint = vec3(1.0); // @color"),
                         m_content);
    m_empty->setStyleSheet(QStringLiteral("color: #80848b"));
    contentLayout->addWidget(m_empty);
    contentLayout->addStretch();
    scroll->setWidget(m_content);
}

MaterialPanel::~MaterialPanel() = default;

void MaterialPanel::setParameters(const QList<MaterialParameter>& parameters)
{
    QString signature;
    for (const MaterialParameter& p : parameters)
        signature += QStringLiteral("%1:%2:%3:%4:%5:%6;")
                         .arg(p.name, toString(p.type))
                         .arg(p.hints.min.value_or(NAN))
                         .arg(p.hints.max.value_or(NAN))
                         .arg(p.hints.color)
                         .arg(p.hints.label);
    if (signature != m_signature) {
        m_signature = signature;
        rebuild(parameters);
        return;
    }
    for (int i = 0; i < parameters.size(); ++i) {
        Row& row = *m_rows[std::size_t(i)];
        row.param = parameters[i];
        refresh(row);
    }
}

void MaterialPanel::rebuild(const QList<MaterialParameter>& parameters)
{
    m_rows.clear();
    while (QLayoutItem* item = m_grid->takeAt(0)) {
        delete item->widget();
        delete item->layout();
        delete item;
    }
    m_empty->setVisible(parameters.isEmpty());
    for (int i = 0; i < parameters.size(); ++i)
        addRow(i, parameters[i]);
}

void MaterialPanel::addRow(int index, const MaterialParameter& parameter)
{
    auto row = std::make_unique<Row>();
    Row* r = row.get();
    r->param = parameter;

    auto* label = new QLabel(parameter.displayName(), m_content);
    label->setToolTip(QStringLiteral("%1 %2").arg(toString(parameter.type), parameter.name));
    m_grid->addWidget(label, index, 0);

    auto* editor = new QWidget(m_content);
    auto* h = new QHBoxLayout(editor);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(4);

    const int n = componentCount(parameter.type);
    switch (parameter.type) {
    case ParamType::Bool:
        r->check = new QCheckBox(editor);
        connect(r->check, &QCheckBox::toggled, this, [this, r](bool on) {
            r->param.value[0] = on ? 1.0f : 0.0f;
            commit(*r);
        });
        h->addWidget(r->check);
        h->addStretch();
        break;
    case ParamType::Int:
        r->intSpin = new QSpinBox(editor);
        r->intSpin->setRange(parameter.hints.min ? int(*parameter.hints.min) : -100000,
                             parameter.hints.max ? int(*parameter.hints.max) : 100000);
        connect(r->intSpin, &QSpinBox::valueChanged, this, [this, r](int v) {
            r->param.value[0] = float(v);
            commit(*r);
        });
        h->addWidget(r->intSpin, 1);
        break;
    default: {
        if (parameter.hints.color && n >= 3) {
            r->swatch = new QToolButton(editor);
            r->swatch->setToolTip(tr("Pick colour"));
            connect(r->swatch, &QToolButton::clicked, this, [this, r, n] {
                QColorDialog::ColorDialogOptions options;
                if (n == 4)
                    options |= QColorDialog::ShowAlphaChannel;
                const QColor picked = QColorDialog::getColor(toColor(r->param.value, n), this,
                                                             r->param.displayName(), options);
                if (!picked.isValid())
                    return;
                r->param.value[0] = float(picked.redF());
                r->param.value[1] = float(picked.greenF());
                r->param.value[2] = float(picked.blueF());
                if (n == 4)
                    r->param.value[3] = float(picked.alphaF());
                refresh(*r);
                commit(*r);
            });
            h->addWidget(r->swatch);
        }
        if (parameter.type == ParamType::Float) {
            r->sliderMin = parameter.hints.min.value_or(std::min(0.0f, parameter.defaultValue[0]));
            r->sliderMax = parameter.hints.max.value_or(
                std::max(1.0f, std::abs(parameter.defaultValue[0]) * 2.0f));
            r->slider = new QSlider(Qt::Horizontal, editor);
            r->slider->setRange(0, kSliderSteps);
            connect(r->slider, &QSlider::valueChanged, this, [this, r](int pos) {
                r->param.value[0] = r->sliderMin + (r->sliderMax - r->sliderMin) * float(pos) / kSliderSteps;
                refresh(*r);
                commit(*r);
            });
            h->addWidget(r->slider, 1);
        }
        for (int c = 0; c < n; ++c) {
            auto* spin = new QDoubleSpinBox(editor);
            spin->setDecimals(parameter.type == ParamType::Float ? 3 : 2);
            spin->setSingleStep(0.01);
            spin->setRange(parameter.hints.min.value_or(-1.0e6f), parameter.hints.max.value_or(1.0e6f));
            spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
            spin->setMinimumWidth(44);
            if (parameter.type == ParamType::Float)
                spin->setFixedWidth(64);
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this, r, c](double v) {
                r->param.value[std::size_t(c)] = float(v);
                refresh(*r);
                commit(*r);
            });
            r->spins.append(spin);
            h->addWidget(spin, parameter.type == ParamType::Float ? 0 : 1);
        }
        break;
    }
    }
    m_grid->addWidget(editor, index, 1);

    auto* reset = new QToolButton(m_content);
    reset->setText(QStringLiteral("↺"));
    reset->setToolTip(tr("Reset to the shader's default value"));
    connect(reset, &QToolButton::clicked, this, [this, r] {
        r->param.value = r->param.defaultValue;
        refresh(*r);
        emit parameterReset(r->param.name);
        emit parameterChanged(r->param);
    });
    m_grid->addWidget(reset, index, 2);

    refresh(*r);
    m_rows.push_back(std::move(row));
}

void MaterialPanel::refresh(Row& row)
{
    const ParamValue& v = row.param.value;
    if (row.check) {
        const QSignalBlocker block(row.check);
        row.check->setChecked(v[0] != 0.0f);
    }
    if (row.intSpin) {
        const QSignalBlocker block(row.intSpin);
        row.intSpin->setValue(int(std::lround(v[0])));
    }
    for (int c = 0; c < row.spins.size(); ++c) {
        QDoubleSpinBox* spin = row.spins[c];
        if (spin->hasFocus() && std::abs(spin->value() - v[std::size_t(c)]) < 1e-6)
            continue; // don't fight the user's typing
        const QSignalBlocker block(spin);
        spin->setValue(v[std::size_t(c)]);
    }
    if (row.slider) {
        const QSignalBlocker block(row.slider);
        const float t = (v[0] - row.sliderMin) / (row.sliderMax - row.sliderMin);
        row.slider->setValue(int(std::lround(std::clamp(t, 0.0f, 1.0f) * kSliderSteps)));
    }
    if (row.swatch)
        row.swatch->setStyleSheet(swatchStyle(toColor(v, componentCount(row.param.type))));
}

void MaterialPanel::commit(Row& row)
{
    emit parameterChanged(row.param);
}

} // namespace pf
