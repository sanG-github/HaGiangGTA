# HaGiangGTA — Art Bible (research synthesis)

One map, one moment in time. Everything (terrain, vegetation, water, light, props, NPCs) must be
consistent with the choice below. Numbers come from the research notes at the end; where sources
disagree or are silent, the design value is marked *(design)*.

## 1. Season, time of day, weather — LOCKED

**Late March, ~10:30, clear sky with light dry-season haze (no cloud deck).**

Why this moment:
- Bombax / kapok (*hoa gạo*) is in bloom along the Nho Quế valley (lunar months 2–3 ≈ March) — the red trees in the reference photos.
- End of the dry season (Oct–Apr): the Nho Quế reservoir is at its most turquoise/emerald, and the draw-down leaves a bare **ochre band** at the waterline.
- Canyon slopes are **dry grass (mauve / khaki) dotted with green shrubs**; maize terraces are freshly **ploughed brown soil** (planting season).
- Up at Thẩm Mã (~1,490 m) the slopes stay greener (olive green, forest on the big north-west mountain); flower sellers carry **yellow mustard (cải) and white flowers**; peach/plum blossom lingers near hamlets.
- Morning fog has lifted (sources: winter/spring fog usually burns off by mid–late morning) → clear air with aerial haze, no valley "sea of clouds".

Light: sun ~50° above the horizon from the east-south-east, neutral-warm (5,500–5,800 K), soft
shadows; sky light fills the canyon walls (no black shadows). Exposure as in the reference photos:
bright, slightly hazy, saturated turquoise water.

## 2. Geography - real data (since the "exact places" pass)

The map is built from **Copernicus GLO-30 elevation + OpenStreetMap** (`Scripts/Tools/gen_heightmap.py`,
`Data/Geo/README.md`): a 5.89 km x 5.89 km frame at true scale centred on 23.2450 N 105.4040 E, north = -Y.

| Place | Source | In game |
|---|---|---|
| QL4C Tà Làng junction -> Youth monument -> Mã Pí Lèng Panorama -> south | OSM trunk QL.4C | exact path, ~10 km |
| Tà Làng road (35 hairpins, QL4C -> Nho Quế) | OSM way 855373895 | exact path, 6.2 km, 1,095 -> 470 m |
| Tu Sản gorge, Nho Quế 1 reservoir, dam | OSM river / reservoir polygon / dam 1320784007 | water 450.5 m, 18 m lower below the dam |
| Mã Pí Lèng Panorama café | OSM node 23.2270 N 105.4179 E | ~980 m |
| Youth Volunteers monument | OSM node 23.2497 N 105.3937 E | ~1,250 m |
| Mỏm Đá Tử Thần | no published coordinates | ledge on the rim nearest the Panorama, facing the gorge |
| Dốc Thẩm Mã (23.1689 N 105.1942 E, 25 km west) | real 1.5 km DEM patch pasted SW of Mã Pí Lèng, OSM QL4C bends | real altitude 986 m (shown in the HUD), +48 m in game |

Earlier research figures such as "Thẩm Mã 1,490 m" and "Mã Pí Lèng 1,230 m" were not used: the elevation
data puts the Thẩm Mã viewpoint at ~990 m and the Panorama at ~980 m.

The route: Thẩm Mã climb + bends -> link road (least-cost path) -> QL4C at the Panorama -> north past the
Youth monument -> Tà Làng road down to the dock on the Nho Quế (boats run south through Tu Sản to the dam).

## 3. Locations

### Dốc Thẩm Mã (VP1)
- Road: QL4C, 6–7 m asphalt, yellow dashed centre line, white edge lines, W-beam guardrails on the valley side, black/white chevrons on the bends, concrete utility poles.
- Bends: 4–6 tight, irregular S-bends (legs ~80–200 m, inner radius ~10–15 m), curved legs, not parallel.
- Top: stone railing/parapet, small roadside coffee shop, H'mông children and grandmothers selling flowers / flower wreaths, red buckets of yellow & white flowers, motorbike tourists. Pack horses (the "horse-testing slope" legend).
- Landscape: narrow V valley, big steep olive-green mountain on the NW side, maize fields and stone/rammed-earth H'mông houses with grey tile roofs in the valley.

