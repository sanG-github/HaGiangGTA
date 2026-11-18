# Checkpoint Log

Active-work time is read from `ACTIVE_WORK_TIMER.json` (`active_work_seconds`);
time spent waiting for a usage-limit reset is excluded (`cooldown_wait_seconds`).

| # | Branch | Tag | Milestone | Status |
|---|---|---|---|---|
| 01 | `checkpoint/01-10min-greybox` | `cp-01-10min` | 10 min | done (~7 min active) |
| 02 | `checkpoint/02-1hr-heightmap-bike` | `cp-02-1hr` | 1 h | done (~26 min active) |
| 03 | `checkpoint/03-6hr-3viewpoints-photoreal` | `cp-03-6hr` | 6 h | done (~35 min active) |
| 04 | `checkpoint/04-12hr-living-world` | `cp-04-12hr` | 12 h | done (~42 min active) |
| 05 | `checkpoint/05-24hr-full-gta-5star` | `cp-05-24hr` | 24 h | done (~50 min active) |

---

## CP01 — Greybox (`cp-01-10min`)

**Rebuild:** compile the `HaGiangGTA` module, then run `Scripts/Checkpoints/build_cp01.py`
(Tools → Execute Python Script). Creates `/Game/Maps/L_HaGiang_CP01` and saves it.

**Contents**
- UE 5.5 C++ project (`HaGiangGTA.uproject`, module `HaGiangGTA`), plugins: Python, Editor Scripting, Enhanced Input, Modeling Tools.
- `Config/DefaultEngine.ini` 60 FPS profile: TSR (`r.AntiAliasingMethod=4`, `r.ScreenPercentage=66`),
  Software Lumen GI + reflections, Nanite, Virtual Shadow Maps, `r.Streaming.PoolSize=2048`, DX12/Metal SM6.
- `AHGGreyboxCarPawn` — box car with trace-based arcade driving: follows slopes, gravity on grades,
  launches off sharp crests, lands, slides on walls, body lean, chase camera with lag.
- `HGInput` — Enhanced Input actions/contexts built at runtime in C++ (no input `.uasset` files).
- `AHGGreyboxGameMode` + `AHGGreyboxHUD` (speed km/h, grounded/airborne, control hints).
- `build_cp01.py` — 4 km × 4 km grid ground, 300 m approach road, 200 m / +30 m white ramp
  (Dốc Thẩm Mã stand-in) with red/white posts every 10 m, summit platform + check-in ring,
  test kicker, sun + SkyAtmosphere + realtime SkyLight + height fog, PlayerStart, GameMode override.

**Test:** Play → W/S throttle/brake/reverse, A/D steer, Shift boost, Space handbrake, R reset.
Drive up the ramp, hit the red kicker at ~80+ km/h to check airborne/landing.

**Known limits:** C++ and the editor script were written without a local UE 5.5 install in the
authoring environment, so the first compile/run happens on your machine.

---

## CP02 — Heightmap, Honda Win & Nho Que boats (`cp-02-1hr`)

**Rebuild**
1. (once) `python3 -m venv .venv && .venv/bin/pip install -r Scripts/Tools/requirements.txt`
2. `.venv/bin/python Scripts/Tools/gen_heightmap.py` (~15 s; `build_cp02.py` also runs it automatically if the files are missing)
3. Compile `HaGiangGTA` + `HaGiangGTAEditor`, then run `Scripts/Checkpoints/build_cp02.py` in the editor
   (creates `/Game/Maps/L_HaGiang_CP02`; the landscape import takes about a minute).

**Terrain (`gen_heightmap.py`, seed 20261118)**
- 4033 × 4033, 16-bit PNG + `.r16`, 1 m/quad, Z scale 256 → world Z (cm) = altitude (m) × 100, range 300–1610 m.
- One ~10 km road carved with cut/fill banks:
  Dốc Thẩm Mã (10 legs / 9 hairpins, 800 → 1050 m) → link road → Mã Pí Lèng canyon-rim road (1200 m)
  with the Mỏm Đá Tử Thần rock spur over the void → 11-hairpin descent to the Tà Làng dock (701.6 m).
