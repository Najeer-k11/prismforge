#pragma once

#include "material/MaterialParameter.h"

#include <QWidget>

#include <memory>
#include <vector>

class QGridLayout;
class QLabel;

namespace pf {

// Editable controls for the active program's material parameters.
class MaterialPanel final : public QWidget {
    Q_OBJECT
public:
    explicit MaterialPanel(QWidget* parent = nullptr);
    ~MaterialPanel() override;

    // Rebuilds the controls only when the set of parameters (names, types,
    // hints) changed; otherwise just refreshes values so focus is kept while
    // the shader is being edited.
    void setParameters(const QList<MaterialParameter>& parameters);

signals:
    void parameterChanged(const pf::MaterialParameter& parameter);
    void parameterReset(const QString& name);

private:
    struct Row;
    void rebuild(const QList<MaterialParameter>& parameters);
    void addRow(int index, const MaterialParameter& parameter);
    void refresh(Row& row);
    void commit(Row& row);

    QWidget* m_content = nullptr;
    QGridLayout* m_grid = nullptr;
    QLabel* m_empty = nullptr;
    std::vector<std::unique_ptr<Row>> m_rows;
    QString m_signature;
};

} // namespace pf
