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

---

## Round 2 - traffic, boats, camera, audio, road furniture (`improve/gameplay-realism`)

Coordinator findings fixed:
- **Black boat frames (A)**: the cano camera (3.2 m socket) ended inside our own canopy / hull whenever the arm's
  collision test pulled it in against a pontoon or moored boat. Socket 4.5 m above the boat (always over the
  2.1 m canopy), never pitched up from below, lifted to stay >= 1 m over the water; PlayCheck flags
  `CAMERA BELOW WATER`.
- **W_straight shot with no bike, 0 km/h (B)**: the playtest teleported the bike to the next stage in the frame
  the screenshot was requested, and the lagging spring arm swept across the map. Teleports now wait >= 1.5 s
  after a shot and `AHGVehicleBase::SnapCamera` skips the arm lag for 3 frames on every teleport (also [R]).
  (Stair-stepped terrain after teleports: landscape mip streaming, fixed by agent B.)
- **Bike on the rim shoulder (C)**: lane 170 -> 140 cm from the centre, bike lane-keeping gain 2.2; the playtest
  logs the lane error: mean 0.03-0.05 m on Tham Ma / Ta Lang, larger only during overtakes / go-arounds.

Road furniture (user bugs):
- **Signs in the road**: gen_scatter placed posts / chevrons / rails from its own 2 m polyline before the
  hairpins were rounded for the UE spline: 51 chevrons, 5 posts and 1 wall stood on the carriageway.
  `placements.road_furniture` now places them against the spline and its real width (concrete included):
  posts 0.6 m, chevrons 0.9 m (1.5 m fallback) outside the edge on the outside of the bend.
- **Guardrails / rim walls off the bends**: rebuilt as 4 m chords of the offset edge curve (bisected to exactly
  4 m, smoothed normals so the curve has no jumps at polyline vertices), end to end, drop side and outer side of
  bends only. validate_layout: 8 furniture checks (clearance, follow-the-edge band, joints) - 119 checks, 0 FAIL.
- Layout bikes stood on the road (start bike at 38 m, Ma Pi Leng bike in the lane, which stopped convoys): now
  parked on the nearest valid shoulder spot; validate checks the whole bike outline.

Boats: player cano berthed in a clear lane on the cruise route facing the gorge (`placements.dock_departure`,
validated); [R] back onto the cruise line; playtest rides dock -> Ma Pi Leng bend (3.7 km): reached in 218 s.

Camera: mouse orbit on every vehicle (drifts back behind after 1.5 s), [C] look-back (a cut, not a swing).

Traffic: lane-line obstacle probe (the straight nose probe saw bikes parked on the outside of bends and stopped
convoys at viewpoints), overtaking on clear stretches (no oncoming within 150 m, R > 40 m), downhill vehicle
yields on hairpins, go-around of anything standing in the lane for 3 s, player tracked as traffic; solo bikes
(oncoming + faster ones catching up) and a QL4C truck kept around the player out of sight; trucks no longer
wedge on banks (box 50 cm above ground, land on the probe that is on the road); stuck AI turns round instead of
hopping forward after 3 tries; convoys turn 60 m before the dock yard. Bike variant hook
`AHGTrafficSpawner::ConfigureBikeVariant` for agent B's bike builder. Playtest counters: near misses, AI dead
stops (with the reason), oncoming met, overtook us, AI overtakes / yields / go-arounds / unsticks.

Audio: generated wind / birds+insects / tyre loops (gen_assets.make_ambience), driven by speed, altitude and
surface; engine + ambient levels logged at every check.

Perf: `-hgperf` now benchmarks the 9 review cameras (Generated/Heightmaps/HaGiang_review_cameras.json from
`shots.py`) with game / draw / GPU ms and a screenshot each (Saved/HG_CI/Perf). AI further than 450 m from the
camera ticks at 10 Hz, minimap blips rebuilt at 10 Hz (ThamMa drone 56 -> 114 FPS); Lumen: no surface-cache
feedback, half the adaptive screen probes, off-screen shadows from the global SDF (gorge boat ride 43-50 ->
64-74 FPS). Distant ranges: foothill apron from the map edge (the rim views looked past the map into empty haze).