- Tu Sản canyon: cutter along the Nho Quế with near-vertical walls at Mã Pí Lèng, gentler in the north;
  river surface 700 m, bed 692 m, ~800 m deep from the east-bank peaks. 420 "tai mèo" karst cones.
- `HaGiang_layout.json`: landscape transform, road + river polylines, viewpoints, spawns. `HaGiang_preview.png`: hillshade check.

**C++**
- `UHGLandscapeBuilder` (editor module `HaGiangGTAEditor`) — Python-callable landscape import from `.r16`.
- `AHGRoadSplineActor` — spline + asphalt spline-mesh segments (landscape gives collision).
- `AHGWaterBody` — river surface + query used by boats and swimmers.
- `AHGVehicleBase` → `AHGGroundVehicle` (shared arcade model) → `AHGGreyboxCarPawn`, `AHGBikePawn`;
  `ANhoQueBoatPawn` (Cano / Kayak). `[F]` enter/exit with a 0.4 s guard.
- `AHGBikePawn` (Honda Win 110): lean = atan(v·ω/g), spinning wheels, head/brake lights, plate "23-H1 234.56 / HA GIANG",
  backpacker passenger on a spring-damper (leans out of turns, nods when braking).
- `ANhoQueBoatPawn`: buoyancy spring + waves, bow lift, turn roll, won't beach (min depth), kayak paddle pulses.
- `AHGPlayerCharacter`: walk / sprint / jump, swim (custom buoyancy, wades out at banks), `[F]` nearest vehicle.
- `AHGOpenWorldGameMode` + `AHGOpenWorldHUD` (mode, km/h, altitude, `[F]` prompt).

**Test:** Play → walk to the Honda Win (`F`), ride the 9 hairpins → Mã Pí Lèng → descend to Tà Làng →
`F` off, walk onto the dock → `F` on the cano or kayak; or jump into the river and swim to a boat.

**Known limits:** written without a local UE 5.5 build; greybox shapes for bike/boat/character; asphalt and water
are flat colours until CP03 (materials, Nanite cliffs, Lumen sky).

---

## CP03 — Three photoreal viewpoints (`cp-03-6hr`)

**Rebuild:** `.venv/bin/python Scripts/Tools/gen_heightmap.py && .venv/bin/python Scripts/Tools/gen_assets.py && .venv/bin/python Scripts/Tools/gen_scatter.py`
(the build script runs any that are missing), compile, then run `Scripts/Checkpoints/build_cp03.py` → `/Game/Maps/L_HaGiang_CP03`.

**Procedural assets (`gen_assets.py`, no Megascans download needed)**
- Textures: tileable limestone normal (ridges + strata), RGB breakup noise (B = puddle mask), seamless water normal.
- Nanite meshes (OBJ, cm): 4 karst cliff slabs (fracture-plane cuts, karren flutes, strata ledges, pinnacle crown),
  3 boulders, 2 shrub clumps, VN road post (UV.V = height for the red band).
- `SW_HG_SaoMong.wav`: synthesized Hmong-flute-style D-minor pentatonic phrase with grace slides, vibrato and valley echo.

**Materials (`hg_lib/materials.py`, node graphs via MaterialEditingLibrary)**
- `M_HG_Terrain`: auto-slope grass/scrub ↔ limestone + moss, wet dark banks above the river.
- `M_HG_WetAsphalt`: aggregate, noise puddles at roughness 0.05 that mirror the low sun.
- `M_HG_RiverWater`: SingleLayerWater, emerald absorption/scattering, two panning wave normals.
- `M_HG_KarstRock`: limestone with vertical rain stains and moss on up-facing ledges. `M_HG_Bush`, `M_HG_RoadPost`.

