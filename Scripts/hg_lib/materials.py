"""Photoreal-ish materials built node-by-node with MaterialEditingLibrary (CP03).

No Megascans needed: the graphs use the procedural textures from gen_assets.py
(world-aligned noise, limestone normal, water normal). Every look parameter is a
Scalar/Vector parameter, so material instances can art-direct them later.
"""
from __future__ import annotations

import unreal

from . import editor_utils as hg

MEL = unreal.MaterialEditingLibrary
FOLDER = f"{hg.GENERATED_ROOT}/Materials"


class Graph:
    """Tiny helper around MaterialEditingLibrary for readable node graphs."""

    def __init__(self, name: str):
        path = f"{FOLDER}/{name}"
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            self.mat = unreal.EditorAssetLibrary.load_asset(path)
            MEL.delete_all_material_expressions(self.mat)
        else:
            self.mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
        self.x = -1600
        self.y = 0

    def node(self, cls, **props):
        e = MEL.create_material_expression(self.mat, cls, self.x, self.y)
        self.y += 120
        if self.y > 2400:
            self.y = 0
            self.x += 300
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    # -- leaf nodes ------------------------------------------------------
    def scalar(self, name, value):
        return self.node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=float(value))

    def color(self, name, rgb):
        return self.node(unreal.MaterialExpressionVectorParameter, parameter_name=name,
                         default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))

    def const(self, value):
        return self.node(unreal.MaterialExpressionConstant, r=float(value))

    def const3(self, rgb):
        return self.node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))

    def texture(self, tex, uv, normal=False, out=""):
        s = self.node(unreal.MaterialExpressionTextureSample, texture=tex,
                      sampler_type=(unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if normal
                                    else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR))
        self.link(uv, s, "UVs")
        return s

    # -- math --------------------------------------------------------------
    def link(self, src, dst, dst_input="", src_output=""):
        if isinstance(src, tuple):
            src, src_output = src
        if not MEL.connect_material_expressions(src, src_output, dst, dst_input):
            hg.warn(f"[{self.mat.get_name()}] could not connect {src.get_name()}.{src_output} -> {dst.get_name()}.{dst_input}")

    def binary(self, cls, a, b):
        e = self.node(cls)
        for pin, v in (("A", a), ("B", b)):
            if isinstance(v, (int, float)):
                e.set_editor_property("const_" + pin.lower(), float(v))
            else:
                self.link(v, e, pin)
        return e

    def mul(self, a, b):
        return self.binary(unreal.MaterialExpressionMultiply, a, b)

    def add(self, a, b):
        return self.binary(unreal.MaterialExpressionAdd, a, b)

    def sub(self, a, b):
        return self.binary(unreal.MaterialExpressionSubtract, a, b)

    def lerp(self, a, b, alpha):
        e = self.node(unreal.MaterialExpressionLinearInterpolate)
        for pin, v in (("A", a), ("B", b), ("Alpha", alpha)):
            if isinstance(v, (int, float)):
                e.set_editor_property("const_" + pin.lower(), float(v))
            else:
                self.link(v, e, pin)
        return e

    def saturate(self, a):
        e = self.node(unreal.MaterialExpressionSaturate)
        self.link(a, e)
        return e

    def mask(self, a, r=False, g=False, b=False, al=False):
        e = self.node(unreal.MaterialExpressionComponentMask, r=r, g=g, b=b, a=al)
        self.link(a, e)
        return e

    def append(self, a, b):
        e = self.node(unreal.MaterialExpressionAppendVector)
        self.link(a, e, "A")
        self.link(b, e, "B")
        return e

    def normalize(self, a):
        e = self.node(unreal.MaterialExpressionNormalize)
        self.link(a, e)
        return e

    # -- common inputs -----------------------------------------------------
    def world_pos(self):
        return self.node(unreal.MaterialExpressionWorldPosition)

    def world_uv(self, wp, size_cm):
        """World XY / size -> UV (top projection)."""
        return self.mul(self.mask(wp, r=True, g=True), 1.0 / size_cm)

    def up_facing(self):
        return self.mask(self.node(unreal.MaterialExpressionVertexNormalWS), b=True)

    # -- outputs -----------------------------------------------------------
    def out(self, src, prop, src_output=""):
        if isinstance(src, tuple):
            src, src_output = src
        MEL.connect_material_property(src, src_output, prop)

    def finish(self):
        MEL.layout_material_expressions(self.mat)
        MEL.recompile_material(self.mat)
        unreal.EditorAssetLibrary.save_loaded_asset(self.mat)
        return self.mat


def remap_mask(g: Graph, value, center, sharpness, invert=False):
    """saturate((value - center) * sharpness), optionally inverted."""
    d = g.sub(center, value) if invert else g.sub(value, center)
    return g.saturate(g.mul(d, sharpness))


