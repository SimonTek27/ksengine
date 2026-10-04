#include "3DModeling_panels.h"
#include "sdk/kseditor/engine/Graphics/SceneObject.h"
#include <QDebug>
#include <QListWidgetItem>
#include <QGroupBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QButtonGroup>
#include <QColorDialog>
#include <QJsonArray>
#include <QPainterPath>
#include <QLineEdit>

namespace ks {

// All implementations from 3DModeling_panels.cpp

// ============================================================================
// Properties Panel
// ============================================================================

PropertiesPanel::PropertiesPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText("Object Name");
    connect(m_nameEdit, &QLineEdit::textChanged, this, &PropertiesPanel::onNameChanged);

    m_visibleCheck = new QCheckBox("Visible", this);
    connect(m_visibleCheck, &QCheckBox::toggled, this, &PropertiesPanel::onVisibleChanged);

    auto* headerLayout = new QHBoxLayout();
    headerLayout->addWidget(new QLabel("Name:"));
    headerLayout->addWidget(m_nameEdit, 1);
    headerLayout->addWidget(m_visibleCheck);
    mainLayout->addLayout(headerLayout);

    buildTransformGroup();
    buildInfoGroup();

    mainLayout->addStretch();
}

PropertiesPanel::~PropertiesPanel() = default;

void PropertiesPanel::buildTransformGroup()
{
    m_transformGroup = new QGroupBox("Transform", this);
    auto* layout = new QFormLayout(m_transformGroup);
    layout->setSpacing(2);
    layout->setContentsMargins(6, 10, 6, 6);

    auto* posLayout = new QHBoxLayout();
    m_posX = new QDoubleSpinBox(); m_posX->setRange(-9999, 9999); m_posX->setSingleStep(0.1);
    m_posY = new QDoubleSpinBox(); m_posY->setRange(-9999, 9999); m_posY->setSingleStep(0.1);
    m_posZ = new QDoubleSpinBox(); m_posZ->setRange(-9999, 9999); m_posZ->setSingleStep(0.1);
    posLayout->addWidget(new QLabel("X")); posLayout->addWidget(m_posX);
    posLayout->addWidget(new QLabel("Y")); posLayout->addWidget(m_posY);
    posLayout->addWidget(new QLabel("Z")); posLayout->addWidget(m_posZ);
    layout->addRow("Position", posLayout);

    auto* rotLayout = new QHBoxLayout();
    m_rotX = new QDoubleSpinBox(); m_rotX->setRange(-360, 360); m_rotX->setSingleStep(1);
    m_rotY = new QDoubleSpinBox(); m_rotY->setRange(-360, 360); m_rotY->setSingleStep(1);
    m_rotZ = new QDoubleSpinBox(); m_rotZ->setRange(-360, 360); m_rotZ->setSingleStep(1);
    rotLayout->addWidget(new QLabel("X")); rotLayout->addWidget(m_rotX);
    rotLayout->addWidget(new QLabel("Y")); rotLayout->addWidget(m_rotY);
    rotLayout->addWidget(new QLabel("Z")); rotLayout->addWidget(m_rotZ);
    layout->addRow("Rotation", rotLayout);

    auto* scaleLayout = new QHBoxLayout();
    m_scaleX = new QDoubleSpinBox(); m_scaleX->setRange(0.001, 9999); m_scaleX->setSingleStep(0.1); m_scaleX->setValue(1);
    m_scaleY = new QDoubleSpinBox(); m_scaleY->setRange(0.001, 9999); m_scaleY->setSingleStep(0.1); m_scaleY->setValue(1);
    m_scaleZ = new QDoubleSpinBox(); m_scaleZ->setRange(0.001, 9999); m_scaleZ->setSingleStep(0.1); m_scaleZ->setValue(1);
    scaleLayout->addWidget(new QLabel("X")); scaleLayout->addWidget(m_scaleX);
    scaleLayout->addWidget(new QLabel("Y")); scaleLayout->addWidget(m_scaleY);
    scaleLayout->addWidget(new QLabel("Z")); scaleLayout->addWidget(m_scaleZ);
    layout->addRow("Scale", scaleLayout);

    connect(m_posX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PropertiesPanel::onPosXChanged);
    connect(m_posY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PropertiesPanel::onPosYChanged);
    connect(m_posZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PropertiesPanel::onPosZChanged);
    connect(m_rotX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PropertiesPanel::onRotXChanged);
    connect(m_rotY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PropertiesPanel::onRotYChanged);
    connect(m_rotZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PropertiesPanel::onRotZChanged);
    connect(m_scaleX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PropertiesPanel::onScaleXChanged);
    connect(m_scaleY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PropertiesPanel::onScaleYChanged);
    connect(m_scaleZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PropertiesPanel::onScaleZChanged);
}

