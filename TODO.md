# MorphoView TODO

Near-term and later work. Command-language details also live in [`docs/commandapi.md`](docs/commandapi.md); this file is the checklist.

## View session API (Morpho)

Refactor [`share/modules/morphoview.morpho`](share/modules/morphoview.morpho) away from a blocking constructor:

- [ ] `init` — defaults only (no spawn)
- [ ] `open(commands)` — bind, spawn, send, wait for `ok`
- [ ] `update(commands)` — send another chunk, wait for `ok`
- [ ] `poll(timeoutMs)` — non-blocking / short wait; return event or `nil`
- [ ] `wait(sessionTimeOut=0)` — convenience loop until closed / timeout
- [ ] `close()` — idempotent cleanup
- [ ] Private helpers prefixed with `_`

Morpho has no async runtime: scripts drive the loop with `poll` / `System.sleep`.

## Command language extensions

### Replace / update

| Command | Status | Intent |
|---------|--------|--------|
| `U S <id>` | Done | Clear scene `id` in place (keep window); select as current; following `o`/`v`/`l`/`d` refill |
| `U O <id>` | Later | Clear/redefine one object in the current scene |
| `U V <objid>` | Later | Replace vertex blob only (morph targets / pointwise edits) |

`S` stays find-or-create/select. Replace is always explicit via `U`, so re-selecting a scene cannot wipe it by accident.

### Delete / quit

| Command | Status | Intent |
|---------|--------|--------|
| `X O <id>` | Later | Delete object from current scene |
| `X S <id>` | Later | Close scene / window |
| `Q` | Later | Quit viewer cleanly (prefer over Morpho-side `pkill`) |

### Events (viewer → Morpho)

Returned on the ZMQ PAIR and consumed by `View.poll`:

| Message | Status | Intent |
|---------|--------|--------|
| `ok` / `err …` | Done | Parse result for a command chunk |
| `window.closed` | Done | Last display closed |
| `pick …` | Later | Selection / hit info |
| `view <16 floats>` | Later | Camera / view matrix |
| `click …` | Later | Mouse click (gated / on request to avoid flood) |

### Performance / bulk data

- [ ] Binary / byte-buffer transport for large vertex payloads (ASCII remains fine for control)
- [ ] Persistent apply context across `command_process` batches (needed for chunks that only do `U O` / `v` without a leading `S` / `U S`)

## Demos / tests

- [ ] High-level `View` smoke: `open` + `wait(sessionTimeOut=…)`
- [ ] Animation demo: rotating triangle via `U S` + `update` / `poll` loop
- [ ] Keep low-level [`test/testzmq.morpho`](test/testzmq.morpho) as transport smoke

## Notes

- Each `command_process` batch currently starts with an empty apply context, so every chunk that draws must establish a scene (`S` or `U S`) before `o`/`v`/….
- Graphics `Show` stays fire-and-forget (temp file + `-t`). Package `View` is the live duplex path.