### Đèo Mã Pí Lèng + Mỏm Đá Tử Thần (VP2)
- Road cut into the cliff (rock face uphill, drop downhill), ~5–6 m, red/white concrete posts (*cọc tiêu*) and low stone walls along the edge, often no barrier at all.
- Summit: Youth Volunteers ("Con đường Hạnh Phúc") monument + Mã Pí Lèng name stone, small café/viewing building, drink stalls, motorbike parking.
- Mỏm Đá Tử Thần: grey limestone ledge jutting over the gorge, hundreds of metres drop, partial stone fence since 09/2025.
- View: Tu Sản gorge with near-vertical grey limestone walls (orange/rust streaks), emerald river far below like a thread, maize pockets and zig-zag footpaths on the slopes.

### Hẻm Tu Sản + Bến Tà Làng + sông Nho Quế (VP3)
- Water: emerald/turquoise, calm reservoir, ochre draw-down band, bombax trees in red bloom along the banks.
- Dock: floating pontoons with green roofs, covered metal tourist boats (12–18 seats, red roof with yellow star, blue hull), kayaks/SUP, ticket booth, café platform, concrete parking/ramp.
- Nho Quế 1 dam 400 m downstream: concrete wall with spillway piers, powerhouse; rocky shallow river below.

## 4. Palette (linear, used by the materials)
- Dry grass mauve `(0.20, 0.13, 0.115)` / khaki `(0.20, 0.155, 0.095)`; shrub green `(0.015, 0.04, 0.012)`–`(0.045, 0.085, 0.025)`.
- Thẩm Mã olive green `(0.04, 0.075, 0.025)`; ploughed soil `(0.27, 0.16, 0.095)`.
- Limestone `(0.22–0.30)` grey, rust streak `(0.46, 0.20, 0.08)`.
- Water turquoise `(0.008, 0.085, 0.078)`; waterline ochre `(0.42, 0.27, 0.13)`.
- Bombax red `(0.5, 0.04, 0.03)`; peach pink `(0.6, 0.25, 0.3)`; mustard yellow `(0.7, 0.55, 0.05)`.

## 5. Performance rules (Apple M1 Pro, SM5)
- No Nanite (SM6 needs Apple8 / M2+): LOD chains on every mesh.
- No VolumetricCloud (measured −10…−25 FPS even at low quality on M1): clear hazy sky (matches the Nho Quế reference photos).
- CSM to 150 m, no distance-field shadows (banding on steep landscape).
- Landscape material ≤ ~7 texture samples.

## 6. Sources
- Thẩm Mã: climbfinder.com (Tham Ma Pass profile), localvietnam.com/ha-giang/tham-ma-pass, wanderlog (Coffee Dốc Thẩm Mã, 23.1694 N 105.1946 E), vietnamnet (horse-testing legend, flower wreaths), baotuyenquang.
- Mã Pí Lèng: vi/en Wikipedia "Đèo Mã Pí Lèng", dangerousroads.org, vietnamtourism.com (skywalk guide), dtinews (Mỏm Đá closure 09/2025, stone fences).
- Nho Quế / Tu Sản / Tà Làng: showcaves.com (Tu San gorge, dam 1.4 km downstream, jetty 400 m upstream of the dam), vietnamnews.vn (Tà Làng pass), oxalisadventure.com, localvietnam.com, tuoitre (2023 boat halt), baovanhoa 2026 (boats running, 150,000 VND), en.nhandan.vn (bombax season), Wikipedia "Nho Quế River" (dam cascade).
- Reference photos supplied by the user (Thẩm Mã bends from the parapet, Nho Quế from the rim, Tà Làng dock, Tu Sản narrows).
