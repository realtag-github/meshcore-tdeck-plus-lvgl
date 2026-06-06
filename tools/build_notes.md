# Build notes

## development host Docker container

```bash
cd meshcore_tdeck_plus_lvgl_plan
tools/run_dev-host_docker.sh
ssh -p 2231 <ssh-user>@dev-host.local
```

If the host user needs sudo for Docker:

```bash
DOCKER="sudo docker" tools/run_dev-host_docker.sh
```

## Firmware

```bash
cd firmware
pio run -e tdeck-plus-915
pio run -e tdeck-plus-868
pio run -e tdeck-plus-433
```

## Flashing

```bash
pio run -e tdeck-plus-915 -t upload
```

## Simulator

```bash
cd simulator
mkdir build
cd build
cmake ..
cmake --build .
```

This scaffold does not include upstream MeshCore, LILYGO board drivers, or LVGL SDL glue. Add them as pinned dependencies once you choose the exact upstream versions.

Fallback path for a minimal host check:

```bash
cd simulator
make run
```

This generates `build/screens.html` without LVGL or SDL2 so screen content and
320 x 240 layout decisions can still be reviewed.

To view it through the development host Docker container:

```bash
sudo docker exec -d meshcore-tdeck-plus-dev bash -lc \
  'cd /workspace/tdeck-plus/meshcore_tdeck_plus_lvgl_plan/simulator && python3 -m http.server 8080 -d build'
```

Open:

```text
http://dev-host.local:8092/screens.html
```

## Interactive LVGL simulator

```bash
sudo docker exec -d meshcore-tdeck-plus-dev bash -lc \
  '/workspace/tdeck-plus/meshcore_tdeck_plus_lvgl_plan/tools/start_lvgl_vnc.sh'
```

Open:

```text
http://dev-host.local:8094/vnc.html?autoconnect=true&resize=scale
```
