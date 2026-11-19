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

    def scan(self, tex, uv, kind="diff"):
        """Round 7B: Poly Haven scan sample on a shared wrap sampler (Metal SM5 has 16 sampler slots; the landscape
        material needs more textures than that). kind: diff (sRGB colour), nor (normal map), rough (linear)."""
        st = {"diff": unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
              "nor": unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL}.get(kind, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        s = self.node(unreal.MaterialExpressionTextureSample, texture=tex, sampler_type=st)
        try:
            s.set_editor_property("sampler_source", unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
        except Exception as exc:
            hg.warn(f"shared sampler: {exc}")
        self.link(uv, s, "UVs")
        return s

    def scan_biplanar(self, tex, wp, size_cm, steep, kind="diff"):
        top = self.scan(tex, self.world_uv(wp, size_cm), kind)
        side_uv = self.mul(self.append(self.add(self.mask(wp, r=True), self.mask(wp, g=True)), self.mask(wp, b=True)), 1.0 / size_cm)
        return self.lerp(top, self.scan(tex, side_uv, kind), steep)

    def lum(self, rgb):
        d = self.node(unreal.MaterialExpressionDotProduct)
        self.link(self.mask(rgb, r=True, g=True, b=True), d, "A")
        self.link(self.const3((0.3, 0.59, 0.11)), d, "B")
        return d

    def cam_far(self, wp, near_cm, far_cm):
        """0 near the camera -> 1 beyond far_cm (distance tiling breakup)."""
        cam = self.node(unreal.MaterialExpressionCameraPositionWS)
        d = self.node(unreal.MaterialExpressionDistance)
        self.link(cam, d, "A")
        self.link(wp, d, "B")
        return self.saturate(self.mul(self.sub(d, near_cm), 1.0 / (far_cm - near_cm)))

    def scanned(self, palette, scan_rgb, mean, mix, gain_name, gain=(1.0, 1.0, 1.0)):
        """Palette colour carrying the scan's detail (luminance / mean), mixed with the scan's own colour."""
        detail = self.mul(palette, self.mul(self.lum(scan_rgb), 1.0 / mean))
        own = self.mul(self.mask(scan_rgb, r=True, g=True, b=True), self.color(gain_name, gain))
        return self.lerp(detail, own, mix)

    # -- math --------------------------------------------------------------
    def link(self, src, dst, dst_input="", src_output=""):
        if isinstance(src, tuple):
            src, src_output = src
        # Only texture samples expose R/G/B/A pins; for other nodes insert a component mask.
        if src_output in ("R", "G", "B", "A") and not isinstance(src, unreal.MaterialExpressionTextureSample):
            m = self.node(unreal.MaterialExpressionComponentMask, r=src_output == "R", g=src_output == "G",
                          b=src_output == "B", a=src_output == "A")
            MEL.connect_material_expressions(src, "", m, "")
            src, src_output = m, ""
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

    def biplanar(self, tex, wp, size_cm, steep, normal=False):
        """Top projection on flat ground, side projection (X+Y, Z) on steep faces - no stretching on cliffs."""
        top = self.texture(tex, self.world_uv(wp, size_cm), normal=normal)
        side_uv = self.mul(self.append(self.add(self.mask(wp, r=True), self.mask(wp, g=True)), self.mask(wp, b=True)), 1.0 / size_cm)
        side = self.texture(tex, side_uv, normal=normal)
        return self.lerp(top, side, steep)

    def up_facing(self):
        return self.mask(self.node(unreal.MaterialExpressionVertexNormalWS), b=True)

    # -- outputs -----------------------------------------------------------
    def out(self, src, prop, src_output=""):
        if isinstance(src, tuple):
            src, src_output = src
        MEL.connect_material_property(src, src_output, prop)

    def finish(self):
        # The editor adds missing usage flags on the fly; the game does not and renders the default
        # checker material instead (roads = spline meshes, scatter = instanced static meshes).
        for flag in ("used_with_instanced_static_meshes", "used_with_spline_meshes"):
            try:
                self.mat.set_editor_property(flag, True)
            except Exception as exc:
                unreal.log_warning(f"{self.mat.get_name()}.{flag}: {exc}")
        MEL.layout_material_expressions(self.mat)
        MEL.recompile_material(self.mat)
        unreal.EditorAssetLibrary.save_loaded_asset(self.mat)
        return self.mat


def road_along(g: Graph, v):
    """Distance along the road in metres: AHGRoadSplineActor gives each segment a MID with its start distance
    (RoadSegStartM) and length (RoadSegLenM); V runs 0..1 along the segment. Markings and slabs then repeat per real
    metre instead of per (slightly varying) segment."""
    def cpd(index, name):
        # Per-segment MID parameters set by AHGRoadSplineActor::RebuildRoadMeshes (index kept for reference).
        return g.scalar(name, 0.0)
    return g.add(cpd(0, "RoadSegStartM"), g.mul(v, cpd(1, "RoadSegLenM")))


def remap_mask(g: Graph, value, center, sharpness, invert=False):
    """saturate((value - center) * sharpness), optionally inverted."""
    d = g.sub(center, value) if invert else g.sub(value, center)
    return g.saturate(g.mul(d, sharpness))


# ---------------------------------------------------------------------------
# Materials
# ---------------------------------------------------------------------------

def terrain(tex, water_z_cm=42000.0, half_cm=201600.0, lush_center_cm=None, lush_radius_cm=90000.0) -> unreal.Material:
    """Landscape matched to reference photos of Ma Pi Leng / Nho Que / Tham Ma (dry season):
    * canyon slopes: dry mauve-khaki grass with patches of green shrubs and bare terraced corn fields;
    * Doc Tham Ma valley (lush_center_cm, the real-terrain patch): forested olive-green hillside;
    * steep faces (> ~57 deg): grey limestone with orange-rust and dark rain streaks;
    * a bare ochre band 0-4.5 m above the reservoir level (the "bathtub ring" of the Nho Que lake)."""
    g = Graph("M_HG_Terrain")
    wp = g.world_pos()
    x = g.mask(wp, r=True)
    z = g.mask(wp, b=True)
    up = g.up_facing()
    steep = remap_mask(g, up, 0.6, 5.0, invert=True)          # 0 on flat ground, 1 on > ~55 deg faces
    n_far = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 3200.0))
    n_mid = g.biplanar(tex["T_HG_Noise"], wp, 700.0, steep)
    n_fine = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 110.0))      # M1 budget: ~7 samples per landscape pixel
    # Round 7B: Poly Haven scans (Scripts/hg_lib/scans.py) where present; every role falls back to the
    # procedural colour. ~8 extra samples on shared samplers.
    scan = tex.get("scan") or {}
    if "rock" in scan and scan["rock"].get("nor") is not None:
        rock_n = g.scan(scan["rock"]["nor"], g.world_uv(wp, 380.0), "nor")
    else:
        rock_n = g.texture(tex["T_HG_RockNormal"], g.world_uv(wp, 420.0), normal=True)
    rock_d = g.scan_biplanar(scan["rock"]["diff"], wp, 380.0, steep) if "rock" in scan else None

    # 1 = lush Tham Ma valley, ~0.5 = dry canyon / karst plateau.
    if lush_center_cm is not None:
        dx = g.sub(x, g.scalar("LushCenterX", lush_center_cm[0]))
        dy = g.sub(g.mask(wp, g=True), g.scalar("LushCenterY", lush_center_cm[1]))
        dist = g.node(unreal.MaterialExpressionSquareRoot)
        g.link(g.add(g.mul(dx, dx), g.mul(dy, dy)), dist)
        lush_w = g.saturate(g.mul(g.sub(g.scalar("LushRadius", lush_radius_cm), dist), 1.0 / 30000.0))
    else:
        lush_w = g.saturate(g.mul(g.sub(g.scalar("LushWestX", -20000.0), x), 1.0 / 90000.0))
    lush = g.lerp(g.scalar("CanyonGreenness", 0.82), 1.0, lush_w)

    dry = g.lerp(g.color("DryGrass", (0.15, 0.12, 0.075)), g.color("DryKhaki", (0.17, 0.15, 0.08)), (n_mid, "G"))
    green = g.lerp(g.color("Shrub", (0.015, 0.04, 0.012)), g.color("ShrubLight", (0.045, 0.085, 0.025)), (n_fine, "G"))
    green_amt = g.saturate(g.add(g.add(g.sub(g.mul(lush, 1.5), 0.62), g.mul(g.sub((n_far, "R"), 0.5), 1.3)),
                                 g.mul(g.sub((n_fine, "R"), 0.5), 0.7)))
    # Shrub speckle (3-8 m clumps): the dotted look of the dry Nho Que slopes, readable from far away.
    # Shrub dots re-use channels already sampled: n_mid.B (7 m clumps) + n_fine.B (1 m specks).
    dots = g.saturate(g.mul(g.sub((n_mid, "B"), g.sub(0.45, g.mul(lush, 0.2))), 2.0))
    dots = g.saturate(g.add(dots, g.mul(g.saturate(g.mul(g.sub((n_fine, "B"), 0.5), 4.0)), 0.5)))
    # Greener lower slopes near the water (photos): +shrubs below ~800 m.
    low = g.saturate(g.mul(g.sub(water_z_cm + 10000.0, z), 1.0 / 8000.0))
    green_amt = g.saturate(g.add(g.add(green_amt, dots), g.mul(low, 0.35)))
    # Round 6B palette (art bible: dry mauve / khaki grass dotted with shrubs, not lawn green).
    dry = g.lerp(dry, g.color("DryMauve", (0.2, 0.13, 0.115)), g.mul(g.saturate(g.mul(g.sub((n_far, "G"), 0.45), 3.0)), 0.6))
    if "grass" in scan:
        # Scanned dry grass: near (3 m) and far (14 m) tiling blended by camera distance, palette-tinted.
        # (budget: one sample at 4.5 m; the n_far / n_mid macro tints already break up the repeat)
        gd = g.scan(scan["grass"]["diff"], g.world_uv(wp, 450.0))
        dry = g.scanned(dry, gd, 0.245, g.scalar("GrassScanMix", 0.6), "GrassScanGain")
        # The shrubby / lush green carries the same scanned grass detail (it read as flat procedural blotches).
        green = g.mul(green, g.lerp(1.0, g.mul(g.lum(gd), 1.0 / 0.245), 0.8))
    if "ground" in scan:
        # Thin rocky soil showing through on the slopes.
        sl = g.saturate(g.mul(g.sub(0.97, up), 4.0))
        gnd = g.scan(scan["ground"]["diff"], g.world_uv(wp, 350.0))
        dry = g.lerp(dry, g.scanned(dry, gnd, 0.204, 0.5, "GroundScanGain"), g.mul(sl, g.scalar("RockySoil", 0.55)))
    veg = g.lerp(dry, green, g.mul(green_amt, g.lerp(0.72, 1.0, lush_w)))

    # Bare terraced corn plots on gentler ground (striped by contour lines).
    flat = remap_mask(g, up, 0.82, 8.0)
    plots = g.mul(g.mul(g.saturate(g.mul(g.sub((n_far, "B"), 0.6), 6.0)), flat), g.sub(1.0, g.mul(lush, 0.8)))
    stripe = g.node(unreal.MaterialExpressionFrac)
    g.link(g.mul(z, 1.0 / 260.0), stripe)
    soil = g.mul(g.color("Soil", (0.28, 0.145, 0.085)), g.lerp(0.75, 1.1, stripe))   # 6B: redder laterite
    mud_d = g.scan(scan["mud"]["diff"], g.world_uv(wp, 280.0)) if "mud" in scan else None
    if mud_d is not None:
        soil = g.scanned(soil, mud_d, 0.119, g.scalar("MudScanMix", 0.35), "MudScanGain")
    ground = g.lerp(veg, soil, plots)

    # Limestone cliffs with rust and dark streaks running down the face.
    xy = g.add(g.mask(wp, r=True), g.mask(wp, g=True))
    streak = g.texture(tex["T_HG_Noise"], g.append(g.mul(xy, 1.0 / 900.0), g.mul(z, 1.0 / 5000.0)))
    lime = g.mul(g.color("Limestone", (0.23, 0.228, 0.222)), g.add(0.7, g.mul((n_fine, "R"), 0.45)))   # 3B: 0.27 read as white sheets
    if rock_d is not None:
        lime = g.scanned(g.color("Limestone", (0.23, 0.228, 0.222)), rock_d, 0.275, g.scalar("RockScanMix", 0.5), "RockScanGain")
    rust = g.mul(g.saturate(g.mul(g.sub((streak, "G"), 0.72), 5.0)), 0.6)   # rust only in patches (photos: grey walls)
    dark = g.saturate(g.mul(g.sub((streak, "R"), 0.6), 4.0))
    # Round 6B: wet seeps - narrow dark green-black streaks where water runs down the limestone.
    seep = g.mul(g.saturate(g.mul(g.sub((streak, "G"), 0.58), 5.0)), g.saturate(g.mul(g.sub(0.45, (streak, "B")), 5.0)))
    rock_col = g.lerp(g.lerp(lime, g.color("Rust", (0.46, 0.2, 0.08)), rust), g.color("Stain", (0.09, 0.085, 0.075)), g.mul(dark, 0.7))
    rock_col = g.lerp(rock_col, g.color("WetSeep", (0.03, 0.036, 0.026)), g.mul(seep, 0.85))
    # Round 3B - big faces read as one flat white sheet: bedding strata (warped horizontal bands, 1.5-5 m),
    # darker vertical crevices / joints, and shrubs clinging to ledges and cracks in streaks.
    warp = g.mul(g.sub((n_mid, "G"), 0.5), 380.0)
    bed = g.node(unreal.MaterialExpressionFrac)
    g.link(g.mul(g.add(z, warp), 1.0 / 340.0), bed)
    bed_d = g.node(unreal.MaterialExpressionAbs)
    g.link(g.sub(bed, 0.5), bed_d)
    bedding = g.saturate(g.mul(g.sub(bed_d, 0.43), 14.0))                          # thin dark bedding planes
    band_tone = g.add(0.82, g.mul(g.sub((streak, "B"), 0.5), 0.5))                    # alternating beds
    rock_col = g.mul(rock_col, g.mul(band_tone, g.sub(1.0, g.mul(bedding, 0.35))))
    # Crevices / ledge shrubs from samples already taken (the fine top projection stretches into vertical
    # streaks on cliffs): no extra texture fetch on the landscape's M1 budget.
    crev = g.saturate(g.mul(g.sub((n_fine, "B"), 0.78), 7.0))
    rock_col = g.lerp(rock_col, g.color("Crevice", (0.045, 0.045, 0.042)), g.mul(crev, 0.75))
    # Round 4B - macro variation (no extra fetch): n_far is a 32 m top projection, so on cliffs it stretches into
    # broad vertical bands - big pale faces become curtains of light grey, warm ochre-grey and dark weathered
    # grey-green (rain-washed and lichen streaks, as in the Tham Ma / Ma Pi Leng photos).
    macro = g.add(0.62, g.mul((n_far, "G"), 0.6))
    rock_col = g.mul(rock_col, macro)
    warm = g.mul(g.saturate(g.mul(g.sub((n_far, "R"), 0.55), 3.0)), 0.55)
    rock_col = g.lerp(rock_col, g.mul(g.color("WarmLime", (0.24, 0.19, 0.13)), g.add(0.7, g.mul((n_fine, "R"), 0.4))), warm)
    weathered = g.mul(g.saturate(g.mul(g.sub(0.45, (n_far, "B")), 3.0)), 0.65)
    rock_col = g.lerp(rock_col, g.color("Weathered", (0.075, 0.08, 0.065)), weathered)
    ledge_veg = g.mul(g.saturate(g.mul(g.sub((n_mid, "B"), 0.55), 5.0)), g.add(bedding, 0.45))
    rock_col = g.lerp(rock_col, g.mul(green, 0.9), g.saturate(ledge_veg))
    # Even steep canyon slopes are grassed in the photos: bare rock only above ~68 deg.
    threshold = g.add(g.scalar("RockSlopeThreshold", 0.62), g.mul(g.sub((n_mid, "R"), 0.5), 0.25))
    rock = remap_mask(g, up, threshold, 6.0, invert=True)
    # Round 6B - the karst look: grey limestone outcrops poke through the thin soil on every slope (they were
    # only on the cliffs, the hills read as smooth grassland): weathered pale tops with yellow-green and black
    # lichen, dark solution joints, and a ring of red-brown earth where the soil thins out around them.
    slope = g.saturate(g.mul(g.sub(0.985, up), 5.0))
    out_n = g.add(g.add(g.mul((n_mid, "R"), 0.62), g.mul((n_fine, "G"), 0.28)), g.mul((n_far, "R"), 0.22))
    # (first pass too timid: specks only) - clustered by the 32 m macro noise, frequent on any slope
    out_th = g.sub(g.sub(0.88, g.mul((n_far, "B"), 0.1)), g.mul(slope, g.mul(0.17, g.sub(1.0, g.mul(lush_w, 0.6)))))
    outcrop = g.saturate(g.mul(g.sub(out_n, out_th), 12.0))
    soil_ring = g.saturate(g.sub(g.saturate(g.mul(g.sub(out_n, g.sub(out_th, 0.045)), 12.0)), outcrop))
    out_col = g.mul(g.color("OutcropLime", (0.215, 0.212, 0.2)), g.add(0.68, g.mul((n_fine, "R"), 0.45)))
    if rock_d is not None:
        out_col = g.scanned(g.color("OutcropLime", (0.215, 0.212, 0.2)), rock_d, 0.275, 0.5, "OutcropScanGain")
    out_col = g.lerp(out_col, g.color("LichenYellow", (0.16, 0.16, 0.08)), g.mul(g.saturate(g.mul(g.sub((n_fine, "B"), 0.62), 4.0)), 0.7))
    out_col = g.lerp(out_col, g.color("LichenBlack", (0.035, 0.035, 0.032)), g.mul(g.saturate(g.mul(g.sub(0.26, (n_fine, "R")), 6.0)), 0.8))
    out_col = g.lerp(out_col, g.color("Joint", (0.03, 0.028, 0.025)), g.mul(crev, 0.9))
    earth = g.mul(g.color("RedEarth", (0.19, 0.095, 0.058)), g.add(0.8, g.mul((n_fine, "G"), 0.35)))
    if mud_d is not None:
        earth = g.scanned(g.color("RedEarth", (0.19, 0.095, 0.058)), mud_d, 0.119, 0.3, "EarthScanGain")
    ground = g.lerp(ground, earth, g.mul(soil_ring, 0.85))
    ground = g.lerp(ground, out_col, outcrop)
    base = g.lerp(ground, rock_col, rock)

    # Reservoir draw-down band: bare ochre rock/soil from the water line up ~4.5 m.
    # Round 3B: irregular top edge (+-1.7 m), grey-brown mud and ochre silt with faint old waterlines on gentle
    # ground, water-stained rock on steep faces (the flat ochre stripe on vertical cuts read as brown curtains).
    band_top = g.add(g.scalar("BandTopZ", water_z_cm + 520.0), g.mul(g.sub((n_mid, "R"), 0.5), 340.0))
    band = g.mul(g.saturate(g.mul(g.sub(band_top, z), 1.0 / 120.0)),
                 g.saturate(g.mul(g.sub(z, water_z_cm - 50.0), 1.0 / 60.0)))
    lines = g.node(unreal.MaterialExpressionFrac)
    g.link(g.mul(z, 1.0 / 70.0), lines)
    mud = g.lerp(g.color("DrawdownMud", (0.15, 0.13, 0.105)), g.color("WaterlineOchre", (0.26, 0.19, 0.11)), (n_mid, "G"))
    mud = g.mul(mud, g.add(0.82, g.mul(lines, 0.22)))
    # Pebbles and rock fragments (dark / light specks) so the band reads as stony mud, not a sand beach.
    pebbles = g.saturate(g.mul(g.sub((n_fine, "B"), 0.55), 5.0))
    mud = g.lerp(mud, g.mul(lime, 0.75), g.mul(pebbles, 0.6))
    stained_rock = g.mul(rock_col, g.color("WaterStain", (0.68, 0.6, 0.5)))
    band_col = g.lerp(mud, stained_rock, rock)
    wet = g.saturate(g.mul(g.sub(g.add(water_z_cm, 45.0), z), 1.0 / 30.0))       # dark wet line at the water
    band_col = g.mul(band_col, g.sub(1.0, g.mul(wet, 0.45)))
    base = g.lerp(base, band_col, band)

    # Footpaths (pale zig-zag trails, baked by gen_scatter.py into a 2 m/px map-wide mask).
    if "T_HG_PathMask" in tex:
        map_uv = g.mul(g.add(g.mask(wp, r=True, g=True), half_cm), 1.0 / (2.0 * half_cm))
        path = g.mul(g.texture(tex["T_HG_PathMask"], map_uv), g.sub(1.0, rock))
        base = g.lerp(base, g.color("Path", (0.3, 0.24, 0.17)), g.mul(path, 0.85))
    # Round 3B - road verges (gen_scatter T_HG_ShoulderMask, ~1.4 m/px): packed grey-brown gravel and dust in
    # the first ~2.5 m beside the carriageway, loose limestone chips and dark stones, grass creeping back in.
    if "T_HG_ShoulderMask" in tex:
        smap_uv = g.mul(g.add(g.mask(wp, r=True, g=True), half_cm), 1.0 / (2.0 * half_cm))
        verge = g.mul(g.texture(tex["T_HG_ShoulderMask"], smap_uv), g.sub(1.0, g.mul(rock, 0.7)))
        verge = g.saturate(g.mul(g.sub((verge, "R"), g.mul((n_fine, "G"), 0.35)), 1.6))     # ragged grass edge
        gravel = g.mul(g.color("Gravel", (0.19, 0.17, 0.14)), g.add(0.8, g.mul((n_fine, "R"), 0.4)))
        chips = g.saturate(g.mul(g.sub((n_fine, "B"), 0.62), 6.0))
        stones = g.saturate(g.mul(g.sub(0.3, (n_fine, "B")), 6.0))
        gravel = g.lerp(g.lerp(gravel, g.mul(lime, 0.8), g.mul(chips, 0.6)), g.color("DarkStone", (0.07, 0.065, 0.06)), g.mul(stones, 0.6))
        base = g.lerp(base, gravel, verge)
    rocky = g.saturate(g.add(rock, outcrop))
    rough = g.lerp(g.lerp(0.92, 0.95, plots), 0.78, rocky)
    if "riverbed" in scan:
        # River bed under the turquoise water: scanned pebbles / silt below the waterline.
        under = g.saturate(g.mul(g.sub(water_z_cm - 40.0, z), 1.0 / 150.0))
        bed = g.mul(g.mask(g.scan(scan["riverbed"]["diff"], g.world_uv(wp, 250.0)), r=True, g=True, b=True), g.color("RiverbedGain", (0.8, 0.8, 0.8)))
        base = g.lerp(base, bed, under)
    if False:   # budget: grass normal dropped (no visible gain at 720p, ~0.3 ms)
        ground_n = g.scan(scan["grass"]["nor"], g.world_uv(wp, 300.0), "nor")
        normal = g.normalize(g.lerp(g.lerp(g.const3((0.0, 0.0, 1.0)), ground_n, 0.6), rock_n, g.add(g.mul(rocky, 0.6), 0.4)))
    else:
        normal = g.normalize(g.lerp(g.const3((0.0, 0.0, 1.0)), rock_n, g.add(g.mul(rocky, 0.6), 0.4)))
    g.out(base, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(rough, unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(normal, unreal.MaterialProperty.MP_NORMAL)
    return g.finish()


def wet_asphalt(tex) -> unreal.Material:
    """QL4C on a dry March morning (ART_BIBLE): worn, sun-bleached mountain asphalt - mid grey with exposed
    aggregate, polished darker wheel paths, a few low-contrast sealed patches and crack seals, faded paint.
    (Round 1 had big near-black "puddle" patches every few metres that read as oil stains.)"""
    g = Graph("M_HG_WetAsphalt")
    wp = g.world_pos()
    broad = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 2600.0))
    n = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 420.0))
    fine = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 38.0))
    agg_n = g.texture(tex["T_HG_RockNormal"], g.world_uv(wp, 60.0), normal=True)
    tc = g.node(unreal.MaterialExpressionTextureCoordinate)
    u = g.mask(tc, r=True)
    v = g.mask(tc, g=True)
    du = g.node(unreal.MaterialExpressionAbs)
    g.link(g.sub(u, 0.5), du)

    # Bleached grey with gentle broad variation and salt-and-pepper aggregate.
    tone = g.add(g.add(0.86, g.mul((broad, "R"), 0.22)), g.mul(g.sub((fine, "G"), 0.5), 0.35))
    asphalt = g.mul(g.color("Asphalt", (0.13, 0.128, 0.122)), tone)
    scan = tex.get("scan") or {}
    if "asphalt" in scan:
        # Round 7B: scanned asphalt (aggregate, wear) carrying the bleached grey palette.
        ad = g.scan(scan["asphalt"]["diff"], g.world_uv(wp, 250.0))
        asphalt = g.mul(g.scanned(g.color("Asphalt", (0.13, 0.128, 0.122)), ad, 0.1, g.scalar("AsphaltScanMix", 0.5), "AsphaltScanGain"),
                        g.add(0.9, g.mul((broad, "R"), 0.2)))
        if scan["asphalt"].get("nor") is not None:
            agg_n = g.scan(scan["asphalt"]["nor"], g.world_uv(wp, 250.0), "nor")
    # Wheel paths (each lane's tyre tracks at |u-0.5| ~ 0.14 and 0.36): slightly darker and smoother.
    tr1 = g.node(unreal.MaterialExpressionAbs)
    g.link(g.sub(du, 0.14), tr1)
    tr2 = g.node(unreal.MaterialExpressionAbs)
    g.link(g.sub(du, 0.34), tr2)
    tracks = g.saturate(g.add(g.saturate(g.mul(g.sub(0.07, tr1), 12.0)), g.saturate(g.mul(g.sub(0.07, tr2), 12.0))))
    tracks = g.mul(tracks, g.add(0.5, g.mul((n, "R"), 0.6)))
    base = g.lerp(asphalt, g.mul(asphalt, 0.8), g.mul(tracks, g.scalar("WheelPathDarken", 0.6)))
    # Sealed repair patches: rare, only a touch darker (no oil-stain blotches).
    patch = g.saturate(g.mul(g.sub((n, "B"), 0.74), g.scalar("PatchAmount", 6.0)))
    base = g.lerp(base, g.mul(base, 0.82), g.mul(patch, 0.7))
    # Thin crack seals: tar lines where the mid noise crosses a level.
    crack_d = g.node(unreal.MaterialExpressionAbs)
    g.link(g.sub((n, "G"), 0.5), crack_d)
    crack = g.mul(g.saturate(g.mul(g.sub(0.012, crack_d), 60.0)), g.saturate(g.mul(g.sub((broad, "B"), 0.45), 4.0)))
    base = g.lerp(base, g.color("CrackSeal", (0.03, 0.03, 0.03)), g.mul(crack, 0.8))
    # Round 6B: potholes (rare, in the wheel paths), dark wet-looking hollows with a pale gravel rim.
    hole_n = g.add((n, "B"), g.mul((fine, "G"), 0.08))
    # (rare: only where the broad noise marks a worn stretch, and only the noise peaks - the first pass read
    # as puddles all over the lanes)
    worn = g.saturate(g.mul(g.sub((broad, "G"), 0.62), 5.0))
    hole = g.mul(g.mul(g.saturate(g.mul(g.sub(hole_n, 0.965), 40.0)), g.saturate(g.mul(tracks, 2.0))), worn)
    rim = g.mul(g.mul(g.saturate(g.mul(g.sub(hole_n, 0.945), 40.0)), g.sub(1.0, hole)), worn)
    base = g.lerp(base, g.color("HoleGravel", (0.2, 0.17, 0.13)), g.mul(rim, g.saturate(g.mul(tracks, 2.0))))
    base = g.lerp(base, g.color("Hole", (0.03, 0.028, 0.025)), hole)
    # Pale dust along the edges (gravel shoulders).
    edge_dust = g.saturate(g.mul(g.sub(du, 0.44), 14.0))
    base = g.lerp(base, g.color("EdgeDust", (0.24, 0.2, 0.15)), g.mul(edge_dust, g.add(0.3, g.mul((n, "R"), 0.5))))

    rough = g.add(g.scalar("DryRoughness", 0.78), g.mul((fine, "R"), 0.16))
    rough = g.lerp(rough, 0.5, g.mul(tracks, 0.7))   # 6B: tyre-polished wheel paths
    flat = g.const3((0.0, 0.0, 1.0))
    normal = g.normalize(g.lerp(agg_n, flat, g.add(g.mul(tracks, 0.25), 0.45)))

    # Lane paint from the road-strip UVs (U across, V along each 8 m segment): yellow dashed centre line
    # (3.4 m dash / 4.6 m gap) + solid white edge lines, worn through in the wheel paths and in flakes.
    centre = g.saturate(g.mul(g.sub(0.013, du), 400.0))
    # 3 m dash / 6 m gap per real metre of road (custom primitive data from the road actor).
    dphase = g.node(unreal.MaterialExpressionFrac)
    g.link(g.mul(road_along(g, v), 1.0 / 9.0), dphase)
    dash = g.saturate(g.mul(g.sub(0.333, dphase), 60.0))
    yellow = g.mul(centre, dash)
    de = g.node(unreal.MaterialExpressionAbs)
    g.link(g.sub(du, 0.455), de)
    edge = g.saturate(g.mul(g.sub(0.012, de), 400.0))
    flakes = g.saturate(g.mul(g.sub((fine, "B"), 0.3), 3.0))
    # Faded, not gone: ~65-95 % paint with flaked spots, a little thinner in the wheel paths.
    paint_wear = g.mul(g.add(0.62, g.mul(flakes, g.add(0.15, g.mul((n, "G"), 0.2)))), g.sub(1.0, g.mul(tracks, 0.3)))
    base = g.lerp(base, g.color("LaneYellow", (0.5, 0.36, 0.06)), g.mul(yellow, paint_wear))
    base = g.lerp(base, g.color("LaneWhite", (0.52, 0.52, 0.5)), g.mul(edge, paint_wear))
    rough = g.lerp(rough, 0.62, g.saturate(g.add(yellow, edge)))
    g.out(base, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(rough, unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.const(0.45), unreal.MaterialProperty.MP_SPECULAR)
    g.out(normal, unreal.MaterialProperty.MP_NORMAL)
    return g.finish()


def concrete_road(tex) -> unreal.Material:
    """Ta Lang road: hand-poured concrete lanes - 4 m slabs, each pour a slightly different tone, sawn
    transverse joints and a centre joint with chipped dark edges, broom-finish grain across the road, tyre
    darkening in the wheel paths, hairline cracks, red-ochre dust blown on from the slopes. No lane paint."""
    g = Graph("M_HG_ConcreteRoad")
    wp = g.world_pos()
    broad = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 1800.0))
    n = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 260.0))
    fine = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 30.0))
    agg_n = g.texture(tex["T_HG_RockNormal"], g.world_uv(wp, 90.0), normal=True)
    tc = g.node(unreal.MaterialExpressionTextureCoordinate)
    u = g.mask(tc, r=True)
    v = g.mask(tc, g=True)
    du = g.node(unreal.MaterialExpressionAbs)
    g.link(g.sub(u, 0.5), du)

    # Per-slab tone: sample the noise at the slab's index (2 slabs per 8 m segment x 2 lanes) + broad drift.
    along = road_along(g, v)                       # metres along the road (4 m pours)
    slab_v = g.node(unreal.MaterialExpressionFloor)
    g.link(g.mul(along, 0.25), slab_v)
    side = g.saturate(g.mul(g.sub(u, 0.5), 1000.0))
    slab_uv = g.append(g.add(g.mul(slab_v, 0.173), g.mul(side, 0.29)), g.add(g.mul((broad, "R"), 0.5), g.mul(side, 0.41)))
    slab_n = g.texture(tex["T_HG_Noise"], slab_uv)
    slab_tone = g.add(0.78, g.mul((slab_n, "G"), 0.42))
    # Broom finish: fine grain stretched across the road.
    broom_uv = g.append(g.mul(u, 1.5), g.mul(along, 32.0))
    broom = g.texture(tex["T_HG_Noise"], broom_uv)
    concrete = g.mul(g.color("Concrete", (0.3, 0.292, 0.272)),
                     g.mul(slab_tone, g.add(g.add(0.86, g.mul((fine, "G"), 0.22)), g.mul((broom, "R"), 0.12))))
    scan = tex.get("scan") or {}
    if "concrete" in scan:
        # Round 7B: scanned concrete detail on each pour (slab tone and broom grain kept).
        cd_s = g.scan(scan["concrete"]["diff"], g.world_uv(wp, 300.0))
        concrete = g.scanned(concrete, cd_s, 0.3, g.scalar("ConcreteScanMix", 0.35), "ConcreteScanGain")
        if scan["concrete"].get("nor") is not None:
            agg_n = g.scan(scan["concrete"]["nor"], g.world_uv(wp, 300.0), "nor")
    elif "ground" in scan and scan["ground"].get("nor") is not None:
        # No scanned concrete was fetched: procedural slabs over the stony-ground scan's normal (aggregate relief).
        agg_n = g.scan(scan["ground"]["nor"], g.world_uv(wp, 200.0), "nor")
    # Wheel paths: grey-brown tyre grime.
    tr = g.node(unreal.MaterialExpressionAbs)
    g.link(g.sub(du, 0.24), tr)
    tracks = g.mul(g.saturate(g.mul(g.sub(0.1, tr), 9.0)), g.add(0.4, g.mul((n, "R"), 0.7)))
    base = g.lerp(concrete, g.mul(concrete, g.color("Grime", (0.72, 0.7, 0.66))), tracks)
    # Water / dirt staining in soft patches (low contrast).
    stain = g.saturate(g.mul(g.sub((n, "B"), 0.62), 3.0))
    base = g.lerp(base, g.mul(base, 0.8), g.mul(stain, 0.6))
    # Hairline cracks within slabs.
    cd = g.node(unreal.MaterialExpressionAbs)
    g.link(g.sub((n, "G"), 0.5), cd)
    crack = g.mul(g.saturate(g.mul(g.sub(0.008, cd), 90.0)), g.saturate(g.mul(g.sub((broad, "B"), 0.5), 4.0)))
    base = g.lerp(base, g.color("Crack", (0.08, 0.075, 0.07)), g.mul(crack, 0.7))
    # Sawn joints: transverse every 4 m (V 0 / 0.5 / 1) + longitudinal centre joint, chipped edges.
    fv = g.node(unreal.MaterialExpressionFrac)
    g.link(g.mul(along, 0.25), fv)
    dv = g.node(unreal.MaterialExpressionAbs)
    g.link(g.sub(fv, 0.5), dv)
    chip = g.mul(g.sub((fine, "R"), 0.5), 0.004)
    joint_t = g.saturate(g.mul(g.sub(g.add(dv, chip), 0.488), 120.0))   # ~8 cm sawn joints, readable at 720p
    joint_c = g.saturate(g.mul(g.sub(g.add(0.009, chip), du), 220.0))
    joint = g.saturate(g.add(joint_t, joint_c))
    base = g.lerp(base, g.color("Joint", (0.05, 0.045, 0.04)), joint)
    # Round 6B: spalled holes where a slab corner broke (near joints, in the wheel paths) with loose gravel.
    spall_n = g.add((n, "B"), g.mul(g.sub(1.0, g.saturate(g.mul(g.sub(dv, 0.4), 10.0))), -0.12))
    spall = g.mul(g.mul(g.saturate(g.mul(g.sub(spall_n, 0.9), 30.0)), g.saturate(g.mul(tracks, 2.5))),
                  g.saturate(g.mul(g.sub((broad, "G"), 0.55), 4.0)))
    base = g.lerp(base, g.color("Spall", (0.07, 0.06, 0.05)), spall)
    base = g.lerp(base, g.color("Crack", (0.08, 0.075, 0.07)), g.mul(g.saturate(g.mul(g.sub(0.014, cd), 60.0)), 0.35))
    # Red-ochre dust blown onto the edges and into the joints.
    edge = g.saturate(g.mul(g.sub(du, 0.44), 14.0))                    # last ~25 cm only
    base = g.lerp(base, g.color("Dust", (0.3, 0.2, 0.13)), g.mul(edge, g.add(0.2, g.mul((n, "R"), 0.4))))
    g.out(base, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.lerp(g.add(0.74, g.mul((fine, "R"), 0.18)), 0.92, joint), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.const(0.45), unreal.MaterialProperty.MP_SPECULAR)
    g.out(g.normalize(g.lerp(agg_n, g.const3((0.0, 0.0, 1.0)), 0.6)), unreal.MaterialProperty.MP_NORMAL)
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
    col = g.lerp(g.color("Turquoise", (0.008, 0.085, 0.078)), g.color("TurquoiseLight", (0.014, 0.115, 0.1)), (tint, "R"))
    g.out(col, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.scalar("Roughness", 0.14), unreal.MaterialProperty.MP_ROUGHNESS)
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
    lime = g.mul(g.color("Limestone", (0.22, 0.215, 0.2)), g.add(0.75, g.mul((blotch, "G"), 0.4)))
    rust = g.saturate(g.mul(g.sub((streak, "G"), 0.6), 5.0))
    dark = g.saturate(g.mul(g.sub((streak, "R"), 0.58), 4.0))
    col = g.lerp(g.lerp(lime, g.color("Rust", (0.48, 0.21, 0.08)), rust), g.color("Stain", (0.08, 0.075, 0.065)), g.mul(dark, 0.75))
    # Round 4B: macro bands (35 m top projection -> broad vertical curtains on the walls), as on the landscape.
    macro = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 3500.0))
    col = g.mul(col, g.add(0.62, g.mul((macro, "G"), 0.6)))
    col = g.lerp(col, g.color("WarmLime", (0.24, 0.19, 0.13)), g.mul(g.saturate(g.mul(g.sub((macro, "R"), 0.55), 3.0)), 0.5))
    col = g.lerp(col, g.color("Weathered", (0.075, 0.08, 0.065)), g.mul(g.saturate(g.mul(g.sub(0.45, (macro, "B")), 3.0)), 0.6))
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


