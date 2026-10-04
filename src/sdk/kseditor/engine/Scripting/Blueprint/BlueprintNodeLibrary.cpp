// ============================================================================
// BlueprintNodeLibrary.cpp
// Built-in Blueprint node types: Events, Flow Control, Math, Variables, etc.
// ============================================================================

#include "BlueprintNodeLibrary.h"
#include <QVector3D>
#include <QVariant>
#include <cmath>

namespace ks {
namespace blueprint {

// ============================================================================
// Helper to create ports
// ============================================================================
static BlueprintPin makeExecPin(const QString& name, bool isInput) {
    BlueprintPin p;
    p.id = QUuid::createUuid();
    p.name = name;
    p.type = PinType::Exec;
    p.isInput = isInput;
    p.isMultiConnection = isInput;
    return p;
}

static BlueprintPin makePin(const QString& name, PinType type, bool isInput,
                             QVariant defaultValue = QVariant()) {
    BlueprintPin p;
    p.id = QUuid::createUuid();
    p.name = name;
    p.type = type;
    p.isInput = isInput;
    p.defaultValue = defaultValue;
    return p;
}

// ============================================================================
// Helper to extract input value
// ============================================================================
static float getInputFloat(const QMap<QUuid, QVariant>& inputs, const BlueprintPin& pin, float def = 0.f) {
    return inputs.value(pin.id, pin.defaultValue).toFloat();
}

static int getInputInt(const QMap<QUuid, QVariant>& inputs, const BlueprintPin& pin, int def = 0) {
    return inputs.value(pin.id, pin.defaultValue).toInt();
}

static bool getInputBool(const QMap<QUuid, QVariant>& inputs, const BlueprintPin& pin, bool def = false) {
    return inputs.value(pin.id, pin.defaultValue).toBool();
}

static QString getInputString(const QMap<QUuid, QVariant>& inputs, const BlueprintPin& pin, const QString& def = QString()) {
    return inputs.value(pin.id, pin.defaultValue).toString();
}

static QVector3D getInputVector(const QMap<QUuid, QVariant>& inputs, const BlueprintPin& pin) {
    QVariant v = inputs.value(pin.id, pin.defaultValue);
    return v.value<QVector3D>();
}

static QColor getInputColor(const QMap<QUuid, QVariant>& inputs, const BlueprintPin& pin) {
    QVariant v = inputs.value(pin.id, pin.defaultValue);
    return v.value<QColor>();
}

// ============================================================================
// Register built-in nodes
// ============================================================================
void BlueprintNodeLibrary::registerBuiltInNodes()
{
    // ================================================================
    // EVENT NODES
    // ================================================================
    {
        BlueprintNodeType n;
        n.typeName = "Event.BeginPlay";
        n.displayName = "Begin Play";
        n.category = "Events";
        n.description = "Called when the game or object starts";
        n.isEventNode = true;
        n.outputs.append(makeExecPin("Exec", false));
        n.execute = [](const QUuid&, const QMap<QUuid, QVariant>&,
                       QMap<QUuid, QVariant>& outputs, void*) {
            // Execution continues through the exec output
            Q_UNUSED(outputs);
        };
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Event.Tick";
        n.displayName = "Tick";
        n.category = "Events";
        n.description = "Called every frame with delta time";
        n.isEventNode = true;
        n.outputs.append(makeExecPin("Exec", false));
        n.outputs.append(makePin("Delta Time", PinType::Float, false, 0.016f));
        n.execute = [](const QUuid&, const QMap<QUuid, QVariant>&,
                       QMap<QUuid, QVariant>& outputs, void*) {
            Q_UNUSED(outputs);
        };
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Event.Custom";
        n.displayName = "Custom Event";
        n.category = "Events";
        n.description = "Custom event with parameters";
        n.isEventNode = true;
        n.outputs.append(makeExecPin("Exec", false));
        n.execute = [](const QUuid&, const QMap<QUuid, QVariant>&,
                       QMap<QUuid, QVariant>& outputs, void*) {
            Q_UNUSED(outputs);
        };
        registerNodeType(n);
    }

    // ================================================================
    // FLOW CONTROL
    // ================================================================
    {
        BlueprintNodeType n;
        n.typeName = "Flow.Branch";
        n.displayName = "Branch";
        n.category = "Flow Control";
        n.description = "If condition is true, execute True, else execute False";
        n.inputs.append(makeExecPin("Exec", true));
        n.inputs.append(makePin("Condition", PinType::Bool, true));
        n.outputs.append(makeExecPin("True", false));
        n.outputs.append(makeExecPin("False", false));
        n.execute = [](const QUuid&, const QMap<QUuid, QVariant>& inputs,
                       QMap<QUuid, QVariant>& outputs, void* ctx) {
            bool cond = false;
            for (const auto& pin : {inputs.keys()}) {
                Q_UNUSED(pin);
            }
            // Find the Condition pin value
            for (auto it = inputs.begin(); it != inputs.end(); ++it) {
                if (it.value().typeId() == QMetaType::Bool) {
                    cond = it.value().toBool();
                    break;
                }
            }
            Q_UNUSED(ctx);
            // The executor will follow the appropriate exec pin
        };
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Flow.Sequence";
        n.displayName = "Sequence";
        n.category = "Flow Control";
        n.description = "Execute multiple output pins in order";
        n.inputs.append(makeExecPin("Exec", true));
        n.outputs.append(makeExecPin("Then 0", false));
        n.outputs.append(makeExecPin("Then 1", false));
        n.outputs.append(makeExecPin("Then 2", false));
        n.isMacroNode = true;
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Flow.ForLoop";
        n.displayName = "For Loop";
        n.category = "Flow Control";
        n.description = "Execute a loop from start to end";
        n.inputs.append(makeExecPin("Exec", true));
        n.inputs.append(makePin("Start", PinType::Int, true, 0));
        n.inputs.append(makePin("End", PinType::Int, true, 10));
        n.outputs.append(makeExecPin("Loop Body", false));
        n.outputs.append(makeExecPin("Completed", false));
        n.outputs.append(makePin("Index", PinType::Int, false));
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Flow.ForEachLoop";
        n.displayName = "For Each Loop";
        n.category = "Flow Control";
        n.description = "Iterate over an array";
        n.inputs.append(makeExecPin("Exec", true));
        n.inputs.append(makePin("Array", PinType::Array, true));
        n.outputs.append(makeExecPin("Loop Body", false));
        n.outputs.append(makeExecPin("Completed", false));
        n.outputs.append(makePin("Element", PinType::Wildcard, false));
        n.outputs.append(makePin("Index", PinType::Int, false));
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Flow.Delay";
        n.displayName = "Delay";
        n.category = "Flow Control";
        n.description = "Wait for a specified duration";
        n.inputs.append(makeExecPin("Exec", true));
        n.inputs.append(makePin("Duration", PinType::Float, true, 1.0f));
        n.outputs.append(makeExecPin("Completed", false));
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Flow.Print";
        n.displayName = "Print String";
        n.category = "Flow Control";
        n.description = "Print a string to the debug log";
        n.inputs.append(makeExecPin("Exec", true));
        n.inputs.append(makePin("String", PinType::String, true, "Hello"));
        n.inputs.append(makePin("Color", PinType::Color, true, QColor(255,255,255)));
        n.outputs.append(makeExecPin("Then", false));
        n.execute = [](const QUuid&, const QMap<QUuid, QVariant>& inputs,
                       QMap<QUuid, QVariant>& outputs, void*) {
            for (auto it = inputs.begin(); it != inputs.end(); ++it) {
                if (it.value().typeId() == QMetaType::QString) {
                    qDebug("Blueprint: %s", it.value().toString().toUtf8().constData());
                    break;
                }
            }
            Q_UNUSED(outputs);
        };
        registerNodeType(n);
    }

    // ================================================================
    // MATH - Float
    // ================================================================
    auto registerMathFloat = [&](const QString& name, const QString& display,
                                 std::function<float(float, float)> op) {
        BlueprintNodeType n;
        n.typeName = name;
        n.displayName = display;
        n.category = "Math/Float";
        n.isPureNode = true;
        n.inputs.append(makePin("A", PinType::Float, true, 0.f));
        n.inputs.append(makePin("B", PinType::Float, true, 0.f));
        n.outputs.append(makePin("Result", PinType::Float, false));
        n.execute = [op](const QUuid&, const QMap<QUuid, QVariant>& inputs,
                         QMap<QUuid, QVariant>& outputs, void*) {
            float a = 0, b = 0;
            for (const auto& v : inputs.values()) {
                if (v.typeId() == QMetaType::Float) {
                    if (a == 0) a = v.toFloat();
                    else b = v.toFloat();
                }
            }
            for (auto& out : outputs) out = op(a, b);
        };
        registerNodeType(n);
    };

    registerMathFloat("Math.Float.Add", "Add +", [](float a, float b){ return a + b; });
    registerMathFloat("Math.Float.Subtract", "Subtract -", [](float a, float b){ return a - b; });
    registerMathFloat("Math.Float.Multiply", "Multiply *", [](float a, float b){ return a * b; });
    registerMathFloat("Math.Float.Divide", "Divide /", [](float a, float b){ return b != 0 ? a / b : 0; });
    registerMathFloat("Math.Float.Modulo", "Modulo %", [](float a, float b){ return b != 0 ? std::fmod(a, b) : 0; });
    registerMathFloat("Math.Float.Power", "Power", [](float a, float b){ return std::pow(a, b); });
    registerMathFloat("Math.Float.Min", "Min", [](float a, float b){ return std::min(a, b); });
    registerMathFloat("Math.Float.Max", "Max", [](float a, float b){ return std::max(a, b); });
    registerMathFloat("Math.Float.Clamp", "Clamp", [](float a, float b){ return qBound(a, 0.5f*(a+b), b); });

    // Float unary
    {
        BlueprintNodeType n;
        n.typeName = "Math.Float.Negate";
        n.displayName = "Negate";
        n.category = "Math/Float";
        n.isPureNode = true;
        n.inputs.append(makePin("A", PinType::Float, true, 0.f));
        n.outputs.append(makePin("Result", PinType::Float, false));
        n.execute = [](const QUuid&, const QMap<QUuid, QVariant>& inputs,
                       QMap<QUuid, QVariant>& outputs, void*) {
            float a = 0;
            for (const auto& v : inputs.values()) {
                if (v.typeId() == QMetaType::Float) { a = v.toFloat(); break; }
            }
            for (auto& out : outputs) out = -a;
        };
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Math.Float.Abs";
        n.displayName = "Absolute";
        n.category = "Math/Float";
        n.isPureNode = true;
        n.inputs.append(makePin("A", PinType::Float, true, 0.f));
        n.outputs.append(makePin("Result", PinType::Float, false));
        n.execute = [](const QUuid&, const QMap<QUuid, QVariant>& inputs,
                       QMap<QUuid, QVariant>& outputs, void*) {
            float a = 0;
            for (const auto& v : inputs.values()) {
                if (v.typeId() == QMetaType::Float) { a = v.toFloat(); break; }
            }
            for (auto& out : outputs) out = std::abs(a);
        };
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Math.Float.Lerp";
        n.displayName = "Lerp";
        n.category = "Math/Float";
        n.description = "Linear interpolation between A and B";
        n.isPureNode = true;
        n.inputs.append(makePin("A", PinType::Float, true, 0.f));
        n.inputs.append(makePin("B", PinType::Float, true, 1.f));
        n.inputs.append(makePin("Alpha", PinType::Float, true, 0.5f));
        n.outputs.append(makePin("Result", PinType::Float, false));
        n.execute = [](const QUuid&, const QMap<QUuid, QVariant>& inputs,
                       QMap<QUuid, QVariant>& outputs, void*) {
            float vals[3] = {0, 1, 0.5f};
            int idx = 0;
            for (const auto& v : inputs.values()) {
                if (v.typeId() == QMetaType::Float && idx < 3) vals[idx++] = v.toFloat();
            }
            float alpha = qBound(0.f, vals[2], 1.f);
            for (auto& out : outputs) out = vals[0] + (vals[1] - vals[0]) * alpha;
        };
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Math.Float.Remap";
        n.displayName = "Remap Range";
        n.category = "Math/Float";
        n.isPureNode = true;
        n.inputs.append(makePin("Value", PinType::Float, true, 0.f));
        n.inputs.append(makePin("In Min", PinType::Float, true, 0.f));
        n.inputs.append(makePin("In Max", PinType::Float, true, 1.f));
        n.inputs.append(makePin("Out Min", PinType::Float, true, 0.f));
        n.inputs.append(makePin("Out Max", PinType::Float, true, 1.f));
        n.outputs.append(makePin("Result", PinType::Float, false));
        n.execute = [](const QUuid&, const QMap<QUuid, QVariant>& inputs,
                       QMap<QUuid, QVariant>& outputs, void*) {
            float vals[5] = {0, 0, 1, 0, 1};
            int idx = 0;
            for (const auto& v : inputs.values()) {
                if (v.typeId() == QMetaType::Float && idx < 5) vals[idx++] = v.toFloat();
            }
            float t = (vals[1] != vals[2]) ? (vals[0] - vals[1]) / (vals[2] - vals[1]) : 0.f;
            for (auto& out : outputs) out = vals[3] + t * (vals[4] - vals[3]);
        };
        registerNodeType(n);
    }

    // ================================================================
    // MATH - Integer
    // ================================================================
    auto registerMathInt = [&](const QString& name, const QString& display,
                               std::function<int(int, int)> op) {
        BlueprintNodeType n;
        n.typeName = name;
        n.displayName = display;
        n.category = "Math/Integer";
        n.isPureNode = true;
        n.inputs.append(makePin("A", PinType::Int, true, 0));
        n.inputs.append(makePin("B", PinType::Int, true, 0));
        n.outputs.append(makePin("Result", PinType::Int, false));
        n.execute = [op](const QUuid&, const QMap<QUuid, QVariant>& inputs,
                         QMap<QUuid, QVariant>& outputs, void*) {
            int a = 0, b = 0;
            for (const auto& v : inputs.values()) {
                if (v.typeId() == QMetaType::Int) {
                    if (a == 0 && b == 0) a = v.toInt();
                    else b = v.toInt();
                }
            }
            for (auto& out : outputs) out = op(a, b);
        };
        registerNodeType(n);
    };

    registerMathInt("Math.Int.Add", "Add +", [](int a, int b){ return a + b; });
    registerMathInt("Math.Int.Subtract", "Subtract -", [](int a, int b){ return a - b; });
    registerMathInt("Math.Int.Multiply", "Multiply *", [](int a, int b){ return a * b; });
    registerMathInt("Math.Int.Divide", "Divide /", [](int a, int b){ return b != 0 ? a / b : 0; });
    registerMathInt("Math.Int.Modulo", "Modulo %", [](int a, int b){ return b != 0 ? a % b : 0; });
    registerMathInt("Math.Int.Min", "Min", [](int a, int b){ return std::min(a, b); });
    registerMathInt("Math.Int.Max", "Max", [](int a, int b){ return std::max(a, b); });

    // ================================================================
    // MATH - Boolean
    // ================================================================
    {
        BlueprintNodeType n;
        n.typeName = "Math.Bool.And";
        n.displayName = "AND";
        n.category = "Math/Boolean";
        n.isPureNode = true;
        n.inputs.append(makePin("A", PinType::Bool, true));
        n.inputs.append(makePin("B", PinType::Bool, true));
        n.outputs.append(makePin("Result", PinType::Bool, false));
        n.execute = [](const QUuid&, const QMap<QUuid, QVariant>& inputs,
                       QMap<QUuid, QVariant>& outputs, void*) {
            bool a = false, b = false;
            for (const auto& v : inputs.values()) {
                if (v.typeId() == QMetaType::Bool) {
                    if (!a) a = v.toBool(); else b = v.toBool();
                }
            }
            for (auto& out : outputs) out = a && b;
        };
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Math.Bool.Or";
        n.displayName = "OR";
        n.category = "Math/Boolean";
        n.isPureNode = true;
        n.inputs.append(makePin("A", PinType::Bool, true));
        n.inputs.append(makePin("B", PinType::Bool, true));
        n.outputs.append(makePin("Result", PinType::Bool, false));
        n.execute = [](const QUuid&, const QMap<QUuid, QVariant>& inputs,
                       QMap<QUuid, QVariant>& outputs, void*) {
            bool a = false, b = false;
            for (const auto& v : inputs.values()) {
                if (v.typeId() == QMetaType::Bool) {
                    if (!a) a = v.toBool(); else b = v.toBool();
                }
            }
            for (auto& out : outputs) out = a || b;
        };
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Math.Bool.Not";
        n.displayName = "NOT";
        n.category = "Math/Boolean";
        n.isPureNode = true;
        n.inputs.append(makePin("A", PinType::Bool, true));
        n.outputs.append(makePin("Result", PinType::Bool, false));
        n.execute = [](const QUuid&, const QMap<QUuid, QVariant>& inputs,
                       QMap<QUuid, QVariant>& outputs, void*) {
            bool a = false;
            for (const auto& v : inputs.values()) {
                if (v.typeId() == QMetaType::Bool) { a = v.toBool(); break; }
            }
            for (auto& out : outputs) out = !a;
        };
        registerNodeType(n);
    }

    // ================================================================
    // MATH - Compare
    // ================================================================
    auto registerCompare = [&](const QString& name, const QString& display,
                               std::function<bool(float, float)> op) {
        BlueprintNodeType n;
        n.typeName = name;
        n.displayName = display;
        n.category = "Math/Compare";
        n.isPureNode = true;
        n.inputs.append(makePin("A", PinType::Float, true, 0.f));
        n.inputs.append(makePin("B", PinType::Float, true, 0.f));
        n.outputs.append(makePin("Result", PinType::Bool, false));
        n.execute = [op](const QUuid&, const QMap<QUuid, QVariant>& inputs,
                         QMap<QUuid, QVariant>& outputs, void*) {
            float a = 0, b = 0;
            for (const auto& v : inputs.values()) {
                if (v.typeId() == QMetaType::Float) {
                    if (a == 0 && b == 0) a = v.toFloat();
                    else b = v.toFloat();
                }
            }
            for (auto& out : outputs) out = op(a, b);
        };
        registerNodeType(n);
    };

    registerCompare("Math.Compare.Equal", "==", [](float a, float b){ return a == b; });
    registerCompare("Math.Compare.NotEqual", "!=", [](float a, float b){ return a != b; });
    registerCompare("Math.Compare.Greater", ">", [](float a, float b){ return a > b; });
    registerCompare("Math.Compare.GreaterEqual", ">=", [](float a, float b){ return a >= b; });
    registerCompare("Math.Compare.Less", "<", [](float a, float b){ return a < b; });
    registerCompare("Math.Compare.LessEqual", "<=", [](float a, float b){ return a <= b; });

    // ================================================================
    // VECTOR
    // ================================================================
    {
        BlueprintNodeType n;
        n.typeName = "Vector.MakeVector";
        n.displayName = "Make Vector";
        n.category = "Vector";
        n.isPureNode = true;
        n.inputs.append(makePin("X", PinType::Float, true, 0.f));
        n.inputs.append(makePin("Y", PinType::Float, true, 0.f));
        n.inputs.append(makePin("Z", PinType::Float, true, 0.f));
        n.outputs.append(makePin("Vector", PinType::Vector, false));
        n.execute = [](const QUuid&, const QMap<QUuid, QVariant>& inputs,
                       QMap<QUuid, QVariant>& outputs, void*) {
            float x = 0, y = 0, z = 0;
            int idx = 0;
            for (const auto& v : inputs.values()) {
                if (v.typeId() == QMetaType::Float) {
                    if (idx == 0) x = v.toFloat();
                    else if (idx == 1) y = v.toFloat();
                    else z = v.toFloat();
                    idx++;
                }
            }
            for (auto& out : outputs) out = QVariant::fromValue(QVector3D(x, y, z));
        };
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Vector.BreakVector";
        n.displayName = "Break Vector";
        n.category = "Vector";
        n.isPureNode = true;
        n.inputs.append(makePin("Vector", PinType::Vector, true));
        n.outputs.append(makePin("X", PinType::Float, false));
        n.outputs.append(makePin("Y", PinType::Float, false));
        n.outputs.append(makePin("Z", PinType::Float, false));
        n.execute = [](const QUuid&, const QMap<QUuid, QVariant>& inputs,
                       QMap<QUuid, QVariant>& outputs, void*) {
            QVector3D v;
            for (const auto& val : inputs.values()) {
                if (val.canConvert<QVector3D>()) { v = val.value<QVector3D>(); break; }
            }
            int idx = 0;
            for (auto& out : outputs) {
                if (idx == 0) out = v.x();
                else if (idx == 1) out = v.y();
                else out = v.z();
                idx++;
            }
        };
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Vector.Add";
        n.displayName = "Vector +";
        n.category = "Vector";
        n.isPureNode = true;
        n.inputs.append(makePin("A", PinType::Vector, true));
        n.inputs.append(makePin("B", PinType::Vector, true));
        n.outputs.append(makePin("Result", PinType::Vector, false));
        n.execute = [](const QUuid&, const QMap<QUuid, QVariant>& inputs,
                       QMap<QUuid, QVariant>& outputs, void*) {
            QVector3D a, b;
            int idx = 0;
            for (const auto& v : inputs.values()) {
                if (v.canConvert<QVector3D>()) {
                    if (idx == 0) a = v.value<QVector3D>();
                    else b = v.value<QVector3D>();
                    idx++;
                }
            }
            for (auto& out : outputs) out = QVariant::fromValue(a + b);
        };
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Vector.Scale";
        n.displayName = "Vector * Float";
        n.category = "Vector";
        n.isPureNode = true;
        n.inputs.append(makePin("Vector", PinType::Vector, true));
        n.inputs.append(makePin("Scale", PinType::Float, true, 1.f));
        n.outputs.append(makePin("Result", PinType::Vector, false));
        n.execute = [](const QUuid&, const QMap<QUuid, QVariant>& inputs,
                       QMap<QUuid, QVariant>& outputs, void*) {
            QVector3D v; float s = 1;
            for (const auto& val : inputs.values()) {
                if (val.canConvert<QVector3D>()) v = val.value<QVector3D>();
                else if (val.typeId() == QMetaType::Float) s = val.toFloat();
            }
            for (auto& out : outputs) out = QVariant::fromValue(v * s);
        };
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Vector.Length";
        n.displayName = "Vector Length";
        n.category = "Vector";
        n.isPureNode = true;
        n.inputs.append(makePin("Vector", PinType::Vector, true));
        n.outputs.append(makePin("Length", PinType::Float, false));
        n.execute = [](const QUuid&, const QMap<QUuid, QVariant>& inputs,
                       QMap<QUuid, QVariant>& outputs, void*) {
            QVector3D v;
            for (const auto& val : inputs.values()) {
                if (val.canConvert<QVector3D>()) { v = val.value<QVector3D>(); break; }
            }
            for (auto& out : outputs) out = v.length();
        };
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Vector.Normalize";
        n.displayName = "Normalize";
        n.category = "Vector";
        n.isPureNode = true;
        n.inputs.append(makePin("Vector", PinType::Vector, true));
        n.outputs.append(makePin("Result", PinType::Vector, false));
        n.execute = [](const QUuid&, const QMap<QUuid, QVariant>& inputs,
                       QMap<QUuid, QVariant>& outputs, void*) {
            QVector3D v;
            for (const auto& val : inputs.values()) {
                if (val.canConvert<QVector3D>()) { v = val.value<QVector3D>(); break; }
            }
            for (auto& out : outputs) out = QVariant::fromValue(v.normalized());
        };
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Vector.Distance";
        n.displayName = "Distance";
        n.category = "Vector";
        n.isPureNode = true;
        n.inputs.append(makePin("A", PinType::Vector, true));
        n.inputs.append(makePin("B", PinType::Vector, true));
        n.outputs.append(makePin("Distance", PinType::Float, false));
        n.execute = [](const QUuid&, const QMap<QUuid, QVariant>& inputs,
                       QMap<QUuid, QVariant>& outputs, void*) {
            QVector3D a, b;
            int idx = 0;
            for (const auto& v : inputs.values()) {
                if (v.canConvert<QVector3D>()) {
                    if (idx == 0) a = v.value<QVector3D>();
                    else b = v.value<QVector3D>();
                    idx++;
                }
            }
            for (auto& out : outputs) out = (a - b).length();
        };
        registerNodeType(n);
    }

    // ================================================================
    // VARIABLES
    // ================================================================
    {
        BlueprintNodeType n;
        n.typeName = "Variable.Get";
        n.displayName = "Get Variable";
        n.category = "Variables";
        n.description = "Get the value of a variable";
        n.isPureNode = true;
        n.outputs.append(makePin("Value", PinType::Wildcard, false));
        n.defaultProperties["variableName"] = QString();
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Variable.Set";
        n.displayName = "Set Variable";
        n.category = "Variables";
        n.description = "Set the value of a variable";
        n.inputs.append(makeExecPin("Exec", true));
        n.inputs.append(makePin("Value", PinType::Wildcard, true));
        n.outputs.append(makeExecPin("Then", false));
        n.outputs.append(makePin("Value", PinType::Wildcard, false));
        n.defaultProperties["variableName"] = QString();
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Variable.LocalGet";
        n.displayName = "Get Local";
        n.category = "Variables";
        n.isPureNode = true;
        n.outputs.append(makePin("Value", PinType::Wildcard, false));
        n.defaultProperties["variableName"] = QString();
        n.defaultProperties["localType"] = static_cast<int>(PinType::Float);
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Variable.LocalSet";
        n.displayName = "Set Local";
        n.category = "Variables";
        n.inputs.append(makeExecPin("Exec", true));
        n.inputs.append(makePin("Value", PinType::Wildcard, true));
        n.outputs.append(makeExecPin("Then", false));
        n.defaultProperties["variableName"] = QString();
        n.defaultProperties["localType"] = static_cast<int>(PinType::Float);
        registerNodeType(n);
    }

    // ================================================================
    // CONSTANTS
    // ================================================================
    {
        BlueprintNodeType n;
        n.typeName = "Const.Float";
        n.displayName = "Float";
        n.category = "Constants";
        n.isPureNode = true;
        n.isConstNode = true;
        n.outputs.append(makePin("Value", PinType::Float, false, 0.f));
        n.defaultProperties["value"] = 0.f;
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Const.Int";
        n.displayName = "Integer";
        n.category = "Constants";
        n.isPureNode = true;
        n.isConstNode = true;
        n.outputs.append(makePin("Value", PinType::Int, false, 0));
        n.defaultProperties["value"] = 0;
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Const.Bool";
        n.displayName = "Boolean";
        n.category = "Constants";
        n.isPureNode = true;
        n.isConstNode = true;
        n.outputs.append(makePin("Value", PinType::Bool, false));
        n.defaultProperties["value"] = false;
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Const.String";
        n.displayName = "String";
        n.category = "Constants";
        n.isPureNode = true;
        n.isConstNode = true;
        n.outputs.append(makePin("Value", PinType::String, false, QString()));
        n.defaultProperties["value"] = QString();
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Const.Vector";
        n.displayName = "Vector";
        n.category = "Constants";
        n.isPureNode = true;
        n.isConstNode = true;
        n.outputs.append(makePin("Value", PinType::Vector, false, QVariant::fromValue(QVector3D())));
        n.defaultProperties["value"] = QVariant::fromValue(QVector3D());
        registerNodeType(n);
    }

    // ================================================================
    // STRING
    // ================================================================
    {
        BlueprintNodeType n;
        n.typeName = "String.Concat";
        n.displayName = "Concat";
        n.category = "String";
        n.isPureNode = true;
        n.inputs.append(makePin("A", PinType::String, true, QString()));
        n.inputs.append(makePin("B", PinType::String, true, QString()));
        n.outputs.append(makePin("Result", PinType::String, false));
        n.execute = [](const QUuid&, const QMap<QUuid, QVariant>& inputs,
                       QMap<QUuid, QVariant>& outputs, void*) {
            QString a, b;
            for (const auto& v : inputs.values()) {
                if (v.typeId() == QMetaType::QString) {
                    if (a.isEmpty()) a = v.toString(); else b = v.toString();
                }
            }
            for (auto& out : outputs) out = a + b;
        };
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "String.Length";
        n.displayName = "Length";
        n.category = "String";
        n.isPureNode = true;
        n.inputs.append(makePin("String", PinType::String, true));
        n.outputs.append(makePin("Length", PinType::Int, false));
        n.execute = [](const QUuid&, const QMap<QUuid, QVariant>& inputs,
                       QMap<QUuid, QVariant>& outputs, void*) {
            QString s;
            for (const auto& v : inputs.values()) {
                if (v.typeId() == QMetaType::QString) { s = v.toString(); break; }
            }
            for (auto& out : outputs) out = s.length();
        };
        registerNodeType(n);
    }

    // ================================================================
    // MATH - Trigonometry
    // ================================================================
    {
        BlueprintNodeType n;
        n.typeName = "Math.Sin";
        n.displayName = "Sin";
        n.category = "Math/Trig";
        n.isPureNode = true;
        n.inputs.append(makePin("Angle", PinType::Float, true, 0.f));
        n.outputs.append(makePin("Result", PinType::Float, false));
        n.execute = [](const QUuid&, const QMap<QUuid, QVariant>& inputs,
                       QMap<QUuid, QVariant>& outputs, void*) {
            float a = 0;
            for (const auto& v : inputs.values()) {
                if (v.typeId() == QMetaType::Float) { a = v.toFloat(); break; }
            }
            for (auto& out : outputs) out = std::sin(a);
        };
        registerNodeType(n);
    }
    {
        BlueprintNodeType n;
        n.typeName = "Math.Cos";
        n.displayName = "Cos";
        n.category = "Math/Trig";
        n.isPureNode = true;
        n.inputs.append(makePin("Angle", PinType::Float, true, 0.f));
        n.outputs.append(makePin("Result", PinType::Float, false));
        n.execute = [](const QUuid&, const QMap<QUuid, QVariant>& inputs,
                       QMap<QUuid, QVariant>& outputs, void*) {
            float a = 0;
            for (const auto& v : inputs.values()) {
                if (v.typeId() == QMetaType::Float) { a = v.toFloat(); break; }
            }
            for (auto& out : outputs) out = std::cos(a);
        };
        registerNodeType(n);
    }

    // ================================================================
    // COLOR
    // ================================================================
    {
        BlueprintNodeType n;
        n.typeName = "Color.MakeColor";
        n.displayName = "Make Color";
        n.category = "Color";
        n.isPureNode = true;
        n.inputs.append(makePin("R", PinType::Float, true, 1.f));
        n.inputs.append(makePin("G", PinType::Float, true, 1.f));
        n.inputs.append(makePin("B", PinType::Float, true, 1.f));
        n.inputs.append(makePin("A", PinType::Float, true, 1.f));
        n.outputs.append(makePin("Color", PinType::Color, false, QColor(255,255,255)));
        n.execute = [](const QUuid&, const QMap<QUuid, QVariant>& inputs,
                       QMap<QUuid, QVariant>& outputs, void*) {
            float vals[4] = {1,1,1,1};
            int idx = 0;
            for (const auto& v : inputs.values()) {
                if (v.typeId() == QMetaType::Float && idx < 4) vals[idx++] = v.toFloat();
            }
            QColor c;
            c.setRgbF(qBound(0.0, (double)vals[0], 1.0),
                      qBound(0.0, (double)vals[1], 1.0),
                      qBound(0.0, (double)vals[2], 1.0),
                      qBound(0.0, (double)vals[3], 1.0));
            for (auto& out : outputs) out = QVariant::fromValue(c);
        };
        registerNodeType(n);
    }
}

// ============================================================================
// Library singleton
// ============================================================================
BlueprintNodeLibrary& BlueprintNodeLibrary::instance()
{
    static BlueprintNodeLibrary lib;
    static bool initialized = false;
    if (!initialized) {
        lib.registerBuiltInNodes();
        initialized = true;
    }
    return lib;
}

void BlueprintNodeLibrary::registerNodeType(const BlueprintNodeType& nodeType)
{
    m_nodeTypes[nodeType.typeName] = nodeType;
}

bool BlueprintNodeLibrary::hasNodeType(const QString& typeName) const
{
    return m_nodeTypes.contains(typeName);
}

const BlueprintNodeType& BlueprintNodeLibrary::getNodeType(const QString& typeName) const
{
    return m_nodeTypes[typeName];
}

QVector<BlueprintNodeType> BlueprintNodeLibrary::getAllNodeTypes() const
{
    QVector<BlueprintNodeType> types;
    for (auto it = m_nodeTypes.begin(); it != m_nodeTypes.end(); ++it)
        types.append(it.value());
    return types;
}

QVector<BlueprintNodeType> BlueprintNodeLibrary::getNodeTypesByCategory(const QString& category) const
{
    QVector<BlueprintNodeType> types;
    for (auto it = m_nodeTypes.begin(); it != m_nodeTypes.end(); ++it) {
        if (it.value().category == category || it.value().category.startsWith(category + "/"))
            types.append(it.value());
    }
    return types;
}

QStringList BlueprintNodeLibrary::getCategories() const
{
    QSet<QString> cats;
    for (auto it = m_nodeTypes.begin(); it != m_nodeTypes.end(); ++it)
        cats.insert(it.value().category);
    return cats.values();
}

ui::GraphNode BlueprintNodeLibrary::createNode(const QString& typeName, const QPointF& position)
{
    if (!m_nodeTypes.contains(typeName))
        return ui::GraphNode();

    const BlueprintNodeType& nodeType = m_nodeTypes[typeName];

    ui::GraphNode node;
    node.id = QUuid::createUuid();
    node.typeName = typeName;
    node.title = nodeType.displayName;
    node.position = position;
    node.properties = nodeType.defaultProperties;
    node.headerColor = nodeType.isEventNode ? QColor(180, 40, 40) :
                        nodeType.isPureNode ? QColor(40, 140, 80) :
                        QColor(40, 80, 160);

    for (const auto& pin : nodeType.inputs)
        node.inputPortIds.append(pin.id);
    for (const auto& pin : nodeType.outputs)
        node.outputPortIds.append(pin.id);

    return node;
}

}} // namespace ks::blueprint
