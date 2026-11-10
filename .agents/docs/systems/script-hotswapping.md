# Script hotswapping

A `dynamic` build loads its script libraries as shared objects and the worldserver adds, replaces and
removes them at runtime (ported from TrinityCore 3.3.5). Engine: `src/server/game/Scripting/` —
`ScriptMgr`'s script-context API, the per-type swap policy, and `ScriptReloadMgr` (library watcher,
load/unload/reload protocol, auto-recompiler) over the vendored `deps/efsw`. It logs to
`scripts.hotswap` (level 4 by default).

## Build modes

Only a `dynamic` script project yields a library that can be swapped; a `static` one is compiled into
`worldserver` and can never be loaded at runtime.

- `-DWITH_DYNAMIC_LINKING=ON` builds `common`, `shared`, `database` and `game` as shared objects and
  compiles the engine in (`ACORE_API_USE_DYNAMIC_LINKING`). Any `dynamic` script or module project
  forces it on (`IsDynamicLinkingRequired`, `IsDynamicLinkingModulesRequired`).
- `-DSCRIPTS=<none|static|dynamic|minimal-static|minimal-dynamic>` and
  `-DMODULES=<none|static|dynamic>` — the tree-wide value.
- `-DSCRIPTS_<Module>=<default|disabled|static|dynamic>`, `-DMODULE_<Name>=…` — per project; `default`
  follows the tree-wide value, `minimal-*` enables only `Commands` and `Spells`.

```bash
cmake -S . -B build -DWITH_DYNAMIC_LINKING=ON -DSCRIPTS=dynamic -DMODULES=dynamic \
      -DCMAKE_INSTALL_PREFIX=$HOME/azeroth-server
cmake --build build -j"$(nproc)" && cmake --install build
```

The four core libraries and the script projects become shared; `scripts` (the static remainder) and
`worldserver` stay static in both modes. Without dynamic linking the manager is a stub: no library is
loaded or watched, and the `HotSwap.*` keys are ignored.

### Install layout

| what | UNIX | Windows |
|---|---|---|
| core libraries | `${prefix}/lib/lib{common,shared,database,game}.so` | `${prefix}/{common,shared,database,game}.dll` |
| swap libraries | `${prefix}/bin/scripts/libscripts_<Module>.so`, `libmod_<dir>.so` | `${prefix}/scripts/scripts_<Module>.dll`, `mod_<dir>.dll` |
| server | `${prefix}/bin/worldserver` | `${prefix}/worldserver.exe` |

`HotSwap.ScriptDir` (`"scripts"`) resolves against the executable's directory unless it is absolute.

## Settings

All eight keys live in `worldserver.conf.dist` under `# HOT SWAP`. The six booleans are cached in
`WorldConfig`; the two strings are read once in `ScriptReloadMgr::LoadSettings()`. All are read at
start-up, so a change needs a restart.

| key | default | effect |
|---|---|---|
| `HotSwap.Enabled` | `1` | the whole subsystem; off loads and watches nothing |
| `HotSwap.ScriptDir` | `"scripts"` | library directory |
| `HotSwap.EnableReCompiler` | `1` | rebuild a module whose sources changed |
| `HotSwap.EnableEarlyTermination` | `1` | discard a build whose sources changed during it |
| `HotSwap.EnableBuildFileRecreation` | `1` | run `cmake <builddir>` first when sources were added or removed |
| `HotSwap.EnableInstall` | `1` | install after a build, so the new library replaces the loaded one |
| `HotSwap.EnablePrefixCorrection` | `1` | rewrite a stale `CMAKE_INSTALL_PREFIX` at start-up |
| `HotSwap.ReCompilerBuildType` | `""` | build type of recompiler jobs; empty = the module's own, then the core's |

## What a swap does, per script type

Each type's policy is one field of its `ScriptTypeInfo` line in `ScriptMgr.cpp`; the policy enum and
the no-op primary `ScriptSwapHooks<T>` template live in `ScriptRegistrySwapHooks.h`, and the
specializations sit beside the type list.