def vertex_color() -> unreal.Material:
    """People and bikes (C++ FHGMeshKit, merged low-poly meshes): base colour from the vertex colour; UV0.x is
    the surface class (0 cloth/skin .. 1 chrome: roughness 0.85 -> 0.22, metallic above 0.7), UV0.y = 1 marks
    painted panels tinted by the MID parameter "Tint" (bike body colour)."""
    g = Graph("M_HG_VertexColor")
    vc = g.node(unreal.MaterialExpressionVertexColor)
    tc = g.node(unreal.MaterialExpressionTextureCoordinate)
    u = g.mask(tc, r=True)
    v = g.mask(tc, g=True)
    rgb = g.mask(vc, r=True, g=True, b=True)
    tinted = g.mul(rgb, g.color("Tint", (0.55, 0.02, 0.02)))
    base = g.lerp(rgb, tinted, v)
    g.out(base, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.lerp(0.85, 0.22, u), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.saturate(g.mul(g.sub(u, 0.7), 3.3)), unreal.MaterialProperty.MP_METALLIC)
    g.out(g.lerp(0.35, 0.5, u), unreal.MaterialProperty.MP_SPECULAR)
    return g.finish()


def kit_dressing(tex) -> unreal.Material:
    """Round 7B: M_HG_VertexColor plus scanned detail for the dressing kit meshes (houses, platforms, terrace and
    dry-stone walls, fences). Matte surfaces (UV0.x = 0) take the luminance detail of a dry-stone scan, or of an
    earth / mud scan where the vertex colour is warm (rammed earth, yards); wood, tiles' satin and metal stay
    plain. World-aligned biplanar projection - the kit meshes have no texture UVs."""
    g = Graph("M_HG_KitDressing")
    scan = tex.get("scan") or {}
    vc = g.node(unreal.MaterialExpressionVertexColor)
    tc = g.node(unreal.MaterialExpressionTextureCoordinate)
    u = g.mask(tc, r=True)
    rgb = g.mask(vc, r=True, g=True, b=True)
    base = rgb
    wp = g.world_pos()
    nz = g.node(unreal.MaterialExpressionAbs)
    g.link(g.up_facing(), nz)
    side = g.saturate(g.mul(g.sub(0.75, nz), 4.0))
    matte = g.saturate(g.sub(1.0, g.mul(u, 4.0)))
    det = None
    if "stonewall" in scan:
        det = g.mul(g.lum(g.scan_biplanar(scan["stonewall"]["diff"], wp, 160.0, side)), 1.0 / g_mean(0.172))
    if "earthwall" in scan:
        ed = g.mul(g.lum(g.scan_biplanar(scan["earthwall"]["diff"], wp, 220.0, side)), 1.0 / g_mean(0.119))
        warm = g.saturate(g.mul(g.sub(g.mask(vc, r=True), g.mul(g.mask(vc, b=True), 1.6)), 10.0))
        det = ed if det is None else g.lerp(det, ed, warm)
    if det is not None:
        base = g.mul(rgb, g.lerp(1.0, det, g.mul(matte, g.scalar("DetailAmount", 0.85))))
    g.out(base, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.lerp(0.85, 0.22, u), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.saturate(g.mul(g.sub(u, 0.7), 3.3)), unreal.MaterialProperty.MP_METALLIC)
    g.out(g.lerp(0.35, 0.5, u), unreal.MaterialProperty.MP_SPECULAR)
    return g.finish()


