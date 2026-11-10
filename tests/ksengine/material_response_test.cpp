// Qt-free test for the rules that turn a KN5 material's own properties into
// the numbers the renderer consumes (src/engine/Graphics/MaterialResponse.h):
// ksSpecular -> dielectric F0 scale (and its GBuffer coverage encoding),
// detailUVMultiplier -> detail tiling, and the pane classification that gives
// authored glass its Fresnel curve.
//
// The fixtures are the values the Benetton B191 really carries, read with
// `kn5baker --materials`: WCCARBODY (paint, specular 0.2), B191_CARBFBD
// (carbon, 0.1, detailUVMultiplier 25), B191_CWIND (reflection shader, blend,
// fresnelC 0.2 / EXP 15 / Max 0.4).

#include "KsTest.h"
#include "engine/Graphics/MaterialResponse.h"

#include <cmath>
#include <cstring>
#include <string>

namespace {

using ks::engine::fileformat::Kn5Material;
using ks::engine::fileformat::Kn5Property;
using namespace ks::graphics;

Kn5Property floatProperty(const std::string& name, float value) {
    Kn5Property property;
    property.name = name;
    std::memcpy(property.value.data(), &value, sizeof(value));
    return property;
}

bool near(float actual, float expected, float tolerance = 1e-5f) {
    return std::fabs(actual - expected) <= tolerance;
}

} // namespace

