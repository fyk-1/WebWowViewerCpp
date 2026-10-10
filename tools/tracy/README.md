# tools/tracy

Tracy 0.10 tooling for profiling the viewer, and `tracy-gpu-csvexport`, an extension of the bundled
Tracy that exports GPU zones to CSV. The viewer's client is the bundled `wowViewerLib/3rdparty/tracy`
at 0.10.0, so `bin/` must hold 0.10 binaries: a profiler of another version cannot connect to it.

## bin/

Gitignored. `Tracy.exe` and `capture.exe` come from `Tracy-0.10.7z` at
https://github.com/wolfpld/tracy/releases/tag/v0.10. `tracy-gpu-csvexport.exe` is built from
`tracy-gpu-csvexport/` and replaces the release's `csvexport.exe`: without `-g` its output is
byte-identical to the stock exporter's, verified on a trace.

## tracy-gpu-csvexport

Tracy 0.10.0 `csvexport` with a `-g` / `--gpu` flag that reports GPU zones.

```
tracy-gpu-csvexport.exe -g trace.tracy              # one row per GPU zone source location
tracy-gpu-csvexport.exe -g -u trace.tracy           # one row per GPU zone event
tracy-gpu-csvexport.exe -g -f "fsr" [-c] trace.tracy
```

- Aggregate columns match the CPU output: `name,src_file,src_line,total_ns,total_perc,counts,mean_ns,min_ns,max_ns,std_ns`.
  `total_perc` is relative to the trace's last time.
- `-u` columns: `name,src_file,src_line,gpu_start_ns,gpu_exec_time_ns,thread`.
- Duration is `GpuEnd - GpuStart`. The worker leaves out zones with an unset GPU end (negative) or a
  non-positive duration, so those never reach a row, as in the profiler's Statistics window in GPU mode.
- `-e` has no GPU form; with `-g` it is ignored, with a note on stderr.

### Layout

The folder holds no copy of Tracy. `CMakeLists.txt` builds the bundled tree's sources in place, by
path from the repo root.

| File | Holds |
| --- | --- |
| `csvexport.cpp` | The bundled `csvexport/src/csvexport.cpp` with the `-g` flag and `export_gpu` added. |
| `tracy-gpu-csvexport.patch` | That difference as a diff, to apply from the bundled tracy root with `patch --binary -p1` (the sources are CRLF) when the bundled Tracy moves to a new version. |
| `stub/` | A stand-in for `capstone.h`, below. |
| `CMakeLists.txt` | The build. |

`TracyWorker.cpp` includes `capstone.h` and calls five functions from `AddSymbolCode`, which a trace
export never reaches. `stub/capstone.h` declares just the types and constants those call sites use,
and `stub/capstone_stub.c` defines `cs_open` (returns an error, so the disassembly path returns
early) and no-op `cs_option`, `cs_disasm`, `cs_free`, `cs_close`. No other dependency is stubbed:
freetype, glfw, nfd and imgui are left out.

### Build

From a shell with MSVC 2022, CMake and Ninja on the path (the viewer's own build environment;
`C:\Wowely-Planet\Modules\Flyover\Viewer\env.bat` sets one up):

```
cmake -S tools/tracy/tracy-gpu-csvexport -B <build dir> -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build <build dir>
```

Release x64, static CRT. Copy `<build dir>/tracy-gpu-csvexport.exe` to `bin/`.