def g_mean(v):
    return float(v)


def lamp() -> unreal.Material:
    """Head / tail / police lamp lenses: unlit emissive Color x Intensity (MID-driven: brake light, flashers).
    Meshes using it have no shadow and no Lumen / distance-field contribution (FHGMeshKit::AddLamp)."""
    g = Graph("M_HG_Lamp")
    try:
        g.mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    except Exception as exc:
        hg.warn(f"M_HG_Lamp unlit: {exc}")
    g.out(g.mul(g.color("Color", (1.0, 0.05, 0.02)), g.scalar("Intensity", 2.0)), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return g.finish()


def range_layer(tex, name, color, haze, haze_amount) -> unreal.Material:
    """Distant range layer (Round 3B): forest / rock breakup from world-space noise, lit by the sun on its sloped
    flanks; the lower slopes fade into valley haze (lighter, bluer) while the ridges keep their contrast. The
    height fog and the atmosphere add the distance colour on top."""
    g = Graph(name)
    g.mat.set_editor_property("two_sided", True)
    wp = g.world_pos()
    z = g.mask(wp, b=True)
    n = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 60000.0))
    n2 = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 9000.0))
    col = g.mul(g.color("Base", color), g.add(0.7, g.add(g.mul((n, "R"), 0.4), g.mul((n2, "G"), 0.25))))
    rock = g.saturate(g.mul(g.sub((n2, "B"), 0.62), 4.0))
    col = g.lerp(col, g.mul(g.color("Rock", (0.2, 0.2, 0.2)), g.add(0.8, g.mul((n, "G"), 0.3))), g.mul(rock, 0.5))
    # Round 4B: one aerial-perspective amount per layer (distance), plus only a light extra haze on the lowest
    # slopes. The old height-graded haze turned every lower flank sky-coloured, so the ridge tops floated as
    # arches and slivers over a pale "lake".
    valley = g.saturate(g.mul(g.sub(g.scalar("HazeTopZ", 90000.0), z), 1.0 / 60000.0))
    amt = g.add(g.scalar("HazeAmount", haze_amount), g.mul(valley, 0.15))
    col = g.lerp(col, g.color("Haze", haze), amt)
    g.out(col, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.const(0.95), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.const(0.2), unreal.MaterialProperty.MP_SPECULAR)
    return g.finish()


