#pragma once

#include "ShaderGraph.h"
#include "../../../resources/ui/NodeGraphEditor.h"
#include <QWidget>
#include <QSplitter>
#include <QTreeWidget>
#include <QPushButton>

namespace ks {
namespace graphics {

class ShaderGraphWidget : public QWidget {
    Q_OBJECT
public:
    explicit ShaderGraphWidget(QWidget* parent = nullptr);
    ~ShaderGraphWidget() override = default;

    void setGraph(const QUuid& graphId);
    QUuid graphId() const { return m_graphId; }

    void setNodePalette(QTreeWidget* palette);

signals:
    void graphChanged();
    void statusMessage(const QString& msg);

public slots:
    void onNodeSelected(const QUuid& nodeId);
    void onGraphChanged();
    void onCompile();
    void onValidate();

private:
    void setupUI();
    void setupConnections();
    void syncGraphToWidget();
    void syncWidgetToGraph();
    void updatePropertyEditor();
    void addNodeFromPalette(const QString& typeName, const QPointF& position);

    QUuid m_graphId;
    ui::NodeGraphWidget* m_nodeGraph = nullptr;
    ui::PropertyEditor* m_propertyEditor = nullptr;
    QPushButton* m_compileBtn = nullptr;
    QPushButton* m_validateBtn = nullptr;
    QTreeWidget* m_nodePalette = nullptr;

    ShaderGraphManager* m_mgr = ShaderGraphManager::instance();
};

} // namespace graphics
} // namespace ks