Numbers (M1 Pro, 1280x720 window, CP05, perf lock held): `-hgperf` review cameras 62-114 FPS - Spawn 78,
ThamMa eye 75 / drone 114, MomDa ledge 79, MaPiLeng plaza 82 / drone 111, NhoQue from rim 73, TaLang dock 62,
TuSan boat 58 (65 in the previous run; agent B's CP05 rebuild overlapped the end of this one), TuSan flycam 81.
Full playtest average 67.1 FPS (57 before the round's perf work); W 6 s on the straight 63 m / 60 km/h; boat
dock -> Ma Pi Leng bend in 202 s, top 83 km/h; 4 near misses, 26 oncoming bikes met, 5 AI overtakes, 67 yields.

Known / open: AI still re-places itself ("unstick") at the 47 decimated hairpins left in road.points_cm (e.g.
2.77 km: the spline turns 128 deg in 5 m, bikes swing onto the rim wall) and where the road profile is off the
landscape (dock spur, up to 14 m): road data in gen_heightmap, next round. The test window was sometimes shrunk
to 679 px by another Unreal instance starting; the playtest now restores 1280x720 when that happens.

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

---

## Round 3 - road data, missions, boat hire, merge of B (`improve/gameplay-realism`)

Road data (gen_heightmap.py, road functions only: `enforce_min_radius`, `vertex_radius`, `grade_limit`,
`fit_to_shelf`, the route smoothing / profile block in `generate()`, `pad()` start point, the dock slipway / yard
pads and a final road-core re-level):
- **Hairpins**: position-based relaxation caps the centreline curvature at R 9 m (sagitta of every vertex over
  its neighbours' chord, excess spread outwards so narrow hairpins grow a balloon), keeps the two legs >= 8 m apart
  and away from the rest of the route. Tightest bend 2.1 m -> 9.1 m, 47 cusps -> 0, max turn 6.4 deg/m.
- **Grades**: the DEM under the OSM line of the Ma Pi Leng rim road (a shelf cut into a cliff) climbed up to 87 %
  (30 % averaged over 400 m) - convoys stalled there and were re-placed. `fit_to_shelf` slides the line up to
  15 m sideways onto the slope at the height a 14 %-limited profile wants (never closer than 12 m to another leg),
  then the profile is grade-limited: max 87 % -> 20 % (11 segments over 15 %).
- **Floating road**: the dock pad flattened the whole 147 m spur to dock level (road up to 14 m above the
  landscape), the yard sat 0.45 m over the road and viewpoint pads started 2 m from the centreline. Slipway pad
  is the last 12 m only, yard at road level, pads start at the road edge, and the road core is re-levelled last.
  Road vs landscape (bilinear, as rendered): worst +14 m -> +0.16 m.
- validate_layout road checks: minimum radius, maximum turn per metre, maximum grade, legs apart, profile
  (0.3 m) - all PASS; 115 checks, 0 FAIL.
- AI: 0 dead stops and 5 re-placements (all trucks: a cow on the road and a truck U-turn at its range end)
  in the full playtest, against 15 stops and 47-69 re-placements before.

Missions (`UHGMissionSubsystem`): xe om Dong Van -> Ma Pi Leng (150,000 + tip), Ma Pi Leng photo [P] (50,000 +
framing bonus, shutter flash), police chase to the Ta Lang dock (3 stars, 300,000, busted = fail). HUD objective
line, centre banner, magenta target / yellow job markers on the minimap. Playtest: all three PASS.
Boat hire at the dock: [F] on foot near the slipway pays 100,000 VND and boards the cano (no 40 m swim), PASS.
Tour boats steer round the player's cano. River on the minimap. Boat camera: 19 m behind, 3.3 m up, pitch -7
(boat ~1/5 of the frame); canopy hidden when the arm is pulled in. Boat [R] used by the playtest bot when stuck.

Merge: agent B's branch (382d10c) merged; traffic wired to B's bikes (convoys EasyRiderWin + one tour group per
convoy, solo traffic RandomTrafficModel); parked bikes plain Win, empty (the xe om job adds the passenger).

Perf: `-hgperf` prints a SUMMARY PASS/FAIL (any camera under 60 fails), `-hgprofile=<camera or check>` dumps a GPU
profile there, rays flag objects clipping a review camera. Review cameras are pushed clear of building footprints
(the Ta Lang dock camera was inside the ticket booth: 62 -> 76 FPS). TuSan_boat 67 FPS: Lumen screen-probe gather
4.3 of 10.8 ms GPU. Window-size guard now restores the start size (it was shrunk to 679 px and grown to 1280 px by
something outside the game); tested by shrinking it on purpose.

River width along the dock -> Ma Pi Leng boat route (3.7 km): water surface min 23 m, p10 39 m, median 80 m, max
181 m; gorge 50 m above the water min 57 m, median 274 m; only 1 of 126 route points narrower than 60 m at that
height - the dark narrows are shade, not a slot narrower than the real river.

Self-review fixes this round: objective line moved below the flycam prompt (they overlapped), context controls
line per vehicle ("R về luồng" on the water), water planes trimmed to the map (the below-dam plane floated 590 m
past the end of the terrain as a flat lake in the rim views), distant-ranges apron raised to 900-1600 m and kept
outside the square map, parked bikes left empty for the xe om job, head-on AI deadlock on hairpins resolved (both
squeeze to their own edge), and the scripted runs pin the viewport at 1280x720 px (the window came up at 676, 1019
or 1280 px for the same -resx=1280, so earlier FPS numbers were not all at the same resolution).

Numbers (M1 Pro, perf lock held, 1280x720 window): `-hgperf` SUMMARY PASS, min 67 FPS (Spawn 76, ThamMa eye 76 /
drone 110, MomDa 79, MaPiLeng plaza 87 / drone 113, NhoQue rim 81, TaLang dock 76, TuSan boat 67, TuSan flycam 83).
Later locked runs read 50-59 at a few cameras while another Unreal instance (CP05 build + clang, 190-240 % CPU)
ran alongside - builds don't take the lock - so treat single runs on this machine with care. Playtest: missions
3/3 PASS, boat hire PASS, boat dock -> Ma Pi Leng bend 198-265 s, AI 0-14 dead stops (head-on deadlocks at U-turns,
now resolved) and 5-7 re-placements per session (47-69 in round 2). Distant foothills are now their own olive mesh
(the hazy ranges material made the apron read as a lake); the Mom Da review camera moved 5 m so the Panorama cafe no
longer fills its corner.

## Round 3B - banks, gorge shade, ranges, Ha Giang dressing (agent B, `worktree-agent-a492f1a4f1c84f0fb`, merged A's ee9cb3b)

Root causes:
- **Brown "curtain" banks** (A's 32_boat_053): gen_heightmap carved the river bed straight to -1.5 m at the shore
  and lifted the first ~12 m of land to +0.6..+5 m, on a near-river water mask built from 5.8 m blocks
  (`blur_coarse > 0.01`): every shoreline was a 2-7 m vertical, stair-stepped face, painted with a flat ochre
  stripe. Measured: low-shore land cell -> neighbouring water cell drop, median 11 m / 90th pct 27 m.
  Fix (terrain part of gen_heightmap step 4): numpy distance rings (`ring_distance`, no scipy) instead of the
  block mask; DEM specks of land inside the river folded into the water (islets 4401 -> ~240 cells outside the
  dock); a 4 m shelf under the water (-0.6 m at the edge, boats float to it); a sloped, irregular rocky drawdown
  bank (35-55 deg) within ~25 m of the shore on ground up to ~30 m above the water; cliffs taller than that
  (Tu San) still drop straight in. Now: median 0.9 m, 90th pct 3.5 m. The Ta Lang dock keeps the round-2 shore
  (round-2 carve blended in within 150-220 m of the dock; water level from the round-2 mask): A's pontoon /
  moored-boat / departure layout is tuned on it - my first version moved a pontoon into the departure lane and
  the playtest boat stayed boxed in at the dock. Material: irregular band top (+-1.7 m), grey-brown mud with
  pebbles and faint old waterlines, water-stained rock on steep faces, dark wet line.
- **Near-black Tu San narrows** (35_boat_128): the auto-exposure floor was EV100 0.6; with the non-physical
  10 lux sun a sky-lit-only slot canyon sits near EV -1, so exposure could not adapt. Floor -1.2, Lumen sky-light
  leaking 0.16 (bounce fill only where the sky is occluded), mild local exposure, valley haze (second fog layer).
- **Flat blue horizon band**: one ring mesh, one colour. Now gen_ranges.py: four ridge rings (6 / 8 / 10.5 /
  13.5 km, peaks rising with distance), sloped gullied flanks so the sun shades them, per-layer material with
  height-graded valley haze; plus the second fog layer in the valleys.
- **Sign lettering**: TextRender's distance-field font has no Vietnamese stacked diacritics ("D C TH M MÃ");
  a world-space UMG widget did not draw for runtime-created components. Lettering is baked by gen_signs.py
  (system Arial) into masks for masked sign materials.

New: `HGDressingProp` (H'Mong house: stone / rammed-earth walls on a stone platform, yin-yang tile roof with
channel tiles, red couplets, corn drying under the eaves, firewood, dry-stone yard wall only on gentle ground;
Ma Pi Leng flag pole with a flapping national flag; carved name stone; wooden boards; terraced young corn as one
HISM per hamlet, on the 2.6 m contour treads, off the banks and clear of houses) placed by the new
`Scripts/hg_lib/dressing.py` (34 houses replace the grey-roofed scatter houses; 8 corn terraces; MPL flag + stone;
Tham Ma and Ta Lang boards). No buckwheat: it flowers Oct-Nov, the art bible's season is late March. Road:
segment start / length (per-segment MID, see below) -> dashes (3 m / 6 m) and 4 m concrete pours per real metre with
per-pour tone; gravel verges from a baked shoulder mask (T_HG_ShoulderMask) in the terrain material. People:
squatting sellers behind a tray of goods, H'Mong kids, women carrying babies on their backs, thumbs, eyebrows.
`-hgreview` also shoots vistas, houses, corn, flag and signs.
Coordinator additions:
- **Player cano**: kit Nho Que tourist boat (lofted narrow steel hull, blue with a white sheer stripe and dark
  bottom, plank floor, rails, 6 benches with backrests, wheel console, low red roof with the yellow star on thin
  posts, long-tail outboard, flapping flag on a stern staff). New FHGMeshKit::Loft / Fan. Collision unchanged.
- **"Non la on the player's bike"** (26_auto_ThamMa_21): not the player body - the NPC rider (LocalMan variant 1:
  olive jacket + non la) appeared on top of the player when the playtest's auto stages switch auto-drive on with
  the player aboard. NPC riders now never show on a player-controlled bike.
- **Flat white limestone faces**: bedding strata (warped bands), dark crevices, shrubs on ledges, limestone
  albedo 0.27 -> 0.23.
- **Haze lake past the rim**: real DEM terrain beyond the map (gen_farterrain.py, out to 12 km, 100 m grid,
  tucked under the landscape at the seam) replaces A's one-colour apron in game (apron kept as the editor
  fallback); the synthetic ridge layers moved out to 14-26 km behind it.
- Road distance per metre: custom primitive data never reached the spline-mesh shaders (centre line stayed
  solid), so each road segment now gets a MID with RoadSegStartM / RoadSegLenM; concrete edge dust narrowed.
- Perf protocol: C++ builds and CP05 rebuilds also under /tmp/hg_perf.lock.
Measured (locked, 1280x720): playtest missions 3/3 PASS, boat hire PASS, average 40.2 FPS (Round 2B: 69.2);
-hgperf 60 / 64 FPS. NOT a valid comparison: five orphaned CrashReportClient processes (pids 1523, 3366, 45941,
52701, 74000; from earlier editor/game crashes) each pin a core at ~100% (load ~12 with no Unreal running). Kill
them and rerun the locked playtest + -hgperf before judging the 60 FPS budget. validate_layout: 0 FAIL.
Open: harness crash (SIGSEGV in screenshot readback while the viewport-restore r.SetRes runs) in one playtest.
---

## Round 4 - traffic U-turns, boat tour, menus, upgrade, water GI tier (`improve/gameplay-realism`)

Review answer, 26_auto_ThamMa_21 (convoy at 0 km/h on the Tham Ma bends): a **dead stop**, not a regroup. The
AI "U-turn" at the end of its stretch was a teleport into the other lane (`StartAutoDrive(RoadDistance, -dir)`):
the convoy leader reappeared nose to nose with its own riders on the 9 m hairpins at road 15-100 m and both
probes held 0 km/h ("probe HGBikePawn_27 at 0.0 m" <-> "probe HGBikePawn_21 at 0.0 m"); the riders behind
queued. Trucks teleport-turned at 3.19 km into the cut bank (re-placed 4x in a row) and wedged on a cow at the
lane edge outside their 70 cm probe.

Traffic (HGGroundVehicle):
- Real U-turns. Each end of an AI stretch gets a turn-round spot (`FindTurnPoint`, cached): road >= 4.2 m wide
  (trucks 6.5 m), radius >= 20 m over +-10 m (trucks 60 m over +-20 m), grade <= 10 % (7 %), and for trucks level
  ground 5.5 m either side of the centre = a lay-by (second pass without it if none within 600 m). The vehicle
  brakes to a stop there, waits until `IsTurnClear` (no oncoming vehicle 15 m behind .. 70 m ahead, nobody just
  ahead in its lane, no U-turn within 40 m, no pawn - people, cows, the player - on the arc), then drives a
  half-ellipse across the road (bike 2.8 m reach / 3.2 s, truck 7.5 m / 9 s) into its lane facing back.
- Riders behind a turning vehicle queue (no go-around into its arc, no overtaking it); the dead-stop counter
  allows that wait up to 20 s.
- Head-on deadlock: both squeeze to their own edge AND creep at 5 km/h (a bike only moves sideways by rolling;
  the probe held both at 0 with the bars turned). Go-around of a stopped obstacle uses the mirror lane, not
  1.5 m past it (put bikes on the chevron boards of the hairpins). Truck probe as wide as the truck.
- AI on its lane no longer slides off landscape contacts (road validated on the landscape; box corners on sags
  wedged trucks "grounded 0").

Missions / game feel (HGMissionSubsystem, HGGtaHudWidget, HGGtaGameMode):
- "Thuyen Tu San: cho 2 khach": in the hired cano at its berth, two backpackers board; down the gorge to the Ma
  Pi Leng bend, back to the berth: 250,000 VND + up to 50,000 tip (round trip under 8 min).
- Mission list on Tab / M (jobs, state, pay) with the upgrade: grip tyres 200,000 VND ([B]): the player's bike
  keeps 90 % of top speed on the Ta Lang concrete (70 % stock) and 65 % off the road (45 %), half the off-road drag.
- Pause menu on Esc (game paused): Tiep tuc / Ve diem ngam < name > (A/D picks the viewpoint, dock = the
  slipway) / Thoat game. Intro / tutorial card in Vietnamese on the first spawn (Enter, Space or riding off
  closes it; not shown in -hgperf). Location card with name + altitude once per viewpoint per session.
- Menu keys use their own edge detection (WasInputKeyJustPressed re-fired every frame while a simulated key
  was held and toggled the list 10 times in 0.2 s).

Panorama cafe (review 20_mission_photo_aim): it stood at (-15, 4.5) m in the plaza frame, past the edge of the
12 m pad - 40 m of air under its far end; the drink stall also hung off the edge. Both moved onto the levelled pad
(placements.MPL_CAFE_AT / MPL_STALL_AT, shared by world.py and the footprints); a corner that still overhangs
stands on 0.4 m concrete piers with a deck slab (1 pier, 1.7 m). validate_layout: "<landmark> on solid ground"
for every landmark building (max 0.0 m under a corner now) - 116 checks, 0 FAIL.

Perf lock protocol (also for agent B): `until mkdir /tmp/hg_perf.lock 2>/dev/null; do sleep 30; done` before any
build (Build.sh), CP05 rebuild (ci_run_checkpoint.py) or -hgperf run; `rmdir /tmp/hg_perf.lock` right after.
Functional playtests don't take it but must not overlap a perf run. Every reported FPS is the median of two runs.

Round 4 additions (coordinator): B's Round 3B (57b14cc) merged (89faabe), Generated/ regenerated (gen_heightmap,
gen_scatter, gen_assets, gen_ranges, gen_farterrain, gen_signs), CP05 rebuilt, validate_layout 116 checks 0 FAIL.
Cano chase camera 12 m behind / 3 m up (was 19 m). Harness: the window guard never resizes while a screenshot is
pending or within 1 s of one (B's SIGSEGV in the readback), and -hgperf / the playtest quit only after the last
screenshot's readback (a one-camera -hgperfonly run froze its game thread at quit until the 15 min timeout).
Every Unreal launch in the scripts already passes -nocrashreports. Boat-tour guests are now HGPeople passengers on
the kit cano's hull frame (standing NPCs went through B's new roof).

Results (machine load-contaminated by orphaned CrashReportClient processes: read FPS as relative only).
- Playtests, traffic per session: r4play1 (first U-turn code) 7 dead stops / 2 re-placements; r4play2 0 / 1;
  r4play3 2 / 6. Round 3: 0-14 dead stops, 5-13 re-placements. Remaining causes: bikes on the inside kerb
  (chevrons) of the 267 m / 661 m hairpins, two squeeze-past unsticks, two bikes 15-24 m off the road at 13.5 km
  in far LOD, a cow herd on the road at 3.24 km. Longest U-turn wait 34-236 s.
- Missions 3/3 PASS, boat hire PASS, boat tour PASS (bend 190 s, back 180 s, +300,000 VND), list + grip tyres PASS
  (Ta Lang concrete top speed 48 -> 58 km/h), pause menu PASS, restart at a viewpoint PASS.
- -hgperf, 2 runs (locked): min 63 / 59 FPS; TuSan_boat 67 / 68, MomDa 69 / 59, ThamMa drone 63 / 71.
- Water GI tier A/B at TuSan_boat, 2 runs each: 64 / 65 FPS without, 65 / 65 with, GPU 14.3 ms both ways - the
  view is draw-thread bound (~14.8 ms), so it buys nothing; now opt-in (-hgwatergi=1). 70+ there needs draw-thread
  work (crew / dressing draw calls), not GI.