void PropertiesPanel::buildInfoGroup()
{
    m_infoGroup = new QGroupBox("Info", this);
    auto* layout = new QFormLayout(m_infoGroup);
    layout->setSpacing(2);
    layout->setContentsMargins(6, 10, 6, 6);

    m_tree = new QTreeWidget(m_infoGroup);
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(2);
    m_tree->setColumnWidth(0, 100);
    layout->addWidget(m_tree);
}

void PropertiesPanel::setObject(SceneObject* obj)
{
    m_object = obj;
    if (!obj) { clear(); return; }

    m_nameEdit->blockSignals(true);
    m_nameEdit->setText(obj->name());
    m_nameEdit->blockSignals(false);

    m_visibleCheck->blockSignals(true);
    m_visibleCheck->setChecked(obj->isVisible());
    m_visibleCheck->blockSignals(false);

    m_tree->clear();
    auto addRow = [&](const QString& key, const QString& value) {
        auto* item = new QTreeWidgetItem(m_tree, {key, value});
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    };

    addRow("ID", QString::number(obj->id()));
    addRow("Type", obj->type() == SceneObject::Type::Mesh ? "Mesh" :
                       obj->type() == SceneObject::Type::Light ? "Light" :
                       obj->type() == SceneObject::Type::Camera ? "Camera" :
                       obj->type() == SceneObject::Type::Bone ? "Bone" : "Node");
    addRow("Has Mesh", obj->hasMesh() ? "Yes" : "No");
    addRow("Children", QString::number(obj->children().size()));
    if (obj->parent()) addRow("Parent", obj->parent()->name());
}

void PropertiesPanel::clear()
{
    m_object = nullptr;
    m_nameEdit->clear();
    m_visibleCheck->setChecked(false);
    m_tree->clear();
    m_posX->setValue(0); m_posY->setValue(0); m_posZ->setValue(0);
    m_rotX->setValue(0); m_rotY->setValue(0); m_rotZ->setValue(0);
    m_scaleX->setValue(1); m_scaleY->setValue(1); m_scaleZ->setValue(1);
}

void PropertiesPanel::onNameChanged(const QString& text)
{
    if (m_object) {
        m_object->setName(text);
        emit propertyChanged("name", text);
    }
}

void PropertiesPanel::onVisibleChanged(bool checked)
{
    if (m_object) {
        m_object->setVisible(checked);
        emit propertyChanged("visible", checked);
    }
}

void PropertiesPanel::onPosXChanged(double v) { emit transformChanged("posX", v); }
void PropertiesPanel::onPosYChanged(double v) { emit transformChanged("posY", v); }
void PropertiesPanel::onPosZChanged(double v) { emit transformChanged("posZ", v); }
void PropertiesPanel::onRotXChanged(double v) { emit transformChanged("rotX", v); }
void PropertiesPanel::onRotYChanged(double v) { emit transformChanged("rotY", v); }
void PropertiesPanel::onRotZChanged(double v) { emit transformChanged("rotZ", v); }
void PropertiesPanel::onScaleXChanged(double v) { emit transformChanged("scaleX", v); }
void PropertiesPanel::onScaleYChanged(double v) { emit transformChanged("scaleY", v); }
void PropertiesPanel::onScaleZChanged(double v) { emit transformChanged("scaleZ", v); }

// ============================================================================
// Material Editor Panel
// ============================================================================

MaterialEditorPanel::MaterialEditorPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    buildSurfaceGroup();
    buildPBRGroup();
    mainLayout->addStretch();
}

MaterialEditorPanel::~MaterialEditorPanel() = default;