| type | policy | on release / commit |
|---|---|---|
| every other type | `Immediate` | hook tables rebuilt at the commit; no live object touched |
| `CreatureScript` | `CreatureAI` | per map: kill its events, un-charm, evade — then destroy the old AI, create the new one, `InitializeAI()`, reset the vehicle kit, evade, cast spell `40162` |
| `GameObjectScript` | `GameObjectAI` | per map: kill its events, `Reset()` — then destroy the old AI, create the new one, `Reset()` |
| `CommandScript` | `CommandCache` | chat-command map rebuilt (invalidated on release and commit) |
| `OutdoorPvPScript` | `OutdoorPvP` | `sOutdoorPvPMgr->Die()` before, `InitOutdoorPvP()` after — progress is lost |
| `InstanceMapScript`, `SpellScriptLoader` | `Lazy` | live instances and auras keep the old library bound; new ones use the newest. Re-casting the spell or `.instance unbind all` + re-entering picks it up; spell scripts are re-validated |
| `BattlefieldScript`, `BattlegroundScript` | `Unsupported` | releasing the owning context asserts |

A change set is applied on the world tick (`WUPDATE_CHECK_HOTSWAP`, 500 ms) after a 500 ms settle
delay, and never while a script is scheduled (`ScriptMgr::IsScriptScheduled()`) — it retries on the
next tick instead.

## AzerothCore specifics

- Scripts register before the database is opened, so a DB-bound script (`CreatureScript`,
  `GameObjectScript`, …) waits in `ALScripts` until its name appears in the `scripted_*.name` table. A
  module loaded later is promoted at the next commit.
- Start-up is split because the configuration is not read yet when scripts load:
  `ScriptReloadMgr::Initialize()` (from `ScriptMgr::Initialize()`) only arms the manager;
  `ScriptReloadMgr::LoadSettings()` (from `World::SetInitialWorldSettings()`, right after
  `LoadConfigSettings()`) reads the keys, resolves the directories, loads the boot libraries, commits
  them and starts the watchers.
- `ModuleScript` is in the registry type list like every other type, so unloading a library destroys
  the `ModuleScript` objects it registered.

## Recompiler requirements

- Run the server outside its build tree, from the `CMAKE_INSTALL_PREFIX` tree; a stale prefix is
  corrected at start-up (`HotSwap.EnablePrefixCorrection`) by re-running `cmake` on the build tree.
- The build directory path must not contain a space — refused at start-up with the path printed.
- An attached debugger blocks every build (logged at most every 7.5 s).
- macOS: the recompiler is compiled out — source changes are ignored, prebuilt libraries still load.
- One job at a time: optional `cmake <builddir>` → `cmake --build … --target <project>` → optional
  install of that component. The project is `scripts_<module>` for a script module of the tree and
  `mod_<dir>` for an out-of-tree module, both lowercased; the kind comes from the library file name.

## Code map

| file | holds |
|---|---|
| `ScriptMgr.h/.cpp` | context API, `ScriptRegistryCompositum`, delayed delete, registry type list with the swap policies and the hook specializations |
| `ScriptRegistrySwapHooks.h` | `ScriptSwapPolicy` enum, no-op `ScriptSwapHooks<T>` |
| `ScriptReloadMgr.h/.cpp` | `ModuleReference`, watchers, load/unload/reload, recompiler, log surface |
| `World.cpp` | `WUPDATE_CHECK_HOTSWAP` timer calling `sScriptReloadMgr->Update()` |
| `src/test/server/game/Scripting/` | context/release and unload-contract unit tests (no live server) |

## Troubleshooting

- `Script module "X" is still used by N spell, aura or instance script(s)` — the old library stays
  cached until those objects die; recast the spell or unbind and leave the instance.
- `has a different revision hash` — warning only; the core/module build revisions differ. An empty
  hash warns too (and a core without a revision hash logs a DEBUG note instead of comparing).
- `Attempt to load a module twice` — two files report the same module name; remove one.
- `committed on the next world tick, because database scripts are still running` — the commit was
  refused while a script was scheduled; it retries by itself.
- `Library directory "…" doesn't exist`, or a library that cannot be copied or opened — check
  `HotSwap.ScriptDir`, its permissions and the cache directory; an uncopyable or unopenable library is
  fatal by design.
- `HotSwap.Enabled is set, but this server was built without dynamic linking` — a static build has no
  library to load.
