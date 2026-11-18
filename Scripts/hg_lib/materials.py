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
        # Recreate from scratch: delete_all_material_expressions() leaves custom outputs such as the
        # SingleLayerWater node behind, and a second one breaks compilation.
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            unreal.EditorAssetLibrary.delete_asset(path)
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
        # Normal maps -> Normal sampler; the noise texture is TC_Masks (linear) -> Masks sampler.
        sampler = (unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if normal
                   else unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        s = self.node(unreal.MaterialExpressionTextureSample, texture=tex, sampler_type=sampler)
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
    """Landscape matched to reference photos of Ma Pi Leng / Nho Que / Tham Ma (dry season):
    * canyon slopes: dry mauve-khaki grass with patches of green shrubs and bare terraced corn fields;
    * west (Doc Tham Ma side): lush green hillside;
    * steep faces (> ~57 deg): grey limestone with orange-rust and dark rain streaks;
    * a bare ochre band 0-4.5 m above the reservoir level (the "bathtub ring" of the Nho Que lake)."""
    g = Graph("M_HG_Terrain")
    wp = g.world_pos()
    x = g.mask(wp, r=True)
    z = g.mask(wp, b=True)
    n_far = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 3200.0))
    n_mid = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 700.0))
    n_fine = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 110.0))
    rock_n = g.texture(tex["T_HG_RockNormal"], g.world_uv(wp, 420.0), normal=True)
    up = g.up_facing()

    # 1 = lush west (Tham Ma), ~0.3 = dry canyon east.
    lush = g.lerp(g.scalar("CanyonGreenness", 0.5), 1.0,
                  g.saturate(g.mul(g.sub(g.scalar("LushWestX", -20000.0), x), 1.0 / 90000.0)))

    dry = g.lerp(g.color("DryGrass", (0.2, 0.13, 0.115)), g.color("DryKhaki", (0.2, 0.155, 0.095)), (n_mid, "G"))
    green = g.lerp(g.color("Shrub", (0.015, 0.04, 0.012)), g.color("ShrubLight", (0.045, 0.085, 0.025)), (n_fine, "G"))
    green_amt = g.saturate(g.add(g.add(g.sub(g.mul(lush, 1.5), 0.62), g.mul(g.sub((n_far, "R"), 0.5), 1.3)),
                                 g.mul(g.sub((n_fine, "R"), 0.5), 0.7)))
    # Shrub speckle (3-8 m clumps): the dotted look of the dry Nho Que slopes, readable from far away.
    n_dots = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 900.0))
    dots = g.saturate(g.mul(g.sub((n_dots, "G"), g.sub(0.6, g.mul(lush, 0.2))), 9.0))
    n_dots2 = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 330.0))
    dots = g.saturate(g.add(dots, g.mul(g.saturate(g.mul(g.sub((n_dots2, "R"), 0.62), 9.0)), 0.8)))
    # Greener lower slopes near the water (photos): +shrubs below ~800 m.
    low = g.saturate(g.mul(g.sub(80000.0, z), 1.0 / 8000.0))
    green_amt = g.saturate(g.add(g.add(green_amt, dots), g.mul(low, 0.35)))
    veg = g.lerp(dry, green, green_amt)

    # Bare terraced corn plots on gentler ground (striped by contour lines).
    flat = remap_mask(g, up, 0.82, 8.0)
    plots = g.mul(g.mul(g.saturate(g.mul(g.sub((n_far, "B"), 0.6), 6.0)), flat), g.sub(1.0, g.mul(lush, 0.8)))
    stripe = g.node(unreal.MaterialExpressionFrac)
    g.link(g.mul(z, 1.0 / 260.0), stripe)
    soil = g.mul(g.color("Soil", (0.27, 0.16, 0.095)), g.lerp(0.75, 1.1, stripe))
    ground = g.lerp(veg, soil, plots)

    # Limestone cliffs with rust and dark streaks running down the face.
    xy = g.add(g.mask(wp, r=True), g.mask(wp, g=True))
    streak = g.texture(tex["T_HG_Noise"], g.append(g.mul(xy, 1.0 / 900.0), g.mul(z, 1.0 / 5000.0)))
    lime = g.mul(g.color("Limestone", (0.3, 0.29, 0.27)), g.add(0.7, g.mul((n_fine, "R"), 0.45)))
    rust = g.saturate(g.mul(g.sub((streak, "G"), 0.62), 5.0))
    dark = g.saturate(g.mul(g.sub((streak, "R"), 0.6), 4.0))
    rock_col = g.lerp(g.lerp(lime, g.color("Rust", (0.46, 0.2, 0.08)), rust), g.color("Stain", (0.09, 0.085, 0.075)), g.mul(dark, 0.7))
    # Even steep canyon slopes are grassed in the photos: bare rock only above ~68 deg.
    threshold = g.add(g.scalar("RockSlopeThreshold", 0.38), g.mul(g.sub((n_mid, "R"), 0.5), 0.2))
    rock = remap_mask(g, up, threshold, 6.0, invert=True)
    base = g.lerp(ground, rock_col, rock)

    # Reservoir draw-down band: bare ochre rock/soil from the water line up ~4.5 m.
    band = g.mul(g.saturate(g.mul(g.sub(g.scalar("BandTopZ", 70700.0), z), 1.0 / 150.0)),
                 g.saturate(g.mul(g.sub(z, 69950.0), 1.0 / 60.0)))
    base = g.lerp(base, g.mul(g.color("WaterlineOchre", (0.42, 0.27, 0.13)), g.add(0.8, g.mul((n_fine, "G"), 0.4))), band)

    # Footpaths (pale zig-zag trails, baked by gen_scatter.py into a 2 m/px map-wide mask).
    if "T_HG_PathMask" in tex:
        map_uv = g.mul(g.add(g.mask(wp, r=True, g=True), 201600.0), 1.0 / 403200.0)
        path = g.mul(g.texture(tex["T_HG_PathMask"], map_uv), g.sub(1.0, rock))
        base = g.lerp(base, g.color("Path", (0.3, 0.24, 0.17)), g.mul(path, 0.85))
    rough = g.lerp(g.lerp(0.92, 0.95, plots), 0.78, rock)
    normal = g.normalize(g.lerp(g.const3((0.0, 0.0, 1.0)), rock_n, g.add(g.mul(rock, 0.6), 0.4)))
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

    puddle = g.saturate(g.mul(g.sub((n, "B"), 0.35), g.scalar("PuddleAmount", 0.6)))   # a few damp patches, not blotches
    asphalt = g.mul(g.color("Asphalt", (0.05, 0.05, 0.055)), g.add(0.75, g.mul((fine, "G"), 0.5)))
    base = g.lerp(asphalt, g.color("PuddleColor", (0.018, 0.018, 0.022)), puddle)
    rough = g.lerp(g.add(g.scalar("DryRoughness", 0.5), g.mul((fine, "R"), 0.25)), g.scalar("WetRoughness", 0.05), puddle)
    flat = g.const3((0.0, 0.0, 1.0))
    normal = g.normalize(g.lerp(agg_n, flat, g.add(g.mul(puddle, 0.6), 0.35)))

    # Lane paint from the road-strip UVs (U across, V along each 8 m segment):
    # yellow dashed centre line (3.4 m dash / 4.6 m gap) + solid white edge lines.
    tc = g.node(unreal.MaterialExpressionTextureCoordinate)
    u = g.mask(tc, r=True)
    v = g.mask(tc, g=True)
    du = g.node(unreal.MaterialExpressionAbs)
    g.link(g.sub(u, 0.5), du)
    centre = g.saturate(g.mul(g.sub(0.013, du), 400.0))
    dash = g.saturate(g.mul(g.sub(0.42, v), 200.0))
    yellow = g.mul(centre, dash)
    de = g.node(unreal.MaterialExpressionAbs)
    g.link(g.sub(du, 0.455), de)
    edge = g.saturate(g.mul(g.sub(0.012, de), 400.0))
    paint_wear = g.saturate(g.add(0.55, (fine, "B")))
    base = g.lerp(base, g.color("LaneYellow", (0.62, 0.42, 0.03)), g.mul(yellow, paint_wear))
    base = g.lerp(base, g.color("LaneWhite", (0.62, 0.62, 0.6)), g.mul(edge, paint_wear))
    rough = g.lerp(rough, 0.55, g.saturate(g.add(yellow, edge)))
    g.out(base, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(rough, unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.const(0.5), unreal.MaterialProperty.MP_SPECULAR)
    g.out(normal, unreal.MaterialProperty.MP_NORMAL)
    return g.finish()


def river_water(tex) -> unreal.Material:
    """Nho Que reservoir water: opaque milky turquoise with calm ripples (matches the photos, and is
    cheaper than SingleLayerWater on the M1's SM5 path). Lumen reflections give the sky/cliff mirror."""
    g = Graph("M_HG_RiverWater")
    wp = g.world_pos()
    time = g.node(unreal.MaterialExpressionTime)

    def panned(size, sx, sy):
        p = g.node(unreal.MaterialExpressionPanner, speed_x=sx, speed_y=sy)
        g.link(g.world_uv(wp, size), p, "Coordinate")
        g.link(time, p, "Time")
        return g.texture(tex["T_HG_WaterNormal"], p, normal=True)

    n1 = panned(2200.0, 0.004, 0.009)
    n2 = panned(700.0, -0.011, 0.003)
    normal = g.normalize(g.lerp(g.const3((0.0, 0.0, 1.0)), g.add(n1, n2), g.scalar("WaveStrength", 0.22)))
    tint = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 6000.0))
    col = g.lerp(g.color("Turquoise", (0.012, 0.12, 0.11)), g.color("TurquoiseLight", (0.02, 0.16, 0.14)), (tint, "R"))
    g.out(col, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.scalar("Roughness", 0.09), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.const(0.3), unreal.MaterialProperty.MP_SPECULAR)
    g.out(normal, unreal.MaterialProperty.MP_NORMAL)
    return g.finish()


