# xplapi

An X-Plane 12 plugin that exposes a **REST HTTP API** for reading (and eventually writing) datarefs and executing commands.  
Useful for driving external displays, tablet panels, scripts, and home-cockpit tools without wrestling with UDP broadcast or SimConnect.

---

## Features

- **Zero configuration required** — starts on port `8012` out of the box
- **Lazy dataref resolution** — just ask for a name; the plugin finds the handle automatically
- **On-demand reads** — values are always fresh (updated every 100 ms by the flight loop)
- **Status page** at `/` — live view of every tracked dataref in the browser
- **CORS enabled** — call the API directly from any browser tab or web app
- **Single config file** — optional `config.yaml` next to the `.xpl` file

---

## Installation

1. Build the plugin (see [Building](#building)) or grab a release binary.
2. Copy the output folder into X-Plane's plugin directory:

```
X-Plane 12/
  Resources/
    plugins/
      xplapi/
        64/
          win.xpl        ← the plugin
          config.yaml    ← optional, see Configuration
```

3. (Re)start X-Plane. Check the developer console for:
   ```
   xplapi: web server listening on port 8012
   ```

---

## Configuration

Place a `config.yaml` file in the same directory as `win.xpl`:

```yaml
# xplapi configuration
port: 8012    # HTTP port to listen on (default: 8012)
```

If the file is absent, all defaults apply. The plugin logs which port it starts on to the X-Plane developer console.

---

## Building

Prerequisites: **Visual Studio 2022** (MSVC v143, Windows 10 SDK).  
No CMake, no IDE needed — pure command-line MSBuild.

```bat
# Debug build
build

# Release build
build release

# Debug build + copy to X-Plane
build deploy

# Release build + copy to X-Plane
build release deploy

# Clean
build clean
```

The `deploy` step copies `win.xpl` to:
```
E:\XPL12\X-Plane 12\Resources\plugins\xplapi\64\
```
Adjust `DEPLOY_DIR` in `build.bat` if your X-Plane lives elsewhere.

---

## API Reference

Base URL: `http://localhost:8012`  
All API responses are `application/json`.

---

### `GET /`

Status page (HTML). Shows all currently tracked datarefs, their resolved types, and whether they were found in X-Plane. Auto-refreshes every 5 seconds.

---

### `GET /api/dataref?name=<dataref-name>`

Read a single dataref by name.

**Query parameters**

| Parameter | Required | Description |
|-----------|----------|-------------|
| `name`    | yes      | Full X-Plane dataref path, URL-encoded |

**Response — resolved value**
```json
{
  "name": "sim/time/total_running_time_sec",
  "type": "float",
  "value": 1523.47
}
```

**Response — first request (not yet resolved)**
```json
{
  "name": "sim/time/total_running_time_sec",
  "status": "pending"
}
```
Retry after ~100 ms. The flight loop resolves the handle on the next tick.

**Response — dataref not found in X-Plane**
```json
{
  "name": "sim/does/not/exist",
  "error": "dataref not found"
}
```

**Value types**

| `type`     | `value` shape            | Notes |
|------------|--------------------------|-------|
| `int`      | `42`                     | |
| `float`    | `3.14`                   | |
| `double`   | `3.141592653589793`      | |
| `int[]`    | `[0, 1, 2, ...]`         | Full array |
| `float[]`  | `[0.1, 0.2, ...]`        | Full array |
| `bytes`    | `"KSFO"`                 | Byte array returned as string |

**curl example**
```bash
curl "http://localhost:8012/api/dataref?name=sim/time/total_running_time_sec"
```

```bash
# URL-encode slashes are not required by most tools, but the name must be exact
curl "http://localhost:8012/api/dataref?name=sim/cockpit2/gauges/indicators/airspeed_kts_pilot"
```

---

### `POST /api/dataref/get`

Read a single dataref. Equivalent to the GET variant but the name is passed in a JSON body — handy when calling from code.

**Request body**
```json
{
  "name": "sim/time/total_running_time_sec"
}
```

**curl example**
```bash
curl -X POST http://localhost:8012/api/dataref/get \
     -H "Content-Type: application/json" \
     -d '{"name": "sim/time/total_running_time_sec"}'
```

---

### `POST /api/dataref/getMultiple`

Read several datarefs in a single round-trip. Accepts either a bare JSON array or an object with a `names` key.

**Request body — bare array (preferred)**
```json
[
  "sim/time/total_running_time_sec",
  "sim/cockpit2/gauges/indicators/airspeed_kts_pilot",
  "sim/flightmodel/position/indicated_airspeed"
]
```

**Request body — object form**
```json
{
  "names": [
    "sim/time/total_running_time_sec",
    "sim/cockpit2/gauges/indicators/airspeed_kts_pilot"
  ]
}
```

**Response** — array, one entry per requested name (same shape as single-get):
```json
[
  {
    "name": "sim/time/total_running_time_sec",
    "type": "float",
    "value": 1523.47
  },
  {
    "name": "sim/cockpit2/gauges/indicators/airspeed_kts_pilot",
    "type": "float",
    "value": 142.3
  },
  {
    "name": "sim/flightmodel/position/indicated_airspeed",
    "status": "pending"
  }
]
```

**curl example**
```bash
curl -X POST http://localhost:8012/api/dataref/getMultiple \
     -H "Content-Type: application/json" \
     -d '["sim/time/total_running_time_sec", "sim/cockpit2/gauges/indicators/airspeed_kts_pilot"]'
```

---

### `POST /api/dataref/set`

Write a value to a dataref. Returns the state of the dataref after the write.

**Request body**
```json
{
  "name": "sim/graphics/view/pilots_head_psi",
  "value": 45.0
}
```

**PowerShell example**
```powershell
Invoke-RestMethod -Method Post -Uri "http://localhost:8012/api/dataref/set" -ContentType "application/json" -Body '{"name":"sim/graphics/view/pilots_head_psi", "value":45.0}'
```

---

### `POST /api/dataref/setMultiple`

Write multiple values in one request.

**Request body**
```json
[
  { "name": "sim/graphics/view/pilots_head_psi", "value": -45.0 },
  { "name": "sim/graphics/view/pilots_head_the", "value": -15.0 }
]
```

**PowerShell example**
```powershell
Invoke-RestMethod -Method Post -Uri "http://localhost:8012/api/dataref/setMultiple" -ContentType "application/json" -Body '[{"name":"sim/graphics/view/pilots_head_psi", "value":-45.0}, {"name":"sim/graphics/view/pilots_head_the", "value":-15.0}]'
```

---

## Typical usage pattern

On first contact the plugin needs one flight-loop tick (~100 ms) to resolve a new dataref name.  
A robust client should handle the `"pending"` response with a simple retry:

```python
import requests, time

BASE = "http://localhost:8012"

def read_dataref(name: str, retries: int = 5) -> dict:
    for _ in range(retries):
        r = requests.get(f"{BASE}/api/dataref", params={"name": name})
        data = r.json()
        if data.get("status") != "pending":
            return data
        time.sleep(0.15)
    return data

print(read_dataref("sim/time/total_running_time_sec"))
# {'name': 'sim/time/total_running_time_sec', 'type': 'float', 'value': 1523.47}
```

---

## Planned endpoints

| Method | Path | Description |
|--------|------|-------------|
| `POST` | `/api/command/once` | Trigger an X-Plane command once |
| `POST` | `/api/command/begin` | Begin a held command |
| `POST` | `/api/command/end` | End a held command |
| `WS`   | `/api/dataref/watch` | WebSocket subscription — push updates at a configurable interval |

---

## Project structure

```
xplapi/
├── SDK/                    X-Plane SDK (CHeaders + Libraries)
├── src/
│   ├── main.cpp            Plugin entry points + flight loop
│   ├── Config.cpp          config.yaml reader
│   ├── DataRefRegistry.cpp Lazy dataref lookup + value cache
│   ├── TcpListener.cpp     Winsock select()-based TCP server
│   └── WebServer.cpp       HTTP routing + JSON responses
├── include/
│   ├── plugin.h            Plugin identity + platform macros
│   ├── Config.h
│   ├── DataRefRegistry.h
│   ├── TcpListener.h
│   ├── WebServer.h
│   └── json.hpp            nlohmann/json (header-only)
├── build/                  Build output (gitignored)
├── xplapi.vcxproj          MSBuild project (x64 only)
├── build.bat               Command-line build script
└── README.md
```

---

## Thread-safety notes

X-Plane's dataref API (`XPLMFindDataRef`, `XPLMGetData*`) must be called from the **simulator's main thread**.  
The HTTP server runs on a separate thread.  
`DataRefRegistry` bridges the two with a mutex:

- **HTTP thread** calls `ensureTracked()` (inserts name) and `readValue()` (reads cached copy).  
- **Flight loop thread** calls `update()` every 100 ms — resolves any unresolved handles and refreshes all cached values — then releases the lock.

This means HTTP responses always return the value from the last flight-loop tick, with at most ~100 ms of staleness.

---

## License

MIT
