---
description: Permission to kill the running Godot process blocking the link, then build
---

The user has explicitly granted permission, for THIS invocation only, to terminate
any running Godot process (editor or game) that is holding
`bin/godot.windows.opt.tools.64.exe` and blocking the linker.

This overrides the standing rule "never kill the editor" for this one invocation.
It is not a standing grant: on any later link failure where `/jpp` was not typed,
go back to reporting that the binary is locked and waiting.

Do this:

1. Check for running Godot processes.
2. If any are holding the binary, terminate them.
3. Run the build:
   `scons platform=windows target=release_debug tools=yes bits=64 -j16`
4. Report the build result — exit code and error count — and say plainly which
   process you killed.

If no process is running, skip straight to the build.

Note that unsaved editor work is lost when the editor is killed. Mention that
only if you actually killed an editor (a large-memory process), not a game
instance.

$ARGUMENTS