# ---------------------------------------------------------------------------
# Materials
# ---------------------------------------------------------------------------

def terrain(tex) -> unreal.Material:
    """Auto-slope landscape: grass/scrub on gentle ground, karst limestone + moss on steep faces,
    dark wet banks near the river. World-aligned, no layer painting needed."""
    g = Graph("M_HG_Terrain")
    wp = g.world_pos()
    n_far = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 2600.0))
    n_mid = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 420.0))
    rock_n = g.texture(tex["T_HG_RockNormal"], g.world_uv(wp, 380.0), normal=True)

    up = g.up_facing()
    # Rock only on genuinely steep ground (>~50 deg) plus noisy outcrops: Ha Giang hills are mostly
    # green with grey limestone breaking through, not bare white cones.
    threshold = g.add(g.scalar("RockSlopeThreshold", 0.64), g.mul(g.sub((n_far, "R"), 0.5), 0.3))
    outcrop = g.saturate(g.mul(g.sub((n_mid, "B"), 0.55), 3.0))
    rock = g.saturate(g.add(remap_mask(g, up, threshold, 6.0, invert=True), g.mul(outcrop, 0.6)))

    # Green scrub with dry-grass / maize-field patches (autumn on the Dong Van plateau).
    dry = g.saturate(g.mul(g.sub((n_far, "G"), 0.55), 4.0))
    grass_tint = g.lerp(g.color("GrassDark", (0.04, 0.07, 0.022)), g.color("GrassLight", (0.075, 0.1, 0.03)), (n_mid, "G"))
    grass = g.lerp(grass_tint, g.color("GrassDry", (0.14, 0.12, 0.055)), dry)
    lime = g.lerp(g.color("Limestone", (0.44, 0.43, 0.40)), g.color("LimestoneDark", (0.16, 0.16, 0.15)), (n_mid, "R"))
    moss_amt = g.mul(g.saturate(g.sub(g.mul((n_far, "R"), 1.6), 0.45)), g.scalar("MossOnRock", 0.55))
    rock_col = g.lerp(lime, g.color("Moss", (0.05, 0.09, 0.035)), moss_amt)
    base = g.lerp(grass, rock_col, rock)

    # Wet, darker banks within ~15 m above the river surface (700 m).
    z = g.mask(wp, b=True)
    wet = g.saturate(g.mul(g.sub(g.scalar("WetAboveZ", 71500.0), z), 1.0 / 1500.0))
    base = g.mul(base, g.lerp(1.0, 0.5, wet))
    rough = g.lerp(g.lerp(0.9, 0.74, rock), 0.32, wet)

    flat = g.const3((0.0, 0.0, 1.0))
    normal = g.normalize(g.lerp(flat, rock_n, g.add(g.mul(rock, 0.7), 0.3)))

    g.out(base, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(rough, unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(normal, unreal.MaterialProperty.MP_NORMAL)
    return g.finish()


def wet_asphalt(tex) -> unreal.Material:
    """Dew-wet asphalt: dark aggregate, glossy puddles that mirror the golden-hour sun."""
    g = Graph("M_HG_WetAsphalt")
    wp = g.world_pos()
    n = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 320.0))
    fine = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 45.0))
    agg_n = g.texture(tex["T_HG_RockNormal"], g.world_uv(wp, 70.0), normal=True)

    puddle = g.saturate(g.mul((n, "B"), g.scalar("PuddleAmount", 1.0)))
    asphalt = g.mul(g.color("Asphalt", (0.05, 0.05, 0.055)), g.add(0.75, g.mul((fine, "G"), 0.5)))
    base = g.lerp(asphalt, g.color("PuddleColor", (0.018, 0.018, 0.022)), puddle)
    rough = g.lerp(g.add(g.scalar("DryRoughness", 0.5), g.mul((fine, "R"), 0.25)), g.scalar("WetRoughness", 0.05), puddle)
    flat = g.const3((0.0, 0.0, 1.0))
    normal = g.normalize(g.lerp(agg_n, flat, g.add(g.mul(puddle, 0.6), 0.35)))

    g.out(base, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(rough, unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.const(0.5), unreal.MaterialProperty.MP_SPECULAR)
    g.out(normal, unreal.MaterialProperty.MP_NORMAL)
    return g.finish()


def river_water(tex) -> unreal.Material:
    """SingleLayerWater, emerald Nho Que: low red scattering, strong red absorption."""
    g = Graph("M_HG_RiverWater")
    g.mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_SINGLE_LAYER_WATER)
    g.mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    wp = g.world_pos()
    time = g.node(unreal.MaterialExpressionTime)

    def panned(size, sx, sy):
        p = g.node(unreal.MaterialExpressionPanner, speed_x=sx, speed_y=sy)
        g.link(g.world_uv(wp, size), p, "Coordinate")
        g.link(time, p, "Time")
        return g.texture(tex["T_HG_WaterNormal"], p, normal=True)

    n1 = panned(1800.0, 0.006, 0.011)
    n2 = panned(650.0, -0.013, 0.004)
    flat = g.const3((0.0, 0.0, 1.0))
    normal = g.normalize(g.lerp(flat, g.add(n1, n2), g.scalar("WaveStrength", 0.45)))

    g.out(g.const3((0.0, 0.0, 0.0)), unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.scalar("Roughness", 0.035), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(normal, unreal.MaterialProperty.MP_NORMAL)

    water_out = g.node(unreal.MaterialExpressionSingleLayerWaterMaterialOutput)
    g.link(g.color("ScatteringCoefficients", (0.015, 0.11, 0.075)), water_out, "ScatteringCoefficients")
    g.link(g.color("AbsorptionCoefficients", (0.42, 0.055, 0.10)), water_out, "AbsorptionCoefficients")
    g.link(g.scalar("PhaseG", 0.2), water_out, "PhaseG")
    return g.finish()


def karst_rock(tex) -> unreal.Material:
    """Limestone for the Nanite cliffs: grey with vertical rain-streak staining and moss on ledges."""
    g = Graph("M_HG_KarstRock")
    wp = g.world_pos()
    uv = g.mul(g.node(unreal.MaterialExpressionTextureCoordinate), g.node(unreal.MaterialExpressionConstant2Vector, r=6.0, g=3.0))
    rock_n = g.texture(tex["T_HG_RockNormal"], uv, normal=True)
    # Streak UV: horizontal = world X+Y (fast), vertical = world Z (slow) -> long vertical stains.
    xy = g.add(g.mask(wp, r=True), g.mask(wp, g=True))
    streak_uv = g.append(g.mul(xy, 1.0 / 700.0), g.mul(g.mask(wp, b=True), 1.0 / 4000.0))
    streak = g.texture(tex["T_HG_Noise"], streak_uv)
    blotch = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 900.0))

    lime = g.lerp(g.color("Limestone", (0.50, 0.49, 0.46)), g.color("Stain", (0.12, 0.115, 0.10)),
                  g.saturate(g.sub(g.mul((streak, "G"), 1.8), 0.55)))
    up = g.up_facing()
    moss = g.mul(remap_mask(g, up, 0.55, 4.0), g.saturate(g.mul((blotch, "R"), 1.4)))
    base = g.lerp(lime, g.color("Moss", (0.05, 0.09, 0.03)), moss)
    rough = g.lerp(g.scalar("RockRoughness", 0.78), 0.92, moss)

    g.out(base, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(rough, unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(rock_n, unreal.MaterialProperty.MP_NORMAL)
    return g.finish()


def bush(tex) -> unreal.Material:
    g = Graph("M_HG_Bush")
    wp = g.world_pos()
    n = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 150.0))
    rnd = g.node(unreal.MaterialExpressionPerInstanceRandom)
    col = g.lerp(g.color("LeafDark", (0.02, 0.05, 0.015)), g.color("LeafLight", (0.09, 0.13, 0.03)),
                 g.saturate(g.add(g.mul(rnd, 0.6), g.mul((n, "G"), 0.6))))
    g.out(col, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.const(0.85), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.texture(tex["T_HG_RockNormal"], g.world_uv(wp, 60.0), normal=True), unreal.MaterialProperty.MP_NORMAL)
    return g.finish()


def road_post() -> unreal.Material:
    """Faded white concrete post with a red top band (V = height along the post)."""
    g = Graph("M_HG_RoadPost")
    v = g.mask(g.node(unreal.MaterialExpressionTextureCoordinate), g=True)
    band = remap_mask(g, v, g.scalar("RedBandStart", 0.74), 300.0)
    dirt = remap_mask(g, v, 0.28, 6.0, invert=True)
    base = g.lerp(g.color("Paint", (0.72, 0.71, 0.66)), g.color("Red", (0.55, 0.03, 0.02)), band)
    base = g.lerp(base, g.color("Dirt", (0.22, 0.2, 0.17)), g.mul(dirt, 0.7))
    g.out(base, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.const(0.62), unreal.MaterialProperty.MP_ROUGHNESS)
    return g.finish()


def build_all(tex) -> dict:
    return {
        "terrain": terrain(tex),
        "asphalt": wet_asphalt(tex),
        "water": river_water(tex),
        "rock": karst_rock(tex),
        "bush": bush(tex),
        "post": road_post(),
    }