void MaterialEditorPanel::buildSurfaceGroup()
{
    m_surfaceGroup = new QGroupBox("Surface", this);
    auto* layout = new QFormLayout(m_surfaceGroup);
    layout->setSpacing(4);
    layout->setContentsMargins(6, 10, 6, 6);

    auto* baseColorRow = new QHBoxLayout();
    m_baseColorBtn = new QPushButton();
    m_baseColorBtn->setFixedSize(40, 24);
    m_baseColorBtn->setStyleSheet(QString("background-color: %1; border: 1px solid #555;").arg(m_baseColor.name()));
    connect(m_baseColorBtn, &QPushButton::clicked, this, &MaterialEditorPanel::onBaseColorClicked);
    baseColorRow->addWidget(m_baseColorBtn);
    baseColorRow->addWidget(new QLabel("Base Color"));
    layout->addRow(baseColorRow);

    auto* emissiveRow = new QHBoxLayout();
    m_emissiveBtn = new QPushButton();
    m_emissiveBtn->setFixedSize(40, 24);
    m_emissiveBtn->setStyleSheet(QString("background-color: %1; border: 1px solid #555;").arg(m_emissiveColor.name()));
    connect(m_emissiveBtn, &QPushButton::clicked, this, &MaterialEditorPanel::onEmissiveClicked);
    emissiveRow->addWidget(m_emissiveBtn);
    emissiveRow->addWidget(new QLabel("Emissive"));
    layout->addRow(emissiveRow);

    m_blendModeCombo = new QComboBox();
    m_blendModeCombo->addItems({"Opaque", "Alpha Blend", "Alpha Test", "Additive"});
    layout->addRow("Blend Mode", m_blendModeCombo);

    m_twoSidedCheck = new QCheckBox("Two Sided");
    layout->addRow("", m_twoSidedCheck);

    connect(m_blendModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { emit materialChanged(); });
    connect(m_twoSidedCheck, &QCheckBox::toggled, this, [this]() { emit materialChanged(); });
}

void MaterialEditorPanel::buildPBRGroup()
{
    m_pbrGroup = new QGroupBox("PBR Properties", this);
    auto* layout = new QFormLayout(m_pbrGroup);
    layout->setSpacing(4);
    layout->setContentsMargins(6, 10, 6, 6);

    m_roughnessSlider = new QSlider(Qt::Horizontal);
    m_roughnessSlider->setRange(0, 100);
    m_roughnessSlider->setValue(50);
    m_roughnessLabel = new QLabel("0.50");
    m_roughnessLabel->setFixedWidth(40);
    m_roughnessLabel->setAlignment(Qt::AlignRight);
    auto* roughRow = new QHBoxLayout();
    roughRow->addWidget(m_roughnessSlider);
    roughRow->addWidget(m_roughnessLabel);
    layout->addRow("Roughness", roughRow);

    m_metallicSlider = new QSlider(Qt::Horizontal);
    m_metallicSlider->setRange(0, 100);
    m_metallicSlider->setValue(0);
    m_metallicLabel = new QLabel("0.00");
    m_metallicLabel->setFixedWidth(40);
    m_metallicLabel->setAlignment(Qt::AlignRight);
    auto* metalRow = new QHBoxLayout();
    metalRow->addWidget(m_metallicSlider);
    metalRow->addWidget(m_metallicLabel);
    layout->addRow("Metallic", metalRow);

    m_opacitySlider = new QSlider(Qt::Horizontal);
    m_opacitySlider->setRange(0, 100);
    m_opacitySlider->setValue(100);
    m_opacityLabel = new QLabel("1.00");
    m_opacityLabel->setFixedWidth(40);
    m_opacityLabel->setAlignment(Qt::AlignRight);
    auto* opacRow = new QHBoxLayout();
    opacRow->addWidget(m_opacitySlider);
    opacRow->addWidget(m_opacityLabel);
    layout->addRow("Opacity", opacRow);

    m_emissiveIntensitySlider = new QSlider(Qt::Horizontal);
    m_emissiveIntensitySlider->setRange(0, 200);
    m_emissiveIntensitySlider->setValue(0);
    m_emissiveIntensityLabel = new QLabel("0.00");
    m_emissiveIntensityLabel->setFixedWidth(40);
    m_emissiveIntensityLabel->setAlignment(Qt::AlignRight);
    auto* emisRow = new QHBoxLayout();
    emisRow->addWidget(m_emissiveIntensitySlider);
    emisRow->addWidget(m_emissiveIntensityLabel);
    layout->addRow("Emissive Intensity", emisRow);

    connect(m_roughnessSlider, &QSlider::valueChanged, this, [this](int v) {
        m_roughnessLabel->setText(QString("%1").arg(v / 100.0, 0, 'f', 2));
        emit materialChanged();
    });
    connect(m_metallicSlider, &QSlider::valueChanged, this, [this](int v) {
        m_metallicLabel->setText(QString("%1").arg(v / 100.0, 0, 'f', 2));
        emit materialChanged();
    });
    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int v) {
        m_opacityLabel->setText(QString("%1").arg(v / 100.0, 0, 'f', 2));
        emit materialChanged();
    });
    connect(m_emissiveIntensitySlider, &QSlider::valueChanged, this, [this](int v) {
        m_emissiveIntensityLabel->setText(QString("%1").arg(v / 100.0, 0, 'f', 2));
        emit materialChanged();
    });
}