def sign_text(tex, key, color):
    """Sign lettering (gen_signs.py coverage mask): masked, two-sided, tinted - carved red on the name stone,
    cream paint on the wooden boards."""
    g = Graph(f"M_HG_Sign_{key}")
    g.mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    g.mat.set_editor_property("two_sided", True)
    tc = g.node(unreal.MaterialExpressionTextureCoordinate)
    t = g.texture(tex[f"T_HG_Sign_{key}"], tc)
    g.out(g.color("Color", color), unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.const(0.75), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out((t, "R"), unreal.MaterialProperty.MP_OPACITY_MASK)
    return g.finish()


def far_terrain(tex) -> unreal.Material:
    """Real DEM terrain beyond the map edge (gen_farterrain.py): dry khaki grass and dark shrub patches like the
    landscape, grey limestone on steep faces, valley floors fading into the low haze. Cheap: 2 samples."""
    g = Graph("M_HG_FarTerrain")
    # Round 4B: two-sided - in game only the slopes above eye level drew (an "arch" over a void past Ma Pi Leng,
    # terrain slivers in the sky). Two-sided draws the whole heightfield whatever the importer did to the winding.
    g.mat.set_editor_property("two_sided", True)
    wp = g.world_pos()
    z = g.mask(wp, b=True)
    n = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 40000.0))
    n2 = g.texture(tex["T_HG_Noise"], g.world_uv(wp, 7000.0))
    up = g.up_facing()
    steep = g.saturate(g.mul(g.sub(0.72, up), 4.0))
    dry = g.lerp(g.color("Dry", (0.075, 0.075, 0.04)), g.color("Khaki", (0.1, 0.09, 0.05)), (n2, "G"))   # match the landscape's green-khaki
    shrub = g.color("Shrub", (0.022, 0.042, 0.018))
    col = g.lerp(dry, shrub, g.saturate(g.mul(g.sub(g.add((n, "R"), g.mul((n2, "B"), 0.5)), 0.55), 2.0)))
    rock = g.mul(g.color("Rock", (0.16, 0.16, 0.155)), g.add(0.75, g.mul((n2, "R"), 0.5)))
    col = g.lerp(col, rock, g.mul(steep, g.add(0.5, g.mul((n, "G"), 0.5))))
    valley = g.saturate(g.mul(g.sub(g.scalar("HazeTopZ", 90000.0), z), 1.0 / 45000.0))
    col = g.lerp(col, g.color("Haze", (0.15, 0.18, 0.21)), g.mul(valley, 0.2))
    # Round 5B: near the map edge the far terrain takes the landscape's own tones (same dry grass / khaki / shrub
    # colours as M_HG_Terrain) so the seam does not read as a pale band; from ~1.5 km beyond the edge it blends
    # into the hazier far palette above, fully hazy from ~6 km out.
    ax = g.node(unreal.MaterialExpressionAbs)
    g.link(g.mask(wp, r=True), ax)
    ay = g.node(unreal.MaterialExpressionAbs)
    g.link(g.mask(wp, g=True), ay)
    beyond = g.sub(g.binary(unreal.MaterialExpressionMax, ax, ay), g.scalar("MapHalfCm", 294336.0))
    near = g.sub(1.0, g.saturate(g.mul(g.sub(beyond, 150000.0), 1.0 / 450000.0)))
    dry_l = g.lerp(g.color("NearDry", (0.15, 0.12, 0.075)), g.color("NearKhaki", (0.17, 0.15, 0.08)), (n2, "G"))
    green_l = g.lerp(g.color("NearShrub", (0.015, 0.04, 0.012)), g.color("NearShrubLight", (0.045, 0.085, 0.025)), (n, "G"))
    col_n = g.lerp(dry_l, green_l, g.saturate(g.mul(g.sub(g.add((n, "R"), g.mul((n2, "B"), 0.5)), 0.45), 2.0)))
    rock_n = g.mul(g.color("NearRock", (0.2, 0.195, 0.185)), g.add(0.7, g.mul((n2, "R"), 0.45)))
    col_n = g.lerp(col_n, rock_n, g.mul(steep, g.add(0.5, g.mul((n, "G"), 0.5))))
    col = g.lerp(col, col_n, near)
    g.out(col, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.const(0.92), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.const(0.3), unreal.MaterialProperty.MP_SPECULAR)
    return g.finish()


