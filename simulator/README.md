# LVGL desktop simulator

This folder describes the simulator target. It is intentionally a scaffold because the exact LVGL simulator setup depends on your host OS and chosen driver backend.

## Recommended simulator stack

Use the official LVGL desktop simulator pattern with SDL2.

Target window:

```text
320 x 240
```

Compile with:

```text
APP_TARGET_SIMULATOR=1
```

## Simulator goals

- Validate layouts before flashing hardware.
- Test touch and keyboard navigation.
- Feed fake MeshCore events into the same app state used by firmware.
- Avoid spending time flashing for every UI adjustment.

## Suggested host dependencies

- CMake
- SDL2 development package
- LVGL source as a submodule or dependency
- C++17 compiler

## Suggested flow

```bash
git submodule add https://github.com/lvgl/lvgl external/lvgl
mkdir build
cd build
cmake ..
cmake --build .
./meshcore_tdeck_sim
```

The simulator should import the same `ui/screens` files used by firmware, but use mock drivers and fake MeshService data.

## Current fallback harness

This folder also includes a no-dependency C++ harness for environments that do
not have LVGL, SDL2, or CMake installed yet.

```bash
make run
```

The harness writes `build/screens.html`, with fixed 320 x 240 frames for the
boot, home, inbox, message, compose, nodes, channels, map, settings, and
diagnostics screens, plus the radio and server-control screens.

To serve the generated snapshot from the development host Docker container:

```bash
cd /workspace/tdeck-plus/meshcore_tdeck_plus_lvgl_plan/simulator
python3 -m http.server 8080 -d build
```

Then open `http://dev-host.local:8092/screens.html`.

## Interactive LVGL simulator over noVNC

The interactive target compiles the same shared LVGL UI code used by firmware,
runs it with the LVGL SDL2 backend, and exposes the SDL window through noVNC.
It also runs a simulator service loop that injects background packets through
the same app snapshot path used by firmware service updates.

From the development host host:

```bash
sudo docker exec -d meshcore-tdeck-plus-dev bash -lc \
  '/workspace/tdeck-plus/meshcore_tdeck_plus_lvgl_plan/tools/start_lvgl_vnc.sh'
```

Then open:

```text
http://dev-host.local:8094/vnc.html?autoconnect=true&resize=scale
```

## Touch navigation test

The simulator build also includes a navigation test that validates the shared
touch-action table used by the LVGL UI. It fails if a screen has an empty button
label, targets an unknown screen, cannot be exited by touch, or has no touch path
back to Home.

```bash
cmake --build build-lvgl --target navigation_test
./build-lvgl/navigation_test
```

The app-controller test validates the stateful command layer used by both the
simulator and firmware UI.

```bash
cmake --build build-lvgl --target app_controller_test
./build-lvgl/app_controller_test
```
