# Tuna Editor — embed spike

A WinForms (.NET Framework 4.8) shell that hosts the **engine** viewport inside its own layout.
This is the proof-of-concept step: it establishes that the engine can live inside a WinForms
window before any editor dock gets written.

We host the engine, not Godot's editor. The engine is launched as a normal runtime (no `-e`),
and every piece of editor UI is ours to build in WinForms.

## Status: the embed works

Verified programmatically (window parentage, not screenshots):

```
host  window : 'Tuna Editor -- embed spike'          pid 22276
  parent panel: WindowsForms10.Window...  854x813    pid 22276
    engine hwnd: class='Engine'           854x813    pid 3724   <- different process
      title    : 'solander (DEBUG)'
      WS_CHILD : True
      WS_EX_APPWINDOW cleared : True
      fills host panel        : True
      cross-process reparent  : True
```

## Architecture: out-of-process + SetParent

The engine keeps the window **it** created. We only restyle that HWND to `WS_CHILD` and
`SetParent` it into a WinForms panel — legal across process boundaries.

Consequences, all of them good:

- The engine keeps its own `WndProc`, message pump, input handling and GL context. Input to
  the viewport reaches Godot through the path it already uses. **Zero engine changes.**
- An engine crash or hang does not take the editor down with it.
- Long engine work (cell load, navmesh bake) never blocks the WinForms UI thread.

### Why not GODOT_WINDOWID

`platform/windows/os_windows.cpp:1503` has an external-HWND adoption path: set `GODOT_WINDOWID`
and the engine adopts that window instead of creating one. It is **not usable here**, because it
works by subclassing the host window:

```c
SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)(WNDPROC)::WndProc);
```

`SetWindowLongPtr(GWLP_WNDPROC)` fails with `ERROR_ACCESS_DENIED` against a window owned by
another process — a WndProc pointer is meaningless in a foreign address space. `GODOT_WINDOWID`
is an in-process mechanism only. Option A works precisely because it avoids it.

If we ever move in-process (engine as a DLL under the .NET process), the trap to avoid is
adopting the panel's HWND: that window's messages pump on the **WinForms UI thread**, so engine
frames would run there and any long operation would freeze the whole editor. The fix is the
corollary of the note already in `OS_Windows::window_create`:

> Must run on the thread that pumps messages: PeekMessage only retrieves messages for windows
> owned by the calling thread.

— i.e. have the engine create its *own* child HWND parented to the panel, from the engine's
thread.

## Two gotchas already handled

**The engine re-sizes its own window during boot.** It applies the project's window settings
seconds after the reparent, overriding our initial layout. Since the window is in another
process we cannot subclass it to intercept `WM_SIZE`, so `EngineHost.ReconcileSize` runs a
250 ms watchdog that issues `SetWindowPos` only when the rects actually disagree.

**`EnumWindows` cannot see the embedded window.** It enumerates top-level windows only, and a
reparented engine window is no longer top-level. Discovery (pre-reparent) uses `EnumWindows`;
any verification afterwards must walk down from the host with `EnumChildWindows`.

## Running it

```
tools\TunaEditor\bin\x64\Debug\TunaEditor.exe
```

Defaults to `C:\Users\Antoine Dussault\Documents\solander` and starts the engine on show.

| Flag | Meaning |
| --- | --- |
| `--project <dir>` | Folder containing `project.godot`. Empty string opens the project manager. |
| `--scene <res://path.tscn>` | Run this scene instead of the project's main scene. |
| `--engine <path.exe>` | Engine binary. Defaults to walking up to the repo's `bin/`. |
| `--no-autostart` | Do not launch the engine on show; use the toolbar button. |

Build:

```
"C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe" TunaEditor.csproj /p:Configuration=Debug /p:Platform=x64
```

## Heads-up before you run it

With no `--scene`, the engine runs the project's **main scene** — that is the game, so it will
capture the mouse if Solander does. For editor work the embedded engine should boot a dedicated
editor-mode scene instead; `--scene` is the hook for that.

## What is not built yet

The docks are empty placeholders. The next real piece is the **data bridge** between the
WinForms UI and the engine:

- `core/io/marshalls.h:183` already provides `encode_variant` / `decode_variant`, which
  marshals any `Variant` to a byte buffer and back. Combined with `get_property_list()`, a
  WinForms inspector can be driven generically rather than hand-written per type.
- Transport: a named pipe carrying those blobs, since the engine is a separate process.
- Gizmos stay native — they are drawn in the engine viewport either way, so the existing
  `terrain_path_gizmo` and friends keep working unchanged.