def build_all(tex, water_z_cm=42000.0, layout=None) -> dict:
    hm = (layout or {}).get("heightmap", {})
    half_cm = hm.get("half_m", 2016.0) * 100.0
    patch = (layout or {}).get("geo", {}).get("tham_ma_patch")
    lush_c = [c * 100.0 for c in patch["center_m"]] if patch else None
    lush_r = (patch["core_m"] + patch["band_m"] * 0.5) * 100.0 if patch else 90000.0
    return {
        "terrain": terrain(tex, water_z_cm, half_cm, lush_c, lush_r),
        "asphalt": wet_asphalt(tex),
        "concrete": concrete_road(tex),
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
        # H'mong houses: rammed-earth walls (ochre-brown), grey yin-yang clay tiles (ART_BIBLE).
        "house_wall": bark(tex, "M_HG_HouseWall", (0.36, 0.23, 0.14)),
        "roof_grey": bark(tex, "M_HG_RoofTile", (0.12, 0.12, 0.125)),
        "peach": foliage(tex, "M_HG_PeachBlossom", (0.45, 0.18, 0.22), (0.72, 0.4, 0.45), 0.75),
        "cafe_wall": bark(tex, "M_HG_CafeWall", (0.55, 0.53, 0.49)),
        "monument": bark(tex, "M_HG_MonumentStone", (0.46, 0.44, 0.4)),
        "roof_green": metal("M_HG_RoofGreen", (0.06, 0.24, 0.13), 0.55),
        "roof_blue": metal("M_HG_RoofBlue", (0.07, 0.17, 0.34), 0.55),
        "vertex_color": vertex_color(),
        "kit_dressing": kit_dressing(tex),
        **({f"sign_{k}": sign_text(tex, k, c) for k, c in (("MaPiLeng", (0.32, 0.02, 0.012)), ("ThamMa", (0.62, 0.56, 0.42)),
                                                           ("TuSan", (0.62, 0.56, 0.42))) if f"T_HG_Sign_{k}" in tex}),
        "lamp": lamp(),
        "far_terrain": far_terrain(tex),
        "range_0": range_layer(tex, "M_HG_RangeLayer_0", (0.05, 0.072, 0.045), (0.12, 0.15, 0.19), 0.22),
        "range_1": range_layer(tex, "M_HG_RangeLayer_1", (0.055, 0.072, 0.065), (0.13, 0.16, 0.21), 0.38),
        "range_2": range_layer(tex, "M_HG_RangeLayer_2", (0.06, 0.08, 0.085), (0.14, 0.17, 0.22), 0.52),
        "ranges": foliage(tex, "M_HG_DistantRanges", (0.05, 0.08, 0.05), (0.09, 0.12, 0.08), 0.95, two_sided=True),
    }
