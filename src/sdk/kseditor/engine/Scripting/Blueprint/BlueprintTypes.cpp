// ============================================================================
// BlueprintTypes.cpp
// Blueprint type conversions and serialization.
// ============================================================================

#include "BlueprintTypes.h"

namespace ks {
namespace blueprint {

// ============================================================================
// BlueprintPin <-> ui::NodePort conversion
// ============================================================================
ui::NodePort BlueprintPin::toNodePort() const
{
    ui::NodePort port;
    port.id = id;
    port.name = name;

    switch (type) {
        case PinType::Exec:      port.type = "execution"; break;
        case PinType::Bool:      port.type = "bool"; break;
        case PinType::Int:       port.type = "int"; break;
        case PinType::Float:     port.type = "float"; break;
        case PinType::String:    port.type = "string"; break;
        case PinType::Vector:    port.type = "vec3"; break;
        case PinType::Vector2:   port.type = "vec2"; break;
        case PinType::Color:     port.type = "color"; break;
        case PinType::Object:    port.type = "object"; break;
        case PinType::Array:     port.type = "array"; break;
        case PinType::Function:  port.type = "function"; break;
        case PinType::Enum:      port.type = "enum"; break;
        case PinType::Struct:    port.type = "struct"; break;
        case PinType::Wildcard:  port.type = "wildcard"; break;
    }

    port.isInput = isInput;
    port.isMultiConnection = isMultiConnection;
    port.defaultValue = defaultValue;
    port.description = description;
    return port;
}

BlueprintPin BlueprintPin::fromNodePort(const ui::NodePort& port)
{
    BlueprintPin pin;
    pin.id = port.id;
    pin.name = port.name;
    pin.isInput = port.isInput;
    pin.isMultiConnection = port.isMultiConnection;
    pin.defaultValue = port.defaultValue;
    pin.description = port.description;

    if (port.type == "execution") pin.type = PinType::Exec;
    else if (port.type == "bool") pin.type = PinType::Bool;
    else if (port.type == "int") pin.type = PinType::Int;
    else if (port.type == "float") pin.type = PinType::Float;
    else if (port.type == "string") pin.type = PinType::String;
    else if (port.type == "vec3") pin.type = PinType::Vector;
    else if (port.type == "vec2") pin.type = PinType::Vector2;
    else if (port.type == "color") pin.type = PinType::Color;
    else if (port.type == "object") pin.type = PinType::Object;
    else if (port.type == "array") pin.type = PinType::Array;
    else if (port.type == "function") pin.type = PinType::Function;
    else if (port.type == "enum") pin.type = PinType::Enum;
    else if (port.type == "struct") pin.type = PinType::Struct;
    else pin.type = PinType::Wildcard;

    return pin;
}

// ============================================================================
// BlueprintNodeType <-> ui::NodeInfo conversion
// ============================================================================
ui::NodeInfo BlueprintNodeType::toNodeInfo() const
{
    ui::NodeInfo info;
    info.typeName = typeName;
    info.displayName = displayName;
    info.category = category;
    info.description = description;
    info.icon = icon;
    info.defaultProperties = defaultProperties;
    info.isExecNode = !isPureNode;
    info.isPureNode = isPureNode;

    for (const auto& pin : inputs)
        info.inputs.append(pin.toNodePort());
    for (const auto& pin : outputs)
        info.outputs.append(pin.toNodePort());

    return info;
}

BlueprintNodeType BlueprintNodeType::fromNodeInfo(const ui::NodeInfo& info)
{
    BlueprintNodeType type;
    type.typeName = info.typeName;
    type.displayName = info.displayName;
    type.category = info.category;
    type.description = info.description;
    type.icon = info.icon;
    type.defaultProperties = info.defaultProperties;
    type.isPureNode = info.isPureNode;

    for (const auto& port : info.inputs)
        type.inputs.append(BlueprintPin::fromNodePort(port));
    for (const auto& port : info.outputs)
        type.outputs.append(BlueprintPin::fromNodePort(port));

    return type;
}

// ============================================================================
// BlueprintVariable serialization
// ============================================================================
QJsonObject BlueprintVariable::toJson() const
{
    QJsonObject json;
    json["name"] = name;
    json["type"] = static_cast<int>(type);
    json["scope"] = static_cast<int>(scope);
    json["defaultValue"] = QJsonValue::fromVariant(defaultValue);
    json["description"] = description;
    json["exposed"] = exposed;
    json["readOnly"] = readOnly;
    return json;
}

void BlueprintVariable::fromJson(const QJsonObject& json)
{
    name = json["name"].toString();
    type = static_cast<PinType>(json["type"].toInt(static_cast<int>(PinType::Float)));
    scope = static_cast<BlueprintVarScope>(json["scope"].toInt());
    defaultValue = json["defaultValue"].toVariant();
    description = json["description"].toString();
    exposed = json["exposed"].toBool();
    readOnly = json["readOnly"].toBool();
}

// ============================================================================
// BlueprintFunction serialization
// ============================================================================
QJsonObject BlueprintFunction::toJson() const
{
    QJsonObject json;
    json["name"] = name;
    json["category"] = category;
    json["description"] = description;
    json["isEvent"] = isEvent;
    json["isPure"] = isPure;

    QJsonArray inputsArr, outputsArr;
    for (const auto& pin : inputs) {
        QJsonObject p;
        p["id"] = pin.id.toString();
        p["name"] = pin.name;
        p["type"] = static_cast<int>(pin.type);
        p["defaultValue"] = QJsonValue::fromVariant(pin.defaultValue);
        inputsArr.append(p);
    }
    for (const auto& pin : outputs) {
        QJsonObject p;
        p["id"] = pin.id.toString();
        p["name"] = pin.name;
        p["type"] = static_cast<int>(pin.type);
        p["defaultValue"] = QJsonValue::fromVariant(pin.defaultValue);
        outputsArr.append(p);
    }
    json["inputs"] = inputsArr;
    json["outputs"] = outputsArr;
    return json;
}

void BlueprintFunction::fromJson(const QJsonObject& json)
{
    name = json["name"].toString();
    category = json["category"].toString();
    description = json["description"].toString();
    isEvent = json["isEvent"].toBool();
    isPure = json["isPure"].toBool();

    inputs.clear();
    for (const auto& p : json["inputs"].toArray()) {
        QJsonObject po = p.toObject();
        BlueprintPin pin;
        pin.id = QUuid(po["id"].toString());
        pin.name = po["name"].toString();
        pin.type = static_cast<PinType>(po["type"].toInt());
        pin.defaultValue = po["defaultValue"].toVariant();
        pin.isInput = true;
        inputs.append(pin);
    }
    outputs.clear();
    for (const auto& p : json["outputs"].toArray()) {
        QJsonObject po = p.toObject();
        BlueprintPin pin;
        pin.id = QUuid(po["id"].toString());
        pin.name = po["name"].toString();
        pin.type = static_cast<PinType>(po["type"].toInt());
        pin.defaultValue = po["defaultValue"].toVariant();
        pin.isInput = false;
        outputs.append(pin);
    }
}

// ============================================================================
// BlueprintGraphData serialization
// ============================================================================
QJsonObject BlueprintGraphData::toJson() const
{
    QJsonObject json;
    json["name"] = name;
    json["id"] = id.toString();

    QJsonArray nodesArr;
    for (const auto& node : nodes) {
        QJsonObject n;
        n["id"] = node.id.toString();
        n["typeName"] = node.typeName;
        n["title"] = node.title;
        n["x"] = node.position.x();
        n["y"] = node.position.y();
        n["properties"] = QJsonObject::fromVariantMap(node.properties);
        n["comment"] = node.comment;
        n["minimized"] = node.minimized;

        QJsonArray inputIds, outputIds;
        for (const auto& pid : node.inputPortIds) inputIds.append(pid.toString());
        for (const auto& pid : node.outputPortIds) outputIds.append(pid.toString());
        n["inputPortIds"] = inputIds;
        n["outputPortIds"] = outputIds;
        nodesArr.append(n);
    }
    json["nodes"] = nodesArr;

    QJsonArray connsArr;
    for (const auto& conn : connections) {
        QJsonObject c;
        c["id"] = conn.id.toString();
        c["fromNodeId"] = conn.fromNodeId.toString();
        c["fromPortId"] = conn.fromPortId.toString();
        c["toNodeId"] = conn.toNodeId.toString();
        c["toPortId"] = conn.toPortId.toString();
        connsArr.append(c);
    }
    json["connections"] = connsArr;

    QJsonArray varsArr;
    for (const auto& var : variables) varsArr.append(var.toJson());
    json["variables"] = varsArr;

    QJsonArray funcsArr;
    for (const auto& func : customFunctions) funcsArr.append(func.toJson());
    json["customFunctions"] = funcsArr;

    json["metadata"] = QJsonObject::fromVariantMap(metadata);
    return json;
}

void BlueprintGraphData::fromJson(const QJsonObject& json)
{
    name = json["name"].toString();
    id = QUuid(json["id"].toString());

    nodes.clear();
    for (const auto& n : json["nodes"].toArray()) {
        QJsonObject no = n.toObject();
        ui::GraphNode node;
        node.id = QUuid(no["id"].toString());
        node.typeName = no["typeName"].toString();
        node.title = no["title"].toString();
        node.position = QPointF(no["x"].toDouble(), no["y"].toDouble());
        node.properties = no["properties"].toObject().toVariantMap();
        node.comment = no["comment"].toString();
        node.minimized = no["minimized"].toBool();

        for (const auto& pid : no["inputPortIds"].toArray())
            node.inputPortIds.append(QUuid(pid.toString()));
        for (const auto& pid : no["outputPortIds"].toArray())
            node.outputPortIds.append(QUuid(pid.toString()));
        nodes.append(node);
    }

    connections.clear();
    for (const auto& c : json["connections"].toArray()) {
        QJsonObject co = c.toObject();
        ui::GraphConnection conn;
        conn.id = QUuid(co["id"].toString());
        conn.fromNodeId = QUuid(co["fromNodeId"].toString());
        conn.fromPortId = QUuid(co["fromPortId"].toString());
        conn.toNodeId = QUuid(co["toNodeId"].toString());
        conn.toPortId = QUuid(co["toPortId"].toString());
        connections.append(conn);
    }

    variables.clear();
    for (const auto& v : json["variables"].toArray()) {
        BlueprintVariable var;
        var.fromJson(v.toObject());
        variables.append(var);
    }

    customFunctions.clear();
    for (const auto& f : json["customFunctions"].toArray()) {
        BlueprintFunction func;
        func.fromJson(f.toObject());
        customFunctions.append(func);
    }

    metadata = json["metadata"].toObject().toVariantMap();
}

}} // namespace ks::blueprint
