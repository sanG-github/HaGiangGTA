# Checkpoint Log

Active-work time is read from `ACTIVE_WORK_TIMER.json` (`active_work_seconds`);
time spent waiting for a usage-limit reset is excluded (`cooldown_wait_seconds`).

| # | Branch | Tag | Milestone | Status |
|---|---|---|---|---|
| 01 | `checkpoint/01-10min-greybox` | `cp-01-10min` | 10 min | done (~7 min active) |
| 02 | `checkpoint/02-1hr-heightmap-bike` | `cp-02-1hr` | 1 h | pending |
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
