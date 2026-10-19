#pragma once

#include "graph/NodeRegistry.h"
#include "graph/ShaderGraph.h"

#include <QSet>
#include <QWidget>

class QFormLayout;
class QLabel;

namespace pf::grapheditor {

// Edits the selected node: unconnected input values and parameter settings.
class NodePropertiesPanel final : public QWidget {
    Q_OBJECT
public:
    explicit NodePropertiesPanel(QWidget* parent = nullptr);

    // `node` may be null (nothing or several nodes selected).
    void showNode(const graph::GraphNode* node, const graph::NodeDefinition* definition,
                  graph::DataType genericType, const QSet<QString>& connectedInputs);

signals:
    void inputValueEdited(int nodeId, const QString& inputId, const pf::ParamValue& value);
    void parameterNameEdited(int nodeId, const QString& name);
    void rangeEdited(int nodeId, std::optional<float> min, std::optional<float> max);

private:
    void clearForm();

    QLabel* m_title = nullptr;
    QLabel* m_description = nullptr;
    QWidget* m_formHost = nullptr;
    QFormLayout* m_form = nullptr;
};

} // namespace pf::grapheditor
