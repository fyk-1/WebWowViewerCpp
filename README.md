# Wowely-Planet-Farseer

A flyover-only build of [Deamon87/WebWowViewerCpp](https://github.com/Deamon87/WebWowViewerCpp), made for the Wowely Planet host. The branch `Farseer` keeps the map renderer, the CASC and database readers, and the stdout protocol the host speaks. The model viewer, the map editor, the exporters, the OpenGL backends and the web build are gone.

Upstream ships no license file, and this fork adds none. The third-party code it builds against keeps its own licenses.

## Running

The executable is `Wowely-Planet-Farseer.exe`. It needs `export.db3` (the client database) and `cascStorages.json` (the list of WoW installs, first entry wins) in its working directory, and prints its usage and exits without `--flyover`.

| Argument | Effect |
| --- | --- |
| `--flyover <route.json>` | Opens the route's map on the first storage and flies the route. Reports on stdout and takes commands on stdin; the protocol is in `src/flyover/FlyoverMode.h`. |
| `--list-gpus` | Prints the Vulkan devices and exits. |
| `--config-defaults` | Prints every settable renderer field with its default, as one JSON object on one line, and exits. It creates no window and opens no game data. |

The host also passes `--wow`, `--product` and `--db`; `src/flyover/FlyoverMode.h` has their current state. The environment variable `WWV_GPU` picks the Vulkan device, as `vvvv:dddd` hex vendor and device ids or as part of the device name.

## Building

Prerequisites:

- Visual Studio 2022 Build Tools (MSVC, CMake and Ninja come with it)
- The Vulkan SDK, whose `slangc` compiles the shaders
- Git, with the submodules fetched by `git submodule update --init --recursive`

The host repository keeps three scripts for this fork in `Modules\Flyover\Viewer\`: `configure.bat`, `build.bat` and `run.ps1`. They configure into `build\`, build the `Wowely-Planet-Farseer` target, install into `install\bin\`, and run a one-point route that passes when the viewer reports its first map area.

Tracy, the profiler, stays in the tree and is off by default. Configure with `-DFLYOVER_TRACY=ON` for a profiling build.