def karst_rock(tex) -> unreal.Material:
    """Gorge cliff faces: grey limestone, orange-rust and dark streaks, shrubs on ledges."""
    g = Graph("M_HG_KarstRock")
    wp = g.world_pos()
    z = g.mask(wp, b=True)
    uv = g.mul(g.node(unreal.MaterialExpressionTextureCoordinate), g.node(unreal.MaterialExpressionConstant2Vector, r=6.0, g=3.0))
    rock_n = g.texture(tex["T_HG_RockNormal"], uv, normal=True)
    xy = g.add(g.mask(wp, r=True), g.mask(wp, g=True))
    streak = g.texture(tex["T_HG_Noise"], g.append(g.mul(xy, 1.0 / 800.0), g.mul(z, 1.0 / 4500.0)))
    blotch = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 900.0))
    lime = g.mul(g.color("Limestone", (0.32, 0.31, 0.29)), g.add(0.75, g.mul((blotch, "G"), 0.4)))
    rust = g.saturate(g.mul(g.sub((streak, "G"), 0.6), 5.0))
    dark = g.saturate(g.mul(g.sub((streak, "R"), 0.58), 4.0))
    col = g.lerp(g.lerp(lime, g.color("Rust", (0.48, 0.21, 0.08)), rust), g.color("Stain", (0.08, 0.075, 0.065)), g.mul(dark, 0.75))
    up = g.up_facing()
    shrub = g.mul(remap_mask(g, up, 0.6, 4.0), g.saturate(g.mul((blotch, "R"), 1.5)))
    col = g.lerp(col, g.color("Shrub", (0.035, 0.07, 0.025)), shrub)
    g.out(col, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.lerp(0.8, 0.92, shrub), unreal.MaterialProperty.MP_ROUGHNESS)
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