**Scene (`hg_lib/world.py`)**
- Nanite landscape + `AHGInstancedScatterActor` (HISM): ~12.5k canyon cliffs, ~380 cone summits, ~11k boulders,
  ~55k shrubs, ~930 posts on the valley edge, rocks under Mỏm Đá Tử Thần. Per-component cull distances
  (boulders 600 m, shrubs 260 m, posts 220 m) do the job of a CullDistanceVolume.
- Golden hour: sun 9° above the horizon at 3900 K with light shafts, VolumetricCloud layer at 750–1100 m
  (sea of clouds below the 1200 m pass road), valley + volumetric fog, filmic PostProcessVolume.
- `AHGViewpointActor` ×3: `[V]` inside a check-in ring → CineCamera orbit (crane-in), letterbox + name/altitude card,
  flute theme; `[V]` again or 28 s to return.

**Known limits:** written without a local UE 5.5 build (engine not installed yet). No CullDistanceVolume actor
(brush volumes can't be sized from Python reliably — HISM cull distances are used instead). Megascans can replace the
procedural meshes later by swapping the meshes passed to `world.build_scatter`.

---

## UE 5.8 retarget (`chore/ue-5.8`)
Project switched from 5.5 to 5.8 at the user's request (`EngineAssociation`, `BuildSettingsVersion.Latest`,
`EngineIncludeOrderVersion.Latest`, launcher script paths).

---

## CP04 — Living world (`cp-04-12hr`)

**Rebuild:** run `Scripts/Checkpoints/build_cp04.py` → `/Game/Maps/L_HaGiang_CP04` (= CP03 scene + the systems below).
`gen_assets.py` now also synthesizes `SW_HG_AirHorn`, `SW_HG_Scream`, `SW_HG_Moo` (formant / reed synthesis).

**C++**
- `AHGGroundVehicle` auto-drive: pure-pursuit steering on the road spline, right-hand lane offset,
  curve speed limit v = √(a·R) from the tightest radius ahead, convoy gap keeping, pawn sweep → brake,
  turn-around / loop at road ends. Stops automatically when the player hijacks the vehicle (`[F]`).
- `AHGTrafficSpawner`: convoys of 8–12 Honda Wins (`FHGConvoySpec`) + dump trucks (`FHGTruckSpec`).
- `AHGBikePawn`: NPC Easy Rider in a reflective vest (hidden when the player rides), backpacker in a red T-shirt with
  the yellow star, nylon-wrapped backpack; screams with arms up near drop-offs (>25 m within 6.5 m sideways at
  >40 km/h) or when airborne.
- `AHGTruckPawn`: blue Thaco-style dump truck, air horn before blind hairpins (R < 30 m ahead) with the player near,
  or when its lane is blocked.
- `AHGCowActor`: road-blocking cows that wander, graze, follow the terrain and moo at the player.
- `AHGTouristNPC`: check-in crowds (backpacker / Easy Rider / local) looking at the view and taking phone photos.
- Happy Water: `AHGHappyWaterPickup` (2 L corn-liquor bottle) → `UHGHappyWaterSubsystem`: 12 s of 2× nitro,
  inverted steering/walking, unbound `PostProcessVolume` (chromatic smear, motion blur, bloom, colour wobble) +
  `UHGDrunkCameraModifier` (rolling horizon, breathing FOV). HUD countdown.

**Test:** follow / overtake the Thẩm Mã convoy, steal a bike with `F`, meet the trucks on the hairpins, slalom the
cows, grab a bottle, ride the rim at speed to hear the passenger scream.

**Known limits:** not compiled yet (UE 5.8 still installing). Greybox NPC bodies; traffic loops by turning around at
the road ends.

---

## CP05 — Full GTA, 5-star wanted (`cp-05-24hr`)

**Rebuild:** run `Scripts/Checkpoints/build_cp05.py` → `/Game/Maps/L_HaGiang_CP05` (now the default map,
`AHGGtaGameMode`). `gen_assets.py` adds seamless loops: bike / truck / boat engines, police siren, helicopter rotor.

