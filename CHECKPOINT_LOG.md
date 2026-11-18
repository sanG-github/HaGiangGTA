# Checkpoint Log

Active-work time is read from `ACTIVE_WORK_TIMER.json` (`active_work_seconds`);
time spent waiting for a usage-limit reset is excluded (`cooldown_wait_seconds`).

| # | Branch | Tag | Milestone | Status |
|---|---|---|---|---|
| 01 | `checkpoint/01-10min-greybox` | `cp-01-10min` | 10 min | done (~7 min active) |
| 02 | `checkpoint/02-1hr-heightmap-bike` | `cp-02-1hr` | 1 h | done (~26 min active) |
| 03 | `checkpoint/03-6hr-3viewpoints-photoreal` | `cp-03-6hr` | 6 h | pending |
| 04 | `checkpoint/04-12hr-living-world` | `cp-04-12hr` | 12 h | pending |
| 05 | `checkpoint/05-24hr-full-gta-5star` | `cp-05-24hr` | 24 h | pending |

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
