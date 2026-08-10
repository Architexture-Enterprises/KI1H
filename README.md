# KI1H

<img width="855" height="500" alt="Kapture 2026-08-10 at 20 31 25" src="https://github.com/user-attachments/assets/2114ef37-883d-4fb8-b775-c25b119dc5cb" />


Keep it 1Hundo is a system based on the Roland System 100 and the Buchla System 100 concepts.

The intent of this project is to model what will eventually be created as a real physical instrument.

## Modules

Shipped as the `Architexture` VCV Rack 2 plugin (see `plugin.json`):

| Module | Description |
| --- | --- |
| KI1H-VCO | Oscillator with sync, FM, and AM |
| KI1H-LFO | Low frequency oscillator with rate attenuation |
| KI1H-MIX | Mixer |
| KI1H-FILTER | Filter with linkable CV |
| KI1H-ENVELOPE | ADSR-style envelope generator based on the 258 |
| KI1H-KAOS | Noise and pink/red chaos source |
| KI1H-VCA | Final-stage VCA with panning |

## Development

### Prerequisites

- The [VCV Rack 2 SDK](https://vcvrack.com/downloads) for your platform. This repo is developed against an SDK checked out at `../Rack-SDK`.
- A C++11 toolchain (Apple Clang on macOS, GCC on Linux, MinGW-w64 on Windows).
- `jq` — the SDK's `plugin.mk` reads the slug and version out of `plugin.json` with it.
- `zstd` — used by `make dist` to pack the `.vcvplugin` archive.
- VCV Rack 2 itself, for actually loading and playing the modules.

### Building

`RACK_DIR` must point at the SDK. The Makefile defaults it to `../..` (the layout used when a plugin lives inside a Rack source tree), so pass it explicitly:

```sh
make RACK_DIR=../Rack-SDK
```

Or export it once per shell:

```sh
export RACK_DIR=../Rack-SDK
make
```

Targets provided by the SDK's `plugin.mk`:

| Target | Effect |
| --- | --- |
| `make` / `make all` | Compiles `src/*.cpp` into `plugin.dylib` (`.so` / `.dll` elsewhere) |
| `make dist` | Builds, strips, signs, and packs `dist/Architexture-<version>-<arch>.vcvplugin` |
| `make install` | Runs `dist`, then copies the archive into Rack's user plugin dir |
| `make clean` | Removes `build/`, the plugin binary, and `dist/` |
| `make cleandist` | Removes `dist/` only |

`make install` targets `~/Library/Application Support/Rack2/plugins-mac-arm64` on Apple Silicon; on Linux it is `~/.local/share/Rack2/plugins-<os>-<cpu>` and on Windows `%LOCALAPPDATA%/Rack2/plugins-<os>-<cpu>`. Restart Rack after installing to pick up the new build.

Sources are compiled with `-O3 -Wall -Wextra`. Warnings are not errors. The current build is not warning-free: MIX, VCA, and ENVELOPE include `componentlibrary.hpp` and `helpers.hpp` directly, which trips the SDK's "Plugins must only include rack.hpp" warning. That is tracked as an open issue — don't add new sources that include SDK headers other than `rack.hpp` (via `plugin.hpp`).

On Apple Silicon the link step prints `ignoring file '../Rack-SDK/libRack.dylib': found architecture 'x86_64'`. This is expected with an x86_64 SDK: macOS plugins link with `-undefined dynamic_lookup` and resolve Rack's symbols at load time, so the build still produces a working `plugin.dylib`.

### Layout

```
src/plugin.cpp      Plugin entry point; every module is registered here
src/plugin.hpp      Model externs, shared panel layout constants, custom port/switch widgets
src/KI1H_*.cpp      One file per module: DSP structs, Module, and ModuleWidget
res/*.svg           Panel graphics, one per module, plus the Bananut port art
Makefile            Thin wrapper over the SDK's plugin.mk
plugin.json         Plugin manifest: slug, version, and the module list
```

Panel geometry is not hard-coded per module. `src/plugin.hpp` defines a 5-column × 6-row grid (`COLUMNS[]`, `ROWS[]`, built from `COLUMN_SPACING` / `ROW_SPACING` at compile time) that every widget lays out against, so panels stay dimensionally consistent with each other and with the hardware they model.

### Adding a module

1. Draw the panel in Inkscape following the [VCV panel tutorial](https://vcvrack.com/manual/PanelTutorial), and save it to `res/KI1H-NAME.svg`.
2. Scaffold the source and the manifest entry with the SDK helper:
   ```sh
   python3 ../Rack-SDK/helper.py createmodule KI1H-NAME res/KI1H-NAME.svg src/KI1H_NAME.cpp
   ```
   This appends the module to `plugin.json` and generates a widget skeleton positioned from the panel's component layer.
3. Declare `extern Model *modelKI1H_NAME;` in `src/plugin.hpp` and add `p->addModel(modelKI1H_NAME);` to `init()` in `src/plugin.cpp`. A module that is not registered will not appear in Rack.
4. Replace the generated positions with the shared `COLUMNS[]` / `ROWS[]` constants so the new panel matches the rest of the system.
5. Build, `make install`, and check it in Rack.

### Conventions

- Module DSP lives in plain structs above the `Module` subclass, kept in an anonymous namespace when it is file-local. Keep `process()` allocation-free — it runs on the audio thread.
- Param, input, and output IDs are enums ending in `NUM_PARAMS` / `NUM_INPUTS` / `NUM_OUTPUTS`.
- Section banner comments (`// ===== ... =====`) separate the DSP, module, and widget sections of each file.
- Ports use the `Bananut*` widgets from `plugin.hpp` rather than the stock Rack components.

There is no automated test suite yet; changes are verified by loading the plugin in Rack. Extracting the pure DSP helpers behind a test target is tracked as an open issue.

`compile_commands.json` is committed for clangd, and is stale relative to the current source list — regenerate it locally (e.g. with `bear -- make`) if your editor needs accurate flags.

### Contributing

Work is tracked as GitHub issues on [Architexture-Enterprises/KI1H](https://github.com/Architexture-Enterprises/KI1H), labeled by kind (`bug`, `enhancement`, `performance`, `cleanup`, `redesign`, `documentation`, …).

1. Branch off `main`, named after the issue: `issue-<number>-<short-slug>` (e.g. `issue-37-filter-uninit-state`).
2. Keep the change scoped to one issue. Most branches here touch a single module.
3. Build and load the plugin in Rack before opening the PR — audio-thread regressions do not show up any other way.
4. Open a PR against `main`; merges land through PRs rather than direct pushes.

### Releasing

The version in `plugin.json` is the single source of truth — `make dist` reads it to name the archive, and Rack uses it to decide whether an installed plugin is current. Bump it there before cutting a release, then:

```sh
make RACK_DIR=../Rack-SDK dist
```

That produces `dist/Architexture-<version>-<arch>.vcvplugin`, which is the artifact users install. Note that a release build is per-architecture: publishing for macOS, Windows, and Linux means running `make dist` on each, or via the VCV Rack plugin toolchain.

`dist/`, `build/`, `dep/`, and the compiled plugin binary are gitignored and should never be committed.