**C++**
- `UHGGtaHudWidget` (UMG `UserWidget`, drawn in `NativePaint`, no widget assets): `500.000 VNĐ`, 5 wanted stars
  (flash while you are hiding), GPS minimap rotating with the camera (road spline, convoy / truck / boat / cow /
  police / helicopter / Happy Water blips, viewpoints clamped to the edge), dial speedometer, GTA location card
  ("ĐÈO MÃ PÍ LÈNG · MỎM ĐÁ TỬ THẦN", altitude), flycam letterbox, `[F]` / `[V]` prompts, Happy Water and
  "BỊ BẮT!" banners. `AHGGtaHUD` + `AHGGtaGameMode`.
- `UHGWantedSubsystem`: heat from impacts reported by player vehicles (road posts tagged `RoadPosts`, convoy bikes,
  trucks, cows, tourists, police) → stars at 0.5 / 2 / 4 / 7 / 11 heat. Police bikes per star 1/2/2/3/4, helicopter
  from 3 stars; heat cools after 6 s unseen (no unit within 150 m); stop next to a police bike for 2.5 s → BUSTED, −200.000 VNĐ.
- `AHGPoliceBikePawn`: white patrol bike, flashing red/blue lights, looping siren; races along the road spline,
  switches to direct pursuit within 50 m and matches your speed to box you in.
- `AHGPoliceHelicopter`: follows behind/above the player with terrain clearance, banks, spinning rotors, rotor
  loop, 300 000 cd volumetric searchlight (scattering ×12) sweeping the player through the canyon fog.
- `HGAudio`: code-made `USoundAttenuation` (cached per radius) for every one-shot and loop; engine loop pitch
  follows speed with fake gear shifts.

**Test:** knock down a few road posts on the Mã Pí Lèng rim → stars, sirens behind you; reach 3 stars and drop into
the Tu San descent to see the helicopter beam in the fog; stop next to the police to get busted.

**Known limits:** nothing compiled yet (UE 5.8 still installing) — compile + fix is the next step for all checkpoints.

---

## Photoreal pass (`improve/photoreal-viewpoints`)

Goal: make the three viewpoints read as Ha Giang, checked with an offline renderer while UE 5.8 installs.

- `Scripts/Tools/render_viewpoints.py`: numpy heightfield ray-marcher (golden-hour sun with terrain shadows,
  slope-based limestone/grass/road albedo, emerald water with Fresnel, aerial perspective + valley fog).
  Renders player-eye and flycam shots of each viewpoint → `Generated/Previews/` (~1 s per 1280×720 frame).
- Terrain v2 (`gen_heightmap.py`):
  - fengcong karst from warped Worley cells (steep cones, saddles, dolines) instead of isolated bell cones;
  - vectorised droplet hydraulic erosion on a 4 m grid (800k droplets): gullies, debris fans, softer valley floors;
  - a broad hillside under Dốc Thẩm Mã so the 9 hairpins sit on a real slope (was an elevated "tongue");
  - steeper Tu San gorge (lower profile exponent) with warped two-scale limestone strata ledges;
  - Thẩm Mã check-in pad moved to an overlook terrace on the downhill side; slimmer Mỏm Đá Tử Thần ledge.
- Lighting: sun from the south-east at 13° raking along the N–S canyon (light shafts down the gorge, lit rim road,
  spur and hairpins); clouds moved above the peaks; dense valley mist below ~950 m for the sea-of-clouds look
  (fog falloff corrected to UE units).
- Terrain material: rock only on >~50° slopes plus noisy outcrops, greener scrub with dry-grass/maize patches.

---

## Real-places pass (`fix/ue58-compile`)

Goal (user): "as we focus on only some places, make them exactly as much as possible". The hand-modelled
4 km map is replaced by real data.