def foliage(tex, name, dark, light, rough=0.85, two_sided=False):
    """Leaf / flower clumps: colour varies per instance and with world noise, light normal detail."""
    g = Graph(name)
    if two_sided:
        g.mat.set_editor_property("two_sided", True)
    wp = g.world_pos()
    n = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 120.0))
    rnd = g.node(unreal.MaterialExpressionPerInstanceRandom)
    col = g.lerp(g.color("Dark", dark), g.color("Light", light), g.saturate(g.add(g.mul(rnd, 0.6), g.mul((n, "G"), 0.6))))
    g.out(col, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.const(rough), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.texture(tex["T_HG_RockNormal"], g.world_uv(wp, 50.0), normal=True), unreal.MaterialProperty.MP_NORMAL)
    return g.finish()


def bark(tex, name, color):
    g = Graph(name)
    wp = g.world_pos()
    n = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 40.0))
    g.out(g.mul(g.color("Bark", color), g.add(0.75, g.mul((n, "R"), 0.5))), unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.const(0.9), unreal.MaterialProperty.MP_ROUGHNESS)
    return g.finish()


def metal(name, color, roughness):
    g = Graph(name)
    g.out(g.color("Color", color), unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.const(1.0), unreal.MaterialProperty.MP_METALLIC)
    g.out(g.const(roughness), unreal.MaterialProperty.MP_ROUGHNESS)
    return g.finish()