int main() {
    // --- nothing authored: AC's neutral, the legacy 0.04 F0 ---------------
    {
        Kn5Material material;
        material.name = "MAIN_PAINT";
        material.shader = "ksPerPixelMultiMap_damage_dirt";
        const AuthoredSurface surface = authoredSurface(material);
        KS_CHECK(near(surface.specular, 0.5f));
        KS_CHECK(near(surface.fresnelC, 0.0f));
        KS_CHECK(near(surface.fresnelExp, 5.0f));
        KS_CHECK(near(surface.fresnelMax, 0.0f));
        KS_CHECK(near(f0Scale(surface.specular), 1.0f));
        // The magnitude packs ksSpecular and the authored fresnel cap: the
        // neutral 0.5 lands on level 8 and "no authored cap" is level 0, so a
        // legacy material stores 1 + 136/272 and both values decode back.
        KS_CHECK(near(coverageMagnitude(surface.specular, surface.fresnelMax), 1.5f));
        KS_CHECK(near(specularFromCoverage(1.5f), 0.5f));
        KS_CHECK(near(fresnelMaxFromCoverage(1.5f), 0.0f));
        KS_CHECK(near(authoredDetailTiles(material), 0.0f));
        KS_CHECK(!isGlassPane(material.shader, false, surface.fresnelMax, 0));
    }

    // --- a *_Ref map is the car's reflection probe, not a colour ----------
    {
        KS_CHECK(isReflectionProbeTexture("HDR_111_Parking_Lot_2_Ref.dds"));
        KS_CHECK(isReflectionProbeTexture("hdr_111_parking_lot_2_ref.dds"));
        KS_CHECK(isReflectionProbeTexture("showroom_reflection.png"));
        KS_CHECK(isReflectionProbeTexture("sky_ref_01.dds"));
        // A surface that merely mentions a reference is not a probe.
        KS_CHECK(!isReflectionProbeTexture("countach_misc.dds"));
        KS_CHECK(!isReflectionProbeTexture("metal_detail_rim.dds"));
        KS_CHECK(!isReflectionProbeTexture("skin_00.dds"));
        KS_CHECK(!isReflectionProbeTexture("Carpet.DDS"));
    }

    // --- an OCC map is an occlusion bake, not a colour ---------------------
    {
        KS_CHECK(isOcclusionTexture("INT_Seat_OCC.dds"));
        KS_CHECK(isOcclusionTexture("INT_Cockpit_OCC+.dds"));
        KS_CHECK(isOcclusionTexture("int_steer_occ.dds"));
        KS_CHECK(!isOcclusionTexture("leather.dds"));
        KS_CHECK(!isOcclusionTexture("Carpet.DDS"));
    }

    // --- ksSpecular scales F0 around 0.5 (WCCARBODY = 0.2) ----------------
    {
        Kn5Material material;
        material.name = "WCCARBODY";
        material.shader = "ksPerPixelMultiMap_damage_dirt";
        material.properties.push_back(floatProperty("ksSpecular", 0.2f));
        const AuthoredSurface surface = authoredSurface(material);
        KS_CHECK(near(surface.specular, 0.2f));
        KS_CHECK(near(f0Scale(surface.specular), 0.4f));
        // A full-strength specular doubles the legacy F0.
        KS_CHECK(near(f0Scale(1.0f), 2.0f));
        // Broken values are clamped, never propagated.
        KS_CHECK(near(f0Scale(0.0f), 0.4f));
        KS_CHECK(near(f0Scale(7.0f), 2.0f));
        KS_CHECK(near(f0Scale(-1.0f), 0.4f));
    }

    // --- the GBuffer carries it in the coverage magnitude, sign preserved -
    {
        const float values[] = {0.0f, 0.1f, 0.2f, 0.5f, 0.75f, 1.0f};
        for (float specular : values) {
            const float stored = coverageMagnitude(specular);
            // Two 4-bit levels in one float channel: the magnitude stays just
            // above 1, far from the abs(w) < 0.5 "no geometry" test, and
            // decodes to the level the value quantised to (1/16 of a
            // reflectance, which no surface can show).
            KS_CHECK(stored >= 1.0f && stored <= 2.1f);
            const float quantised = std::floor(specular * 16.0f + 0.5f) / 16.0f;
            KS_CHECK(near(specularFromCoverage(stored), quantised, 1e-4f));
            // The signed form (brake disc) decodes to the same value.
            KS_CHECK(near(specularFromCoverage(-stored), quantised, 1e-4f));
        }
        // Out-of-range input is clamped, not wrapped.
        KS_CHECK(near(specularFromCoverage(coverageMagnitude(-2.0f)), 0.0f));
        KS_CHECK(near(specularFromCoverage(coverageMagnitude(3.0f)), 1.0f));
        // The authored fresnel cap rides along and comes back: 0.625 is a level
        // (10/16), which is where the Countach's authored 0.60 quantises, so
        // its body reflects the environment instead of the legacy 0.04.
        const float authored = coverageMagnitude(1.0f, 0.625f);
        KS_CHECK(near(specularFromCoverage(authored), 1.0f, 1e-4f));
        KS_CHECK(near(fresnelMaxFromCoverage(authored), 0.625f, 1e-4f));
        // A material that authored no cap keeps the split-sum reflection.
        KS_CHECK(near(fresnelMaxFromCoverage(coverageMagnitude(0.5f)), 0.0f));
    }

    // --- detailUVMultiplier is the authored name (B191 carbon = 25) -------
    {
        Kn5Material carbon;
        carbon.properties.push_back(floatProperty("detailUVMultiplier", 25.0f));
        KS_CHECK(near(authoredDetailTiles(carbon), 25.0f));

        // The old guessed name still works when the authored one is absent.
        Kn5Material legacy;
        legacy.properties.push_back(floatProperty("ksDetailU", 3.0f));
        KS_CHECK(near(authoredDetailTiles(legacy), 3.0f));

        // The authored name wins when both are present.
        Kn5Material both;
        both.properties.push_back(floatProperty("detailUVMultiplier", 50.0f));
        both.properties.push_back(floatProperty("ksDetailU", 2.0f));
        KS_CHECK(near(authoredDetailTiles(both), 50.0f));

        // Zero or negative tiling is "none authored", not a valid repeat.
        Kn5Material zero;
        zero.properties.push_back(floatProperty("detailUVMultiplier", 0.0f));
        zero.properties.push_back(floatProperty("ksDetailU", -4.0f));
        KS_CHECK(near(authoredDetailTiles(zero), 0.0f));
    }

    // --- the windscreen is a pane because the file authored a Fresnel cap -
    {
        Kn5Material windscreen;
        windscreen.name = "B191_CWIND";
        windscreen.shader = "ksPerPixelReflection";
        windscreen.blend = 1;
        windscreen.properties.push_back(floatProperty("ksSpecular", 1.0f));
        windscreen.properties.push_back(floatProperty("fresnelC", 0.2f));
        windscreen.properties.push_back(floatProperty("fresnelEXP", 15.0f));
        windscreen.properties.push_back(floatProperty("fresnelMaxLevel", 0.4f));
        const AuthoredSurface surface = authoredSurface(windscreen);
        KS_CHECK(near(surface.specular, 1.0f));
        KS_CHECK(near(surface.fresnelC, 0.2f));
        KS_CHECK(near(surface.fresnelExp, 15.0f));
        KS_CHECK(near(surface.fresnelMax, 0.4f));
        KS_CHECK(near(f0Scale(surface.specular), 2.0f));
        KS_CHECK(isGlassPane(windscreen.shader, false, surface.fresnelMax,
                             windscreen.blend));
    }

    // --- classification boundaries ---------------------------------------
    {
        // Preset glass wins regardless of shader or flags.
        KS_CHECK(isGlassPane("ksPerPixel", true, 0.0f, 0));
        // A decal blends but is neither glass nor reflective.
        KS_CHECK(!isGlassPane("ksPerPixel", false, 0.4f, 1));
        // Reflection shader without an authored Fresnel cap stays a decal.
        KS_CHECK(!isGlassPane("ksPerPixelReflection", false, 0.0f, 1));
        // A reflective shader that is not blended is an opaque surface.
        KS_CHECK(!isGlassPane("ksPerPixelReflection", false, 0.4f, 0));
        // Mesh names and shaders are matched lowercased in the file.
        KS_CHECK(isGlassPane("KS_PER_PIXEL_REFLECTION", false, 0.4f, 1));
        KS_CHECK(isGlassPane("ksWindscreen", false, 0.2f, 1));
        // Carbon is opaque, even with authored Fresnel values.
        KS_CHECK(!isGlassPane("ksPerPixelMultiMap_NMDetail", false, 0.01f, 0));
    }

    // --- a broken fresnel exponent falls back to the AC default -----------
    {
        Kn5Material broken;
        broken.properties.push_back(floatProperty("fresnelEXP", 0.0f));
        broken.properties.push_back(floatProperty("fresnelC", 0.3f));
        const AuthoredSurface surface = authoredSurface(broken);
        KS_CHECK(near(surface.fresnelExp, 5.0f));
        KS_CHECK(near(surface.fresnelC, 0.3f));
    }

    // --- weathering maps are data, never the surface (WCCARBODY names its
    //     damage normal txNormal=damage.dds: that is what looked dented) ----
    {
        KS_CHECK(isWeatheringTexture("damage.dds"));
        KS_CHECK(isWeatheringTexture("scratches.dds"));
        KS_CHECK(isWeatheringTexture("dust.dds"));
        KS_CHECK(isWeatheringTexture("9x_tread_dirty.dds"));
        KS_CHECK(!isWeatheringTexture("asr-car-body.dds"));
        // "nodamage" is the clean map Kunos ships as the Esprit body diffuse;
        // the word "damage" inside it must not hide it.
        KS_CHECK(!isWeatheringTexture("nodamage_lod0.dds"));
        KS_CHECK(!isWeatheringTexture("no_damage.dds"));
        KS_CHECK(textureRole("txDiffuse", "nodamage_lod0.dds") == TextureRole::diffuse);

        KS_CHECK(textureRole("txNormal", "damage.dds") == TextureRole::other);
        KS_CHECK(textureRole("txNormal", "body_nm.dds") == TextureRole::normal);
        KS_CHECK(textureRole("txDiffuse", "asr-car-body.dds") == TextureRole::diffuse);
        KS_CHECK(textureRole("txNormalDetail", "weave_nm.dds") == TextureRole::normal);
        KS_CHECK(textureRole("txDust", "dust.dds") == TextureRole::other);
    }

    // --- spec/AO/normal maps in txDetail are data, not colour (the B191
    //     ships 9x_carbfbd_T1_s.dds there: multiplying it into the albedo
    //     painted blue speckles on a clean livery) ------------------------
    {
        KS_CHECK(isDataTexture("9x_carbfbd_T1_s.dds"));
        KS_CHECK(isDataTexture("mat_steer_metal_t2_spec.dds"));
        KS_CHECK(isDataTexture("9x_metal_ao.dds"));
        KS_CHECK(isDataTexture("9x_alcnt_nm.dds"));
        KS_CHECK(isDataTexture("panel_normal.dds"));
        KS_CHECK(!isDataTexture("metal_detail.dds"));
        KS_CHECK(!isDataTexture("9x_carbfbd_t1.dds"));

        KS_CHECK(textureRole("txDetail", "9x_carbfbd_T1_s.dds") == TextureRole::other);
        KS_CHECK(textureRole("txDetail", "9x_metal_ao.dds") == TextureRole::other);
        KS_CHECK(textureRole("txDetail", "metal_detail.dds") == TextureRole::detail);
        // A normal mapping keeps its role even when the texture is a normal.
        KS_CHECK(textureRole("txNormalDetail", "weave_nm.dds") == TextureRole::normal);
    }

    // --- ksDiffuse is a scalar in AC, not a colour: reading the three raw
    //     floats as RGB turned untextured parts dark red ---------------------
    {
        float out[3] = {0.f, 0.f, 0.f};
        const float scalar[3] = {0.4f, 0.0f, 0.0f};
        diffuseFactors(scalar, out);
        KS_CHECK(near(out[0], 0.4f) && near(out[1], 0.4f) && near(out[2], 0.4f));

        const float colour[3] = {0.8f, 0.1f, 0.2f};
        diffuseFactors(colour, out);
        KS_CHECK(near(out[0], 0.8f) && near(out[1], 0.1f) && near(out[2], 0.2f));
    }

    // --- txDetail only when AC's useDetail asks for it ---------------------
    {
        Kn5Material body;
        body.properties.push_back(floatProperty("useDetail", 0.0f));
        KS_CHECK(!usesDetail(body));

        Kn5Material carbon;
        carbon.properties.push_back(floatProperty("useDetail", 1.0f));
        KS_CHECK(usesDetail(carbon));

        // A file that never wrote the flag keeps the historical behaviour.
        Kn5Material legacy;
        KS_CHECK(usesDetail(legacy));
    }

    return 0;
}