- `Scripts/Tools/gen_heightmap.py` (new; the procedural generator is kept as `gen_heightmap_procedural.py` for
  its helpers): Copernicus GLO-30 DEM (bicubic, float64 geo-coordinates) + OpenStreetMap (`Data/Geo`, fetched by
  `Scripts/Tools/fetch_geo.sh`, not committed). 5.89 km frame at true scale centred on 23.2450 N 105.4040 E,
  1.46 m cells, north = -Y.
  - Route: real Doc Tham Ma bends (1.5 km real-terrain patch pasted SW of Ma Pi Leng) -> least-cost link road
    (Dijkstra, grade-penalised, avoids doubling QL4C) -> real QL4C from the Panorama north past the Youth
    Volunteers monument -> real Ta Lang road (OSM way 855373895, 35 hairpins) -> dock on the Nho Que.
    QL4C beyond the route is carved and meshed as stub roads; AI / police / minimap use the longest road.
  - Water: OSM reservoir polygon + river line + the DSM's low valley floor; Nho Que 1 dam from OSM, 18 m drop
    below it; rotated water planes along the reservoir.
  - Sub-DEM detail (slope-scaled multi-octave relief, light droplet erosion), cut/fill road carving with a
    smoothed bank target, pads at the real viewpoint / cafe / monument positions, Mom Da ledge on the rim
    nearest the Panorama (its exact position is not documented).
- Scatter, offline renderer, terrain material and water now read `cell_m` / `half_m` from the layout; hamlets
  from OSM residential areas; forest in the Tham Ma patch, kapok low in the canyon.
- `Scripts/hg_lib/shots.py`: one shot list shared by the offline renderer and the UE screenshot runner (UE
  cine camera FOV now set through the focal length - it ignored `field_of_view`).
- Weather locked (docs/ART_BIBLE.md): late March 10:30, clear, light haze, sun 50 deg from ESE; no
  VolumetricCloud on M1. Benchmark (M1 Pro, 1600x900, CP05): 71-76 FPS at spawn and all viewpoints.
- Roads: asphalt QL4C with lane paint, narrow concrete Ta Lang road (secondary surface on the same spline).
- Landmarks: Youth Volunteers monument, Panorama cafe + name stone + drink stall, low stone walls / partial
  Mom Da fence, Tham Ma coffee shop + Hmong pack horses (`AHGCowActor::bHorse`), dock ticket booth + cafe deck,
  peach blossom near the hamlets.
- Gameplay pass (second agent, merged `cecb434`): `Scripts/Tools/validate_layout.py` offline checker (42 FAIL ->
  0 FAIL), `Scripts/hg_lib/placements.py` shared placement math, lane-keeping AI (Stanley) for convoys / trucks /
  police, trucks kept off the Ta Lang concrete road, 7 convoys at the three viewpoints, boats on navigable water,
  police spawn >= 150 m away, helicopter terrain probing, speed-zoomed minimap with north marker.
- Data fixes after the merge: road output every 4 m with hairpins rounded, main route carved after the QL4C stubs,
  Tham Ma patch at real altitude, concrete start measured on the UE spline. Final verification build (M1 Pro,
  1600x900, CP05): 82-85 FPS; validator 106 checks, 0 FAIL, 4 WARN (data notes).

---

## Round 1 - playable riding (`improve/gameplay-realism`)