def stone(tex):
    g = Graph("M_HG_StoneWall")
    wp = g.world_pos()
    n = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 60.0))
    col = g.lerp(g.color("Stone", (0.2, 0.2, 0.19)), g.color("Moss", (0.06, 0.08, 0.04)), g.saturate(g.mul(g.sub((n, "R"), 0.55), 3.0)))
    g.out(g.mul(col, g.add(0.75, g.mul((n, "G"), 0.5))), unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.const(0.88), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.texture(tex["T_HG_RockNormal"], g.world_uv(wp, 80.0), normal=True), unreal.MaterialProperty.MP_NORMAL)
    return g.finish()


def chevron():
    """Black curve-warning board with three white '>' arrows painted from the UVs."""
    g = Graph("M_HG_Chevron")
    tc = g.node(unreal.MaterialExpressionTextureCoordinate)
    u = g.mask(tc, r=True)
    v = g.mask(tc, g=True)
    fu = g.node(unreal.MaterialExpressionFrac)
    g.link(g.mul(u, 3.0), fu)
    dv = g.node(unreal.MaterialExpressionAbs)
    g.link(g.sub(v, 0.5), dv)
    d = g.node(unreal.MaterialExpressionAbs)
    g.link(g.sub(g.sub(fu, 0.35), g.mul(dv, 0.6)), d)
    stripe = g.saturate(g.mul(g.sub(0.12, d), 20.0))
    g.out(g.lerp(g.color("Board", (0.02, 0.02, 0.02)), g.color("Arrow", (0.75, 0.75, 0.72)), stripe), unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.const(0.5), unreal.MaterialProperty.MP_ROUGHNESS)
    return g.finish()


def build_all(tex) -> dict:
    return {
        "terrain": terrain(tex),
        "asphalt": wet_asphalt(tex),
        "water": river_water(tex),
        "rock": karst_rock(tex),
        "bush": bush(tex),
        "post": road_post(),
        "kapok_bark": bark(tex, "M_HG_KapokBark", (0.42, 0.4, 0.37)),
        "bark": bark(tex, "M_HG_Bark", (0.12, 0.09, 0.07)),
        "kapok_bloom": foliage(tex, "M_HG_KapokBloom", (0.38, 0.025, 0.02), (0.6, 0.06, 0.04), 0.7),
        "leaf": foliage(tex, "M_HG_Leaf", (0.03, 0.06, 0.02), (0.08, 0.12, 0.035)),
        "pine": foliage(tex, "M_HG_PineNeedles", (0.018, 0.04, 0.02), (0.04, 0.075, 0.03)),
        "guardrail": metal("M_HG_Guardrail", (0.55, 0.56, 0.57), 0.38),
        "stone": stone(tex),
        "flowers_yellow": foliage(tex, "M_HG_FlowersYellow", (0.55, 0.42, 0.02), (0.75, 0.62, 0.08), 0.7),
        "flowers_white": foliage(tex, "M_HG_FlowersWhite", (0.6, 0.62, 0.55), (0.8, 0.8, 0.75), 0.7),
        "chevron": chevron(),
        "pole": bark(tex, "M_HG_Concrete", (0.38, 0.37, 0.35)),
        "house_wall": bark(tex, "M_HG_HouseWall", (0.3, 0.25, 0.2)),
        "roof_grey": metal("M_HG_RoofGrey", (0.32, 0.33, 0.34), 0.5),
        "roof_green": metal("M_HG_RoofGreen", (0.06, 0.24, 0.13), 0.55),
        "roof_blue": metal("M_HG_RoofBlue", (0.07, 0.17, 0.34), 0.55),
        "ranges": foliage(tex, "M_HG_DistantRanges", (0.05, 0.08, 0.05), (0.09, 0.12, 0.08), 0.95, two_sided=True),
    }