void MaterialEditorPanel::setMaterial(void* material)
{
    m_material = material;
    emit materialChanged();
}

void MaterialEditorPanel::clear()
{
    m_material = nullptr;
    m_baseColor = QColor(204, 204, 204);
    m_emissiveColor = QColor(0, 0, 0);
    m_baseColorBtn->setStyleSheet(QString("background-color: %1; border: 1px solid #555;").arg(m_baseColor.name()));
    m_emissiveBtn->setStyleSheet(QString("background-color: %1; border: 1px solid #555;").arg(m_emissiveColor.name()));
    m_roughnessSlider->setValue(50);
    m_metallicSlider->setValue(0);
    m_opacitySlider->setValue(100);
    m_emissiveIntensitySlider->setValue(0);
}

void MaterialEditorPanel::onBaseColorClicked()
{
    QColor c = QColorDialog::getColor(m_baseColor, this, "Select Base Color");
    if (c.isValid()) {
        m_baseColor = c;
        m_baseColorBtn->setStyleSheet(QString("background-color: %1; border: 1px solid #555;").arg(c.name()));
        emit materialChanged();
    }
}

void MaterialEditorPanel::onEmissiveClicked()
{
    QColor c = QColorDialog::getColor(m_emissiveColor, this, "Select Emissive Color");
    if (c.isValid()) {
        m_emissiveColor = c;
        m_emissiveBtn->setStyleSheet(QString("background-color: %1; border: 1px solid #555;").arg(c.name()));
        emit materialChanged();
    }
}

// ============================================================================
// Tool Palette Widget
// ============================================================================

ToolPaletteWidget::ToolPaletteWidget(QWidget* parent)
    : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(4, 4, 4, 4);
    m_layout->setSpacing(2);

    m_buttonGroup = new QButtonGroup(this);
    m_buttonGroup->setExclusive(true);
    connect(m_buttonGroup, &QButtonGroup::idClicked, this, [this](int id) {
        QPushButton* btn = qobject_cast<QPushButton*>(m_buttonGroup->button(id));
        if (btn) {
            m_currentTool = btn->toolTip();
            emit toolSelected(m_currentTool);
            updateToolButtons();
        }
    });

    addTool("Select", "S", "Select objects (V)");
    addTool("Move", "M", "Move tool (G)");
    addTool("Rotate", "R", "Rotate tool (R)");
    addTool("Scale", "Sc", "Scale tool (S)");
    addTool("Add Cube", "+", "Add cube primitive");
    addTool("Add Sphere", "o", "Add sphere primitive");
    addTool("Add Cylinder", "C", "Add cylinder primitive");
    addTool("Add Light", "L", "Add light source");
    addTool("Add Camera", "Cam", "Add camera");
}

ToolPaletteWidget::~ToolPaletteWidget() = default;

void ToolPaletteWidget::addTool(const QString& name, const QString& icon, const QString& tooltip)
{
    auto* btn = new QPushButton(name, this);
    btn->setToolTip(tooltip);
    btn->setCheckable(true);
    btn->setFixedHeight(26);
    btn->setStyleSheet(
        "QPushButton { text-align: left; padding-left: 8px; background: #2d2d2d; "
        "color: #cccccc; border: 1px solid #444; font-size: 11px; }"
        "QPushButton:hover { background: #3a3a3a; }"
        "QPushButton:checked { background: #4a6fa5; color: #ffffff; border-color: #5a8fc5; }"
    );

    int id = m_toolWidgets.size();
    m_buttonGroup->addButton(btn, id);
    m_toolWidgets[name] = btn;
    m_layout->addWidget(btn);
}

void ToolPaletteWidget::removeTool(const QString& name)
{
    if (m_toolWidgets.contains(name)) {
        auto* btn = qobject_cast<QPushButton*>(m_toolWidgets[name]);
        if (btn) m_buttonGroup->removeButton(btn);
        m_layout->removeWidget(m_toolWidgets[name]);
        delete m_toolWidgets[name];
        m_toolWidgets.remove(name);
    }
}

void ToolPaletteWidget::selectTool(const QString& name)
{
    if (m_currentTool != name) {
        m_currentTool = name;
        updateToolButtons();
        emit toolSelected(name);
    }
}

void ToolPaletteWidget::updateToolButtons()
{
    for (auto it = m_toolWidgets.begin(); it != m_toolWidgets.end(); ++it) {
        auto* btn = qobject_cast<QPushButton*>(it.value());
        if (btn) btn->setChecked(it.key() == m_currentTool);
    }
}

// ============================================================================
// Object List Widget
// ============================================================================