Root causes found with the scripted playtest (`-hgplaytest`, real key events through Enhanced Input):
- **Bike stopped dead** (W held 6 s -> 14 m, speedo ~1 km/h): input and acceleration were fine; the collision
  box bottom sat only 10 cm above the ground, so on banks / camber a corner started each sweep inside the
  landscape. A sweep that starts penetrating is blocked at t = 0 every frame -> frozen for good, and throttle
  kept adding speed while "airborne" (speedo 19-60 km/h standing still). Same cause froze the auto-driven bike.
  Fix: `AHGVehicleBase::SweepMove` (MTD depenetration + retry, shared by bikes and boats), bike box raised
  (bottom 34 cm above the probes' ground), landing within one frame of fall, no drive force in the air, speed
  clamped to the distance actually covered when blocked, AI stuck watchdog (re-placed on its lane after 2 s).
- **Parapet across the road**: the Doc Tham Ma stone parapet row (fixed offsets) stood on the hairpin below
  the overlook and stopped every bike. Now `placements.tham_ma_parapet` drops blocks within 0.8 m of the road;
  `validate_layout.py` checks it (props category).
- **Grey checker everywhere on greybox actors**: in UE 5.8 `/Engine/BasicShapes` default to WorldGridMaterial
  (no "Color" parameter), so every MID tint was ignored. `HGPaint::MakeMID` starts from BasicShapeMaterial;
  the player character is painted too (it had no material at all).
- **Spawn camera pitch 0**: `AGameModeBase::FinishRestartPlayer` resets the control rotation after possession,
  undoing the pawn's pitch. `AHGGtaGameMode` re-applies -15 deg.

Riding / feel: Honda Win retune (68 km/h flat out, engine curve 0-60 in ~5 s, 1.1 g brakes, engine braking),
surfaces for the player (asphalt / Ta Lang concrete x0.7 / off-road x0.4 + drag, HUD label), smoothed key
steering, chase camera follows half the slope + speed FOV 88-100, [R] puts the bike back on the nearest road
lane, parked bikes angled 30 deg towards the road. HUD controls line readable at 720p and context-specific.
Perf: TAA instead of TSR (TSR was 3-4 ms of a 17 ms GPU frame), gameplay lights excluded from Lumen GI and
distance-culled, small greybox parts culled at 60 m without shadows. Playtest extended: W-only straight,
key-ridden Tham Ma + Ta Lang, [R], auto stages, board the dock cano with [F] and ride on the keys; FPS split
into game / render / GPU ms; `-hgprofilegpu`, `-hgplayboat`.

Playtest (M1 Pro, 1280x720 window, CP05): W 6 s on the straightest stretch 63 m / 60 km/h (was 14 m / 1 km/h);
top 61 km/h on asphalt, 48 km/h on the concrete hairpins; 0 s off road on the keys; average 66-70 FPS over
the whole run (was ~57; every check >= 59).
Open: the dock cano is moored inside the pontoons; the test bot can't steer out around Pontoon_0 yet.

## Round 2B - visual realism (agent B, `worktree-agent-a492f1a4f1c84f0fb`, merged A's ea0b965)

Root causes:
- **Stair-stepped terrain / flat green slab** (09_W_straight, 08_after_R): not the heightmap (bicubic DEM, 2.3 cm
  steps) and not DistantRanges (5-14 km ring). The playtest shot was taken one frame after a 1.6 km teleport, and
  the landscape heightmaps are *streamed*: the vertex shader point-samples whatever mip is resident, so until the
  streamer catches up the terrain is built from a coarse mip -> 6-50 m terraces and flat slabs. Players hit the
  same after any respawn / [R] / teleport. Fix: `UHGLandscapeBuilder::PinLandscapeTextures` (NeverStream on the
  64 heightmap / weightmap textures, ~90 MB) called by `scenes.finish_level` before saving. (A also moved the
  playtest screenshot before its teleports.)
- **Red smear / white halo**: the sun is a non-physical 10 lux, but the bike lights were real candelas: tail
  15 / 60 cd lit the road at ~26 lux (2.6x the sun) metres around the bike; the 1500 cd headlamp sat next to the
  rider's arms and hat; police 400 cd, Happy Water 30 cd. Now 4 cd head / 0.25-1 cd tail / 3 cd police /
  3 cd pickup with short radii and no specular / volumetric, and the lamps read through small unlit emissive
  lenses (`M_HG_Lamp`, MID "Intensity": brake light, flashing light bar) that cast nothing and stay out of Lumen.
- **"Orange block" rider**: the passenger's 52 cm orange rain cover hid everything in the chase view.
- **Washed-out first pass of the new people / bikes**: the static-mesh build stores vertex colours as sRGB bytes
  but the material's VertexColor node returns the bytes as-is (linear 0.02 black came out 0.15 grey):
  `FHGMeshKit` pre-decodes.
- **Oil-stain asphalt**: `wet_asphalt` lerped to near-black "puddle" patches wherever the 3 m noise passed 0.6.

New: `HGMeshKit` (runtime-built merged vertex-colour meshes: box / tube / ellipsoid / pleated frustum / torus,
cached per archetype, one draw per part, `M_HG_VertexColor` with surface class in UV.x and a "Tint" mask in UV.y),
`HGPeopleBuilder` (`FHGPersonRig`: core + head + arms + legs + scarf tail on pivots, procedural walk / idle /
photo / wave / scream / steer-and-lean animation, off past 60 m; looks: H'Mong woman with pleated batik skirt,
leg wraps, head wrap, basket or tho cam bag; local men; backpackers with yellow duck-bill "non vit" helmets;
Easy Rider guides in 4 tour colours with tho cam scarves; player; CSGT police), `HGBikeBuilder`
(`EHGBikeModel` Win / Wave / Dream / Sirius / EasyRiderWin / LocalLoaded: spoked wheels with knobby or street
tyres, raked fork, tank, seat, big rack, exhaust with heat shield, shocks, lamps, plate, flag; grass / firewood /
pig-basket loads), `HGReviewDirector` (`-game -hgreview`: close-up shots of every bike model, riders, animals,
truck, boat and crowds -> Saved/HG_CI/Review). Cows / pack horse and the Thaco dump truck rebuilt with the kit;
cruise canoes open-sided with benches, boatman and tourists. Asphalt: bleached grey, wheel paths, crack seals,
rare low-contrast patches, faded paint. Ta Lang concrete: per-pour slab tone, broom grain, sawn joints, tyre
grime, red dust. Haze: thinner / bluer height fog starting at 25 m + SkyAtmosphere aerial perspective x2.2.
Check-in ring painted as a dashed circle instead of a 4 m yellow disc.

API for traffic: `AHGBikePawn::SetBikeModel(EHGBikeModel, Seed)`, `SetTourGroup(0..3)`,
`AHGBikePawn::RandomTrafficModel(Seed)` (weighted mix); call right after spawning (or any time: visuals rebuild).
Without a call a bike picks from its name (passenger -> Win / EasyRiderWin, else the local mix).

Perf (M1 Pro, 1280x720, every FPS run under /tmp/hg_perf.lock): playtest before (65f6559) 67.0 FPS average,
checks 64-76; after (this branch incl. A's ea0b965) **69.2 FPS average**, bike checks 68-85, boat stage 51-68
(was 59-66). -hgperf spots 75-80 / 66-74 FPS; `-hgnokit` A/B (no kit people / bikes / animals at all) gives
the same numbers within run-to-run noise (+-7 FPS on this machine). Budget: one draw per person part (core /
head / 2 arms / 2 legs / scarf), bike = body + 2 wheels + 2 lamps; people culled at 150 m (boat crews 70 m), only
core + legs cast shadows, poses at half rate past 20 m and frozen past 60 m.
validate_layout: 119 checks, 0 FAIL (4 pre-existing road / terrain / heli warnings).
Self-review fixes (from -hgreview / playtest shots): bleached colours (sRGB vertex colours), backpacker floating
alone on parked bikes, knobby tyres on scooters, step-through shield with daylight round it, mannequin legs /
flipper feet, 4 m yellow check-in disc, 30 cd yellow pool under the Happy Water bottle, the player's rental
turning into a luggage-stacked tour bike, faded-to-nothing centre line, giraffe-necked pack horse.
Open: Ta Lang slab joints still hard to read from the chase camera; DistantRanges is a flat blue band; people
are mannequin-like at close range (no faces / hands detail); boat crews sit under the roof and are mostly hidden
from the high boat camera.
