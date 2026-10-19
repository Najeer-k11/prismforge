#include "grapheditor/NodePropertiesPanel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace pf::grapheditor {

using namespace pf::graph;

namespace {
QDoubleSpinBox* makeSpin(QWidget* parent, double value)
{
    auto* spin = new QDoubleSpinBox(parent);
    spin->setRange(-1.0e6, 1.0e6);
    spin->setDecimals(3);
    spin->setSingleStep(0.05);
    spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spin->setMinimumWidth(48);
    spin->setValue(value);
    return spin;
}

QString swatchStyle(const ParamValue& v)
{
    auto c = [](float x) { return int(std::clamp(x, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return QStringLiteral("QToolButton { background: %1; border: 1px solid #1a1b1e; min-width: 28px; }")
        .arg(QColor(c(v[0]), c(v[1]), c(v[2])).name());
}
} // namespace

NodePropertiesPanel::NodePropertiesPanel(QWidget* parent) : QWidget(parent)
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
    layout->setContentsMargins(10, 8, 10, 8);
    m_title = new QLabel(content);
    m_title->setStyleSheet(QStringLiteral("font-weight: 600"));
    m_description = new QLabel(content);
    m_description->setWordWrap(true);
    m_description->setStyleSheet(QStringLiteral("color: #80848b"));
    layout->addWidget(m_title);
    layout->addWidget(m_description);
    m_formHost = new QWidget(content);
    m_form = new QFormLayout(m_formHost);
    m_form->setContentsMargins(0, 6, 0, 0);
    m_form->setLabelAlignment(Qt::AlignLeft);
    layout->addWidget(m_formHost);
    layout->addStretch();
    scroll->setWidget(content);

    showNode(nullptr, nullptr, DataType::Float, {});
}

void NodePropertiesPanel::clearForm()
{
    while (m_form->rowCount() > 0)
        m_form->removeRow(0);
}

void NodePropertiesPanel::showNode(const GraphNode* node, const NodeDefinition* def, DataType genericType,
                                   const QSet<QString>& connectedInputs)
{
    clearForm();
    if (!node) {
        m_title->setText(tr("No node selected"));
        m_description->setText(tr("Select a single node to edit its inputs.\n\n"
                                  "Right-click the canvas to add nodes. Drag from a port to connect; "
                                  "drag a connected input away to disconnect."));
        return;
    }
    m_title->setText(def ? QStringLiteral("%1  ·  #%2").arg(def->title).arg(node->id) : node->definitionId);
    m_description->setText(def ? def->description : tr("Unknown node type. It is kept in the file but ignored."));
    m_description->setVisible(!m_description->text().isEmpty());
    if (!def)
        return;
    const int nodeId = node->id;

    if (def->role == NodeRole::Parameter) {
        auto* name = new QLineEdit(node->parameterName.isEmpty() ? QStringLiteral("uParam%1").arg(nodeId)
                                                                  : node->parameterName, m_formHost);
        name->setValidator(new QRegularExpressionValidator(
            QRegularExpression(QStringLiteral("[A-Za-z_][A-Za-z0-9_]*")), name));
        name->setToolTip(tr("Uniform name in the generated shader"));
        connect(name, &QLineEdit::editingFinished, this, [this, nodeId, name] {
            emit parameterNameEdited(nodeId, name->text());
        });
        m_form->addRow(tr("Uniform"), name);

        if (def->outputs[0].type.concrete == DataType::Float) {
            auto* rangeRow = new QWidget(m_formHost);
            auto* h = new QHBoxLayout(rangeRow);
            h->setContentsMargins(0, 0, 0, 0);
            auto* enabled = new QCheckBox(rangeRow);
            auto* lo = makeSpin(rangeRow, node->rangeMin.value_or(0.0f));
            auto* hi = makeSpin(rangeRow, node->rangeMax.value_or(1.0f));
            enabled->setChecked(node->rangeMin && node->rangeMax);
            lo->setEnabled(enabled->isChecked());
            hi->setEnabled(enabled->isChecked());
            h->addWidget(enabled);
            h->addWidget(lo, 1);
            h->addWidget(hi, 1);
            auto emitRange = [this, nodeId, enabled, lo, hi] {
                lo->setEnabled(enabled->isChecked());
                hi->setEnabled(enabled->isChecked());
                if (enabled->isChecked())
                    emit rangeEdited(nodeId, float(lo->value()), float(hi->value()));
                else
                    emit rangeEdited(nodeId, std::nullopt, std::nullopt);
            };
            connect(enabled, &QCheckBox::toggled, this, emitRange);
            connect(lo, &QDoubleSpinBox::editingFinished, this, emitRange);
            connect(hi, &QDoubleSpinBox::editingFinished, this, emitRange);
            m_form->addRow(tr("Slider range"), rangeRow);
        }
    }

    for (const InputDefinition& in : def->inputs) {
        if (connectedInputs.contains(in.id))
            continue;
        if (!in.defaultExpression.isEmpty()) {
            auto* note = new QLabel(tr("automatic (%1)").arg(in.defaultExpression), m_formHost);
            note->setStyleSheet(QStringLiteral("color: #80848b"));
            m_form->addRow(in.label, note);
            continue;
        }
        const DataType t = in.type.generic ? genericType : in.type.concrete;
        const int n = componentCount(t);
        auto value = std::make_shared<ParamValue>(node->inputValues.value(in.id, in.defaultValue));

        auto* row = new QWidget(m_formHost);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(4);
        QToolButton* swatch = nullptr;
        if (in.color && n >= 3) {
            swatch = new QToolButton(row);
            swatch->setStyleSheet(swatchStyle(*value));
            h->addWidget(swatch);
        }
        QList<QDoubleSpinBox*> spins;
        for (int c = 0; c < n; ++c) {
            auto* spin = makeSpin(row, (*value)[std::size_t(c)]);
            spins.append(spin);
            h->addWidget(spin, 1);
            connect(spin, &QDoubleSpinBox::valueChanged, this, [=, this](double v) {
                (*value)[std::size_t(c)] = float(v);
                if (swatch)
                    swatch->setStyleSheet(swatchStyle(*value));
                emit inputValueEdited(nodeId, in.id, *value);
            });
        }
        if (swatch) {
            connect(swatch, &QToolButton::clicked, this, [=, this] {
                auto c = [](float x) { return int(std::clamp(x, 0.0f, 1.0f) * 255.0f + 0.5f); };
                const QColor picked = QColorDialog::getColor(
                    QColor(c((*value)[0]), c((*value)[1]), c((*value)[2])), this, in.label);
                if (!picked.isValid())
                    return;
                const float rgb[3] = {float(picked.redF()), float(picked.greenF()), float(picked.blueF())};
                for (int i = 0; i < 3; ++i) {
                    const QSignalBlocker block(spins[i]);
                    spins[i]->setValue(rgb[i]);
                    (*value)[std::size_t(i)] = rgb[i];
                }
                swatch->setStyleSheet(swatchStyle(*value));
                emit inputValueEdited(nodeId, in.id, *value);
            });
        }
        QString label = in.label;
        if (def->role == NodeRole::Parameter && in.id == u"default")
            label = tr("Default value");
        m_form->addRow(label, row);
    }
}

} // namespace pf::grapheditor