ObjectListWidget::ObjectListWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(m_list);

    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        QString id = m_objectIds.value(item->text());
        emit objectSelected(id);
    });

    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        QString id = m_objectIds.value(item->text());
        emit objectDoubleClicked(id);
    });
}

ObjectListWidget::~ObjectListWidget() = default;

void ObjectListWidget::addObject(const QString& id, const QString& name, const QString& type)
{
    QString display = name;
    if (!type.isEmpty()) display = QString("[%1] %2").arg(type, name);
    auto* item = new QListWidgetItem(display, m_list);
    item->setData(Qt::UserRole, id);
    m_objectIds[display] = id;
}

void ObjectListWidget::removeObject(const QString& id)
{
    for (auto it = m_objectIds.begin(); it != m_objectIds.end(); ++it) {
        if (it.value() == id) {
            QList<QListWidgetItem*> items = m_list->findItems(it.key(), Qt::MatchExactly);
            if (!items.isEmpty()) {
                delete m_list->takeItem(m_list->row(items.first()));
            }
            m_objectIds.erase(it);
            return;
        }
    }
}

void ObjectListWidget::updateObject(const QString& id, const QString& name)
{
    for (auto it = m_objectIds.begin(); it != m_objectIds.end(); ++it) {
        if (it.value() == id) {
            QList<QListWidgetItem*> items = m_list->findItems(it.key(), Qt::MatchExactly);
            if (!items.isEmpty()) {
                items.first()->setText(name);
                m_objectIds.insert(name, m_objectIds.take(it.key()));
            }
            return;
        }
    }
}

void ObjectListWidget::setSelection(const QStringList& ids)
{
    m_list->clearSelection();
    for (int i = 0; i < m_list->count(); ++i) {
        auto* item = m_list->item(i);
        if (ids.contains(item->data(Qt::UserRole).toString())) {
            item->setSelected(true);
        }
    }
}

QStringList ObjectListWidget::selection() const
{
    QStringList result;
    for (QListWidgetItem* item : m_list->selectedItems()) {
        result.append(item->data(Qt::UserRole).toString());
    }
    return result;
}

// ============================================================================
// Layer Panel Widget
// ============================================================================

LayerPanelWidget::LayerPanelWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabel("Layers");
    m_tree->setColumnCount(3);
    m_tree->setColumnWidth(0, 120);
    m_tree->setColumnWidth(1, 40);
    m_tree->setColumnWidth(2, 40);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemClicked, this, &LayerPanelWidget::onItemClicked);
}

LayerPanelWidget::~LayerPanelWidget() = default;

void LayerPanelWidget::addLayer(const QString& name, bool visible, bool locked)
{
    auto* item = new QTreeWidgetItem(m_tree);
    item->setText(0, name);
    item->setText(1, visible ? QString::fromUtf8("\u2713") : "");
    item->setText(2, locked ? QString::fromUtf8("\u2713") : "");
    item->setTextAlignment(1, Qt::AlignCenter);
    item->setTextAlignment(2, Qt::AlignCenter);
    m_tree->addTopLevelItem(item);
    m_layers[name] = item;
    m_layerData[name] = {visible, locked};
}

void LayerPanelWidget::removeLayer(const QString& name)
{
    if (m_layers.contains(name)) {
        delete m_layers[name];
        m_layers.remove(name);
        m_layerData.remove(name);
        emit layerChanged(name);
    }
}

void LayerPanelWidget::setLayerVisible(const QString& name, bool visible)
{
    if (m_layers.contains(name)) {
        m_layers[name]->setText(1, visible ? QString::fromUtf8("\u2713") : "");
        m_layerData[name].visible = visible;
        emit layerVisibilityChanged(name, visible);
    }
}

void LayerPanelWidget::setLayerLocked(const QString& name, bool locked)
{
    if (m_layers.contains(name)) {
        m_layers[name]->setText(2, locked ? QString::fromUtf8("\u2713") : "");
        m_layerData[name].locked = locked;
        emit layerChanged(name);
    }
}

void LayerPanelWidget::onItemChanged(QTreeWidgetItem* item, int column)
{
    QString name = item->text(0);
    if (column == 1) {
        bool visible = !m_layerData[name].visible;
        setLayerVisible(name, visible);
    } else if (column == 2) {
        bool locked = !m_layerData[name].locked;
        setLayerLocked(name, locked);
    }
}

void LayerPanelWidget::onItemClicked(QTreeWidgetItem* item, int column)
{
    m_selectedLayer = item->text(0);
    emit layerSelectionChanged(m_selectedLayer);
    if (column == 1 || column == 2) {
        onItemChanged(item, column);
    }
}

} // namespace ks
