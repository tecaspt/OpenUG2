# M7 host/build integration

[observed] All anchors refer to ea2c9c4. main.c and Makefile are read-only to Task F.

## Priority 1 — opt-in drag longitudinal adapter

[inferred] Next to main.c:1352 (`g_vehicle_source`) add:

```c
static PhysDragConfig g_drag_config;
```

[inferred] Immediately after the initial g_vehicle_source assignment at main.c:5018 add:

```c
g_drag_config = (PhysDragConfig){0};
if (physics_from_global)
    phys_drag_config_read(&g_drag_config, &g_vehicle_source,
        globdata, globlen, carname, 0, 0);
```

[inferred] Immediately after the candidate assignment at main.c:7269 add:

```c
g_drag_config = (PhysDragConfig){0};
if (candidate.has_physics)
    phys_drag_config_read(&g_drag_config, &g_vehicle_source,
        candidate.globaldata, candidate.globallen, next_car, 0, 0);
```

[observed] The raw initial global buffer is freed at main.c:5071; the candidate buffer is temporary.
[inferred] Capture the radius/coefficient at these two locations before freeing buffers. Config's
source pointer borrows the stable global g_vehicle_source, never the temporary candidate/local pa.

[inferred] At GF_CMD_ARM_EVENT, before the evi check at main.c:7179, construct:

```c
PhysDragConfig launch_drag = g_drag_config;
launch_drag.power_level = g_dbg.perf_power_level;
launch_drag.transmission_level = g_dbg.perf_transmission_level;
```

[inferred] After the `evi < 0` error branch, before its existing success `else`, insert:

```c
else if (world.city.ev[evi].info.kind == N2_RACE_DRAG &&
         !phys_drag_config_valid(&launch_drag)) {
    snprintf(g_dbg.race_error, sizeof g_dbg.race_error,
             "Drag wheel/engine/resistance data is unavailable.");
}
```

[inferred] Replace the whole drag_gain block at main.c:8332–8339 with:

```c
PhysVehicle driven = g_vehicle;
```

[inferred] Keep the Stage 1 branch at main.c:8344 unchanged. Between its closing brace and the
existing `else dmag = phys_drive_step(...)` at main.c:8355 add this branch:

```c
} else if (drag_active && race_state == 1) {
    PhysDragConfig drag_config = g_drag_config;
    drag_config.power_level = g_dbg.perf_power_level;
    drag_config.transmission_level = g_dbg.perf_transmission_level;
    dmag = phys_drag_step(&drag_gear, &drag_config, carpos, vel, &heading, &speed,
        throttle, steer_filtered, handbrake, drag_shift,
        &surf_now, &driven, g_ride_ready ? &g_ride : NULL);
```

[inferred] Do not call phys_manual_step again in the host: phys_drag_step advances its shift timer,
RPM, limiter/heat state once per fixed tick. Preserve raw throttle for telemetry. In the existing
drag audio branch at main.c:8389 set the load as follows, so torque cuts remain audible:

```c
g_engine.load = (drag_gear.shift > 0 || drag_gear.failed ||
                 drag_gear.rpm >= g_vehicle_source.limiter_rpm)
    ? 0.0f : fmaxf(0.0f, throttle);
```

[inferred] No new SRC entry is required for priority 1; both new functions are in physics.c. Existing
legacy/ordinary physics APIs remain available. NOS force beyond the existing host behavior is not
implemented by this adapter. The orchestrator must run the full game-after-fix check after wiring.

## Exact new checks

```make
drag-physics-test:
	@mkdir -p build
	$(CC) -O0 -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wno-unused-function -ffunction-sections -fdata-sections -Isrc tools/drag_physics_test.c src/physics.c -Wl,--gc-sections -lm -o build/drag_physics_test
	./build/drag_physics_test

drag-speed-check:
	@mkdir -p build
	$(CC) -O0 -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wno-unused-function -ffunction-sections -fdata-sections -Isrc tools/drag_speed_check.c src/physics.c src/drivetrain.c src/drive_stage1.c src/braking.c src/resistance.c src/lateral.c src/contact.c src/body_response.c src/drive_contact.c -Wl,--gc-sections -lm -o build/drag_speed_check
	./build/drag_speed_check
```

[inferred] Repeat synthetic compilation with -DN2_GLES. The speed check reads OPENUG2_DATA at
runtime (exit 77 if unset); it compares adapters on a synthetic level ground query, with full configured ordinary dynamics,
not world collision or a complete race replay. Run from an external report directory and store output there.
All builds/runs require the owner's memory check, heavy lock and nice priority 10.
