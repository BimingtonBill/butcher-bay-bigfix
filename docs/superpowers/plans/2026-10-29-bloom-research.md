# x64 Bloom Research Implementation Plan

**Goal:** Identify the x64 bloom defect if evidence permits; otherwise deliver
bounded observation and a repeatable x64 capture run without executing game code.

**Architecture:** Extend the existing GL upload/bind and draw/swap observation
boundaries. Keep bloom snapshots per context, preserve active texture selection,
and retain the tail of sampled frames so scene geometry cannot consume the
post-process budget. Do not add a speculative image correction or binary patch.

**Constraints:** C++17, x64 only, game files read-only during this task, no game
launch, no push, no child agents. Existing worktree is already isolated.

- [x] Compare both dispatchers, numeric conversions, shader inputs, renderer
  allocation/copy paths and scene commands; record RVAs and confidence.
- [x] Add synthetic tests for opt-in configuration, shader CRC association,
  per-context state, sampled frame tail, bounded output, and state restoration.
- [x] Implement `BloomTrace` snapshots through existing draw/swap hooks and
  upload/bind wrappers, without competing detours or changing rendering.
- [x] Add an x64-only scene script with dry-run support, preserved configuration,
  foreground-only F12 captures, and archived log/shader/capture artifacts.
- [x] Write evidence and exact lead runs, build with VS2022 x64, run CTest with
  and without RIDDICK_DIR, inspect diff, and commit with the requested trailer.

Review focus: diagnostics disabled means no extra GL queries; never query inside
glBegin/glEnd; texture-unit selection must be restored; program ID reuse must
replace the CRC; output and shader tracking must remain bounded. Runtime scene
reachability and visual correctness remain the lead's validation responsibility.
