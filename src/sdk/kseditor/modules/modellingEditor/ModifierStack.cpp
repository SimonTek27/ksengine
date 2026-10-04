#include "ModifierStack.h"
#include "sdk/kseditor/engine/mesh/ModifierSystem.h"
#include "AdditionalModifiers.h"

namespace ks {

namespace {

// Factory mapping a stack modifier type string to a concrete Modifier
// implementation. Kept in sync with MeshModifier::applyModifiers().
ModifierPtr createModifierFromType(const QString& type) {
    if (type == "Mirror")           return ModifierPtr(new MirrorModifier());
    if (type == "Array")            return ModifierPtr(new ArrayModifier());
    if (type == "Bevel")            return ModifierPtr(new BevelModifier());
    if (type == "Solidify")         return ModifierPtr(new SolidifyModifier());
    if (type == "Subdivision")      return ModifierPtr(new SubdivisionModifier());
    if (type == "Decimate")         return ModifierPtr(new DecimateModifier());
    if (type == "Displace")         return ModifierPtr(new DisplaceModifier());
    if (type == "Smooth")           return ModifierPtr(new SmoothModifier());
    if (type == "Cast")             return ModifierPtr(new CastModifier());
    if (type == "Triangulate")      return ModifierPtr(new TriangulateModifier());
    if (type == "Wireframe")        return ModifierPtr(new WireframeModifier());
    if (type == "Remesh")           return ModifierPtr(new RemeshModifier());
    if (type == "Skin")             return ModifierPtr(new SkinModifier());
    if (type == "Shrinkwrap")       return ModifierPtr(new ShrinkwrapModifier());
    if (type == "CageDeform")       return ModifierPtr(new CageDeformModifier());
    if (type == "LatticeEx")        return ModifierPtr(new LatticeExModifier());
    if (type == "SimpleDeform")     return ModifierPtr(new SimpleDeformModifier());
    if (type == "Curve")            return ModifierPtr(new CurveModifier());
    if (type == "CorrectiveSmooth") return ModifierPtr(new CorrectiveSmoothModifier());
    if (type == "UVProject")        return ModifierPtr(new UVProjectModifier());
    if (type == "Weld")             return ModifierPtr(new WeldModifier());
    if (type == "LaplacianSmooth")  return ModifierPtr(new LaplacianSmoothModifier());
    if (type == "SurfaceSmooth")    return ModifierPtr(new SurfaceSmoothModifier());
    if (type == "VolumeSmooth")     return ModifierPtr(new VolumeSmoothModifier());
    if (type == "Taper")            return ModifierPtr(new TaperModifier());
    if (type == "Ripple")           return ModifierPtr(new RippleModifier());
    if (type == "Noise")            return ModifierPtr(new NoiseModifier());
    if (type == "Push")             return ModifierPtr(new PushModifier());
    if (type == "Relax")            return ModifierPtr(new RelaxModifier());
    if (type == "Melt")             return ModifierPtr(new MeltModifier());
    if (type == "Lathe")            return ModifierPtr(new LatheModifier());
    if (type == "SmoothingGroups")  return ModifierPtr(new SmoothingGroupsModifier());
    if (type == "Offset")           return ModifierPtr(new OffsetModifier());
    if (type == "UVResolveOverlaps") return ModifierPtr(new UVResolveOverlapsModifier());
    if (type == "Bevel")            return ModifierPtr(new BevelModifierEx());
    return nullptr;
}

} // anonymous namespace

ModifierStack::ModifierStack(QObject* parent)
    : QObject(parent)
{
}

bool ModifierStack::add(const QString& type)
{
    if (!createModifierFromType(type))
        return false;
    StackModifier sm;
    sm.type = type;
    m_mods.append(sm);
    emit changed();
    return true;
}

bool ModifierStack::remove(int index)
{
    if (index < 0 || index >= m_mods.size())
        return false;
    m_mods.removeAt(index);
    emit changed();
    return true;
}

bool ModifierStack::move(int fromIndex, int toIndex)
{
    if (fromIndex < 0 || fromIndex >= m_mods.size())
        return false;
    if (toIndex < 0 || toIndex >= m_mods.size())
        return false;
    if (fromIndex == toIndex)
        return true;
    m_mods.move(fromIndex, toIndex);
    emit changed();
    return true;
}

bool ModifierStack::setEnabled(int index, bool enabled)
{
    if (index < 0 || index >= m_mods.size())
        return false;
    if (m_mods[index].enabled == enabled)
        return false;
    m_mods[index].enabled = enabled;
    emit changed();
    return true;
}

bool ModifierStack::setParam(int index, const QString& name, const QVariant& value)
{
    if (index < 0 || index >= m_mods.size())
        return false;
    m_mods[index].params.insert(name, QJsonValue::fromVariant(value));
    emit changed();
    return true;
}

void ModifierStack::clear()
{
    if (m_mods.isEmpty())
        return;
    m_mods.clear();
    emit changed();
}

MeshData ModifierStack::evaluate() const
{
    MeshData data = m_base;
    for (const auto& sm : m_mods) {
        if (!sm.enabled)
            continue;
        ModifierPtr mod = createModifierFromType(sm.type);
        if (!mod)
            continue;

        QMap<QString, QVariant> params;
        for (auto it = sm.params.constBegin(); it != sm.params.constEnd(); ++it)
            params.insert(it.key(), it.value().toVariant());
        mod->readParameters(params);

        if (mod->canApply(data))
            data = mod->apply(data);
    }
    return data;
}

} // namespace ks
