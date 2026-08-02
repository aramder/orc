# KiCad MCP logbook

Running log of setting up and using `kicad-mcp-pro` as an MCP server against this repo's `hardware/` KiCad project. Append entries; don't rewrite history.

Related: [docs/hardware-workflow.md](../docs/hardware-workflow.md) (the gate discipline this tooling is meant to help enforce, not replace).

## 2026-08-01 — initial setup

**Why**: looking for a schematic-hygiene-aware tool to complement manual KiCad gates, after finding PCB-Agent-Teams was Windows-blocked on `pcbnew`. Researched MCP options (Konnect, kicad-mcp-pro, obhox/kicad-mcp) — see conversation for the comparison. Picked `kicad-mcp-pro`: pure Python, MIT, `sch_visual_qa`/`pcb_visual_qa` tools give explicit readability findings, and its `compatibility.yaml` confirms SWIG `pcbnew` bindings are forbidden (`test_swig_guard.py`), so it doesn't hit the same Windows wall.

**Environment**:
- `uvx`/`uv` already present (Python 3.13.12 install).
- KiCad 10.0.5 installed at `C:\Program Files\KiCad\10.0\bin\`, but `kicad-cli.exe` was **not** on `PATH`.
- `uvx kicad-mcp-pro --help` confirmed a clean install (94 packages, ~90s first pull).

**Config — [.mcp.json](../.mcp.json)** (project-scoped, so any Claude Code session opened here picks it up automatically):
```json
{
  "mcpServers": {
    "kicad": {
      "command": "uvx",
      "args": ["kicad-mcp-pro"],
      "env": {
        "KICAD_MCP_PROJECT_DIR": "hardware",
        "KICAD_MCP_PROFILE": "default",
        "KICAD_MCP_OPERATING_MODE": "readonly",
        "KICAD_CLI_PATH": "C:\\Program Files\\KiCad\\10.0\\bin\\kicad-cli.exe"
      }
    }
  }
}
```
- `KICAD_CLI_PATH` used instead of touching system `PATH`.
- `readonly` operating mode deliberately chosen for first trial — no `sch_*`/`pcb_*` write tools available until we decide to trust it with edits. 48/48 PCB-write and 48/48 schematic-write tools correctly report `blocked` under this mode.

## 2026-08-01 — first connection, `hardware/` state check

After a Claude Code reload, `kicad` MCP server connected. Ran a read-only pass:

**Server state** (`kicad_get_server_info`):
- `kicad-cli` found, version `10.0.5`, matches `KICAD_CLI_PATH`.
- **KiCad IPC unavailable** — `Connection refused` (KiCad wasn't running). File-backed DRC/ERC/exports work regardless; live PCB/schematic read-write need KiCad open with the API enabled.
- `sch_render_png` requires `write` mode even though it's a render, not a mutation — note for later if a visual is wanted without granting write access.

**`project_get_next_action`**: `BLOCKED` at the `Placement` gate — "No PCB footprints were found to evaluate." `hardware/orc.kicad_pcb` currently has 0 footprints even though the schematic has 111 symbols. **Schematic and PCB are out of sync** — footprints haven't been transferred/placed yet.

**`sch_get_symbols`**: 111 symbols present. This is the two-ground-domain design from [circuit-draft.md](../docs/circuit-draft.md) — ESP32-S3, LM2596S-ADJ ×2 (Domain A + coil supply), ADuM1250 isolator, PCA9555 GPIO expander, ×10 relay driver channels (QN/QP/RB/RP pattern), CAN transceiver, USB-C, harness connector J2. Matches the BOM in circuit-draft.md at a glance — not yet cross-checked line by line.

**`run_erc`**: **FAIL, 46 violations.** Overwhelmingly `Pin not connected` on:
- J1 (USB-C) — SHIELD, CC1, CC2, SBU1, SBU2 unconnected (expected if only D+/D-/VBUS/GND are wired so far)
- U1 (ESP32-S3) — ~25 unused GPIOs unconnected (likely fine, but not yet marked with explicit no-connect flags, which is what ERC is actually complaining about)
- U5 (PCA9555) — `~INT` and IO1_2..IO1_7 unconnected (only 10 of 16 I/O lines used per design-inputs.md's "10 used" note — again likely intentional, needs no-connect markers)
- U7 (SN65HVD230 CAN transceiver) — Vref unconnected
- U8 (USBLC6-2SC6 ESD protection) — I/O1, I/O2 unconnected
- **1 real error, not a no-connect issue**: PF3 and PF7 (both `PWR_FLAG`) are wired to the same net — two power-output pins tied together, which is what triggered "Pins of type Power output and Power output are connected."
- **1 warning worth a look**: U2 (ADuM1250) pin GND2 not tied to a ground net.
- **1 warning, likely a labeling mistake**: `COIL_9V` and `GND_B` labels both attached to the same schematic items — ERC picked `COIL_9V` as the net name, meaning a ground point may be mislabeled as the coil supply net.

**`sch_visual_qa`**: **WARN.** This is the readability-specific tool we set this server up for. Findings:
- Multiple `VIN_BUCK_A` and `GND_A` label instances reported as overlapping at ~0.00mm apart — reads as duplicate/stacked labels at the same coordinates, not just tight spacing.
- Symbol body overlaps: Q5/C19 (~16.5mm²), C20/C14 (~17.5mm²) — components physically overlapping on the sheet.
- Text/field overlaps: C20's value text over the COIL_9V label, C14's reference over GND_B, and repeated VIN_BUCK_A-over-VIN_BUCK_A / GND_A-over-GND_A text pile-ups matching the label-overlap findings above.
- Advisory: no title block on the schematic.

**`pcb_visual_qa`**: PASS — but trivially, since there are 0 footprints to check yet.

**`run_drc`**: FAIL — 1 error: board outline is malformed, no edges on `Edge.Cuts`. Expected given the PCB has no footprints or outline drawn yet.

### Read on all this

None of this is a crisis — the schematic is mid-construction, not claimed-complete. What it actually validates: the tool works, gives real per-object findings (ref designators + positions, not vague summaries), and the readability pass (`sch_visual_qa`) surfaced concrete, actionable overlap issues distinct from what ERC alone would catch — which was the whole point of trying this server. Per hardware-workflow.md Gate 3, this schematic is **not clean** yet: 46 ERC findings (mix of real errors and missing no-connect markers) plus visual-QA overlaps need clearing before this is capture-complete, and the PCB side hasn't started (footprints not placed).

### Open questions / next steps

- [x] ~~Who/what generated the current 111-symbol schematic and the `.mcp-backups/` snapshots in `hardware/`?~~ **Resolved**: `.claude/settings.local.json` (predating this session) has an `additionalDirectories` entry pointing at `C:\Users\aramder\Documents\GitHub\KiCAD-MCP-Server` — a local clone of `mixelpixx/KiCAD-MCP-Server`, the original Python/TypeScript project (predecessor to the Rust rewrite "Konnect" surveyed earlier). That project's pcbnew/kicad-skip backend and its snapshot-on-write behavior line up with the `.mcp-backups/` pattern. So: the current schematic was very likely built through that older server, not through kicad-mcp-pro or by hand. Not investigated further — treating the current schematic as the real working baseline regardless, per the checkpoint commit.
- [ ] Clear the real ERC error (PF3/PF7 power-output short) and the ADuM1250 GND2 warning — both look like genuine mistakes, not missing no-connects.
- [ ] Resolve the COIL_9V/GND_B dual-label warning — check the schematic at that node for a mislabeled net.
- [ ] Decide the no-connect strategy for unused ESP32/PCA9555 pins (mark explicitly vs. leave and suppress the ERC category) before the 46-violation number is treated as a real gate result rather than noise.
- [ ] Fix the sch_visual_qa overlaps (duplicate-position labels, Q5/C19 and C20/C14 symbol overlaps) — these are exactly the "graphical schematic hygiene" issues this tool was chosen to catch.
- [x] ~~Decide when to flip `KICAD_MCP_OPERATING_MODE` from `readonly` to `write`~~ **Done** — see 2026-08-01 (write mode + worktree redirect) entry below.
- [ ] PCB side hasn't started — footprints need transferring from schematic before `pcb_visual_qa`/`run_drc` are meaningful.

## 2026-08-01 — write mode + worktree redirect, verified

**Write mode**: flipped `.mcp.json`'s `KICAD_MCP_OPERATING_MODE` to `write`, user reloaded Claude Code, confirmed via `kicad_get_server_info`: `operatingMode.active` is now `"write"`, all 48 previously-blocked `sch_*`/`pcb_*` write tools report `available: true`. Committed (`2f6dc68`).

**SPICE / ngspice — accepted gap for tonight.** `adapterRouting.categories.simulation` reports `availableCount: 0`, `ngspiceAvailable: false` — ngspice isn't installed on this machine, so `sim_run_*` tools exist in the write-mode tool list but will fail or no-op if called. Decision: **proceed without SPICE tonight.** hardware-workflow.md's Gate 3 SPICE-sanity step is skipped, not satisfied — real electrical verification of the buck feedback dividers etc. still needs to happen before those subcircuits are trusted, just not as part of this pass. Schematic-hygiene, ERC, and DRC gates don't depend on ngspice and proceed normally.

**Worktree redirect — dry-run verified before committing to an unattended overnight run:**
1. `git worktree add .claude/worktrees/overnight-schematic -b overnight-schematic-dryrun` — clean, branched from the baseline checkpoint commit `e16939c`.
2. Copied `.claude/settings.local.json` and this logbook into the worktree's own `.claude/`.
3. `mcp__kicad__kicad_set_project(project_dir=".../overnight-schematic/hardware")` — resolved paths correctly pointed into the worktree.
4. `sch_get_symbols` against the redirected project returned the same 111 symbols as the main copy — confirms the server is actually reading the worktree file, not silently falling back to the main one.
5. Confirmed `git status` on the main repo showed no changes — the redirect doesn't touch the original `hardware/`.
6. Redirected back to the main `hardware/` and removed the dry-run worktree (`git worktree remove`); the branch itself (`overnight-schematic-dryrun`) is orphaned but harmless — `git branch -D` got blocked by the harness's destructive-command guard even though it's unmerged and empty of real work. Left in place; can be deleted manually.

**Conclusion**: the redirect mechanism in the overnight-orchestrator prompt (Step 0) works as written. Cleared to launch the real overnight run — it should use a fresh worktree name (not `overnight-schematic`/`overnight-schematic-dryrun`, both now free again after cleanup, but a dated/unique name avoids any confusion with tonight's dry-run in this log).

## 2026-08-01 — overnight run attempt: blocked at Step 0 by `KICAD_MCP_PROFILE`, not operating mode

**Launched the real overnight run** per the orchestrator prompt. Step 0 completed cleanly: worktree `.claude/worktrees/overnight-schematic-run1` on branch `overnight-schematic-20260801`, branched from `2f6dc68`. `.claude/settings.local.json` and this logbook copied in. `kicad_set_project` redirected correctly (`kicad_get_project_info` showed all paths under the worktree). `kicad_get_server_info` confirmed `operatingMode.active: "write"` — the check the prompt says to stop on was green.

**Then found a second, undocumented gate**: none of the `sch_*`/`pcb_*` **write** tools (`sch_add_no_connect`, `sch_move_symbol`, `sch_resolve_label_overlaps`, `pcb_sync_from_schematic`, `vcs_commit_checkpoint`, etc.) are actually callable this session, despite `operatingMode.active` reporting `write`. Root cause, confirmed directly (not inferred): `.mcp.json`'s `KICAD_MCP_PROFILE` is still `"default"`, and **profile — not operating mode — is what gates which tools the client can see.** `kicad_list_tool_categories` under `default` returns only 6 categories (`project`, `pcb_read`, `schematic`, `library`, `validation`, `dfm`), 24 tools total — no `pcb_write`, no write-side `schematic` ops, no `vcs`. `kicad_get_tools_in_category("schematic")` lists exactly the 4 read tools (`sch_get_symbols`, `sch_get_connectivity_graph`, `sch_get_bounding_boxes`, `sch_visual_qa`) even while stating `Active operating mode: write` in the same response — the mode is write, the profile just never advertises the write tools to begin with.

**This means the 2026-08-01 write-mode dry-run entry above was incomplete**: it verified `operatingMode.active` flips to `write` and that write tools report `available: true` in `kicad_get_server_info`'s big capability dump, but never actually *called* a write tool end-to-end that session — the dump conflates "unblocked by mode" with "exposed by profile," and those turned out to be two different gates.

**Not something I can fix and continue past tonight** — same shape as the mode flip: an env var in a `.mcp.json` that only takes effect on MCP server respawn, which needs a session reload/restart only the user can trigger. Candidate fix for next time: set `KICAD_MCP_PROFILE` to a profile that includes write categories — `kicad_list_tool_categories`'s profile list includes `agent_full`, `builder`, `schematic_authoring`, `schematic`, `full` as candidates; `agent_full` reads most likely to cover everything this workflow needs (schematic + PCB write + vcs), but wasn't tested this session.

**Stopped per the prompt's own discipline** ("if it's still readonly... stop and don't proceed") rather than fabricate schematic edits with tools that were never actually available. **No schematic/PCB files were touched** — only read-only calls (`sch_get_symbols` implicitly via the earlier dry-run, `kicad_list_tool_categories`, `kicad_get_tools_in_category`) were made this session. Worktree `overnight-schematic-run1` / branch `overnight-schematic-20260801` left in place (zero commits, safe to reuse once the profile is fixed) rather than torn down, so the next run doesn't redo Step 0. MCP project pointer redirected back to the main `hardware/` before ending the session.

**Next action for the user**: edit `.mcp.json`'s `KICAD_MCP_PROFILE` (try `agent_full`), reload Claude Code, then re-verify with `kicad_get_tools_in_category("schematic")` that write tools actually appear — not just that `operatingMode.active` says `write` — before trusting an unattended run again.

**Update, same session**: at the user's request, flipped `.mcp.json`'s `KICAD_MCP_PROFILE` from `"default"` to `"agent_full"` directly (previously just a recommendation above). **Not yet verified** — same as the operating-mode flip earlier, this needs a Claude Code reload (new MCP server spawn) before it takes effect, and the user has to trigger that. Once reloaded, re-check with `kicad_get_tools_in_category("schematic")` / `("pcb_write" or similar)` before assuming write tools are actually exposed — don't just trust `operatingMode.active`, that was the mistake that caused this whole blocked run.

## 2026-08-01 — reload confirmed profile fix, but hit a third, deeper gate: no schematic write can succeed on this file at all without live KiCad IPC

User reloaded Claude Code. `kicad_get_project_info` now reports `Server profile: agent_full` (was `default`), and `kicad_list_tool_categories` now shows the real category sizes (`schematic`: 84 tools, `pcb_write`: 48 tools, etc.) — the profile fix from the previous entry worked as expected.

**But most of those tools are still not actually usable.** `kicad_get_tools_in_category("schematic")` annotates roughly half of the 84 schematic tools `— unavailable at runtime; requires live schematic write` — including every symbol/label **move** and **delete** tool (`sch_move_symbol`, `sch_move_label`, `sch_delete_symbol/wire/label`, `sch_resolve_label_overlaps`, `sch_straighten_wires`, `sch_align_to_grid`, `sch_normalize_power_orientation`, `sch_fix_readability`, `sch_add_missing_junctions`, the whole plan-based apply/preview/verify/rollback family) and the entire `pcb_write` category (`pcb_sync_from_schematic`, `pcb_add_footprint`, `pcb_move_footprint`, everything — `adapterRouting.categories.pcb_write` reports `availableCount: 0` of 48). Root cause, from `kicad_get_server_info`'s diagnostics: `KiCad IPC is unavailable: Failed to connect to KiCad: Connection refused` — these tools' backend is `kicad-ipc`/`hybrid-file-ipc`, and there's no live KiCad process for the MCP server to talk to (`tasklist` confirmed: no `kicad.exe` running, only the two `kicad-mcp-pro.exe` server processes). Fixing this needs the KiCad **desktop GUI** open with its IPC API enabled (a one-time Preferences → Plugins toggle) — a GUI interaction outside this session's tool access, so left for the user rather than guessed at.

**Proceeded anyway with the subset of write tools NOT flagged unavailable** — `sch_add_no_connect`, `sch_add_wire`, `sch_add_label`, `sch_modify_property`, etc. (these use a `guarded-schematic-file` file-backed writer, not IPC, so they don't depend on the running app). Pulled the live `run_erc` findings with exact pin coordinates and cross-checked against design-inputs.md/circuit-draft.md before touching anything: confirmed U5 (PCA9555)'s `~INT` + `IO1_2`..`IO1_7` (7 pins) as legitimately unused per circuit-draft.md's explicit "10 of 16 I/O used, INT not in play" note; J1 (USB-C) `SHIELD`/`SBU1`/`SBU2` as legitimately unused (no altmode support needed); left J1 `CC1`/`CC2` alone rather than guessing — those need USB-C Rd pull-downs per spec for host power negotiation and nothing in circuit-draft.md confirms they're present elsewhere, so marking them no-connect could paper over a real missing-part gap, not a cosmetic ERC finding; U7 (SN65HVD230) `Vref` as standard-practice unconnected when unused by other bus transceivers.

**Every one of the 11 `sch_add_no_connect` calls failed identically**: `TOOL_EXECUTION_FAILED: Refusing to write orc.kicad_sch: the schematic mutation dropped structure (wire 313->302). The original file was preserved`. Ran a control test — a no-op `sch_modify_property(R1, Value, "10k")`, rewriting a field to its own current value, touching a completely unrelated part of the schematic — and got the **exact same error**. This rules out anything specific to no-connect markers or to the 11 pin locations: **the guarded-schematic-file writer cannot produce a safe round-trip write of this particular `orc.kicad_sch` at all, for any mutation, in this session.** Best-guess root cause, not confirmed: the earlier logbook entry already established this schematic was very likely originally built through `mixelpixx/KiCAD-MCP-Server` (a different tool), not kicad-mcp-pro — its serialization of some wire/net structure is plausibly not fully round-trip-safe through kicad-mcp-pro's file-backed guard, which fails closed (correctly) rather than risk silent data loss.

**Verified no damage**: `git status`/`git diff --stat` in the worktree showed only `hardware/output/erc_report.json` (a harmless re-run artifact) touched across all 12 failed write attempts — `orc.kicad_sch` itself is byte-for-byte unchanged. Reverted the report diff (`git checkout --`), confirmed clean tree, redirected the MCP project pointer back to the main `hardware/`.

**Net result: zero schematic edits possible tonight through this MCP server**, not because of a scope decision but because the write path itself can't safely touch this file without a live KiCad IPC connection — and IPC tools are the ones flagged unavailable above. This is a harder blocker than the mode/profile ones: those needed a config edit + reload; this one needs KiCad's GUI actually running with API access enabled, which only the user can set up (Preferences → Plugins → "Enable KiCad API," per KiCad's own docs — not independently confirmed against this KiCad 10.0.5 install this session), and possibly also needs the schematic opened and re-saved once inside actual KiCad to normalize whatever structure the guarded file-writer can't round-trip.

**Next action for the user**: (1) launch KiCad 10.0.5 (`C:\Program Files\KiCad\10.0\bin\kicad.exe`), open `hardware/orc.kicad_pro`, enable the IPC API in Preferences, and leave KiCad running; (2) re-run `kicad_get_server_info` and confirm `kicad.ipcAvailable: true`; (3) only then re-attempt a write (start with the same R1 no-op `sch_modify_property` test used here) to confirm the round-trip guard actually clears once IPC is live — don't assume it will just because IPC connects, since the "dropped structure" error may be about the file's own content, not just the transport. If the guard still fires with IPC up, the schematic may need a manual open-and-resave inside KiCad's GUI to normalize it before any agent — this one or a future overnight run — can safely mutate it again.

## 2026-08-01 — IPC actually went live; found a 4th, narrower gap (tool-list snapshot timing)

User enabled the IPC API in KiCad Preferences and got the GUI running with the project open (schematic + PCB editors both open). Took a few tries — my own `start "..."` shell call to open the project actually made things worse the first time (killed the API-enabled KiCad process, replaced it with a fresh one that didn't have IPC reachable — should not have done that; opening a project inside an already-running KiCad needs to happen from inside KiCad, not by launching a competing process). Lesson for next time: don't shell out to (re)launch KiCad mid-session — ask the user to do it from the existing window.

**Once genuinely live** (`kicad_get_server_info`: `ipcAvailable: true`, `livePcbContext: true`, `liveSchematicContext: true`, `ipcDocumentLoaded: true`, zero `diagnostics`), `adapterRouting.categories.schematic` flipped to `availableCount: 84/84` and `pcb_write` to `48/48` — every tool the server thinks it can serve.

**But the R1 no-op `sch_modify_property` retest still failed with the identical `wire 313->302` error.** Traced why: `sch_modify_property` (and `sch_add_wire`, `sch_add_component`) are tagged `hybrid-file-ipc` in `capabilities.liveEditingTools` — per the schematic category's own stated `fallbackPolicy`, that backend still writes through the file-based guarded writer *first* and only uses IPC "opportunistically" for reload afterward. IPC being live doesn't route around the file guard for these tools — it was never going to.

**The tools that would actually bypass the file guard are the 48 tagged pure `kicad-gui-ipc`** (`sch_move_symbol`, `sch_delete_symbol/wire/label`, `sch_resolve_label_overlaps`, `sch_align_to_grid`, `sch_normalize_power_orientation`, the plan-based apply/verify/rollback family, etc.) — these write directly against the live document in the running KiCad process, no file round-trip, no guard. **None of them are actually callable this session.** `ToolSearch` returns no match for any of them, and a direct call to `sch_move_symbol` returns `Error: No such tool available` — not a runtime failure, a missing-registration error. Best explanation: this client's MCP tool list is a snapshot taken at the last reconnect, and that reconnect happened *before* IPC came alive (mode/profile were already `write`/`agent_full`, but `ipcAvailable` was still `false` at that moment) — so the pure-IPC tools, which apparently aren't advertised until IPC is actually reachable, never made it into this session's callable set. `sch_add_no_connect` and `sch_modify_property` *did* get registered at that same snapshot (they're always-advertised, just gated by the file-guard at runtime) — consistent with this theory.

**Net effect, still zero schematic edits tonight**, but for a narrower, more mechanical reason than before: not a fundamentally broken file, just a stale tool-list snapshot that predates IPC coming up. The file-structural "dropped wire 313->302" issue is *specifically* a guarded-schematic-file-writer problem — it may not even apply to the pure-IPC tools at all, since they don't go through that writer.

**Next action**: reload Claude Code one more time (now, with KiCad already running, IPC already live, and the project already open — reload should pick up a tool list that includes the pure-IPC tools this time). Then retest with something cheap and reversible first — e.g. `sch_move_symbol` on a part to its own current coordinates — before trusting it for real fixes. If pure-IPC tools are available post-reload and the no-op move succeeds, that's the actual green light this whole night has been chasing.

## 2026-08-01 — reload picked up the pure-IPC tools, but the guard fires there too: it's a file-content problem, not a backend problem

Reload worked as predicted — `sch_move_symbol` (and the other 47 `kicad-gui-ipc`-backed tools) are now genuinely registered and callable this session.

**Retested the no-op move: `sch_move_symbol(R1, 69.85, 139.7)` — R1's own current coordinates.** Identical failure: `Refusing to write orc.kicad_sch: the schematic mutation dropped structure (wire 313->302)`. This kills the "hybrid-file-ipc vs pure kicad-gui-ipc backend" theory from the previous entry — `sch_move_symbol` is pure `kicad-gui-ipc`, no file-writer involvement per the adapter routing table, and it still hit the exact same guard with the exact same wire pair. The `mutationGuard: "atomic-roundtrip-loss-detection"` in `adapterRouting.categories.schematic` applies to the whole category, all 84 tools, as a pre-flight check — not something specific to the 14 file-backed ones.

**Conclusion, now with real confidence**: this is not a transport problem (mode, profile, or IPC reachability) at all. It is a property of `orc.kicad_sch`'s actual on-disk content — some wire structure the guard's round-trip parser can't losslessly represent, independent of which backend would perform the write. Every environment fix tonight (operating mode, tool profile, IPC connection) was real and necessary, but none of them could have fixed this, because this was never the blocker they were built to guard against.

**Likely fix, not yet tried**: since KiCad now has the schematic open live in its own GUI, a plain save from inside KiCad (File → Save / Ctrl+S in the schematic editor) would make KiCad's own native writer re-serialize the file in its current canonical form — which would very likely resolve whatever the guard's parser can't handle, since the guard is presumably comparing against what KiCad's own writer would produce. Worth trying next, before assuming a deeper repair is needed.

**Tried it — partial signal, not a fix.** User saved from inside KiCad (Ctrl+S in the schematic editor). Retested `sch_move_symbol(R1, 69.85, 139.7)` — R1's own current position, same no-op test as before. **The wire pair in the error changed** (`wire 313->302` → `wire 303->302`), proving the save genuinely re-wrote and re-indexed the file's internal structure — this is real signal, not a cached/stale response. **But the guard still fires**, on a different-but-structurally-similar wire pair. This rules out "stale serialization" as the root cause: KiCad's own canonical writer produces a file that the guard *still* can't round-trip cleanly. Whatever this is, it's a genuine structural characteristic of how this schematic's wires are connected (something like a >2-point wire segment, overlapping/duplicate segments at a junction, or a zero-length segment) — not a formatting/staleness artifact fixable by re-saving.

**Where this leaves things**: all four environment layers chased tonight (operating mode, tool profile, IPC reachability, tool-list snapshot timing) are now genuinely fixed and confirmed working — `sch_move_symbol` runs, gets past auth/routing/registration, and reaches the actual guard logic. The guard itself is the last wall, and it's guarding against something real in the file's wire topology near whatever's structurally at index ~302-313. **Still zero schematic edits made tonight** — every attempt, file-backed and pure-IPC alike, before and after the KiCad save, has been safely rejected rather than risking data loss. No corruption, no partial writes, confirmed each time via `git diff --stat` showing `orc.kicad_sch` unchanged.

## 2026-08-01 — morning session: root-caused and fixed the write blocker, restored ground isolation, real commit landed

**Root cause of the entire multi-session write blocker, finally found.** `sch_get_connectivity_graph` revealed a 257-point connectivity group merging `3V3_A, COIL_9V, FB_B, GND_A, GND_B, PWR_FLAG, VIN_BUCK_A, V_COIL_IN` — literally every major rail on the board, including both isolated ground domains, into one electrical net. This malformed graph is almost certainly what the round-trip guard's structural fingerprint was choking on for every single mutation attempt across the whole night, regardless of target coordinates or backend (file or pure-IPC) — not a tooling bug, a real, severe pre-existing schematic defect.

**Fix 1 — stray stacked label.** Found `COIL_9V` and `GND_B`... no — found `FB_B` (the 9V buck's own feedback-divider sense node, R12/R13 midpoint) with a spurious `COIL_9V` label stacked exactly on top of it at (289.56, 328.93). Deleted the stray `COIL_9V` instance via `sch_delete_label`. **This was the first successful schematic write of the entire session** — confirmed the guard could pass once the underlying defect was gone. `FB_B` is now its own correctly-isolated net.

**Fix 2 — the real killer: PWR_FLAG value collision.** All 6 `PWR_FLAG` symbols (PF1, PF3, PF5, PF6, PF7, PF8) had the untouched default `Value="PWR_FLAG"`. In KiCad, a power symbol's Value field *is* its net name — so despite each flag being individually, correctly wired to a different real rail (GND_A, 3V3_A, VIN_BUCK_A, GND_B, V_COIL_IN, COIL_9V respectively — verified via `sch_get_wires`/label tracing, no mislabeling on any of the six), all six were nevertheless the *same* implicit net by symbol-identity alone. This is a well-known, documented KiCad gotcha for exactly this multi-PWR_FLAG pattern. **This is what was silently shorting `GND_A` to `GND_B`** — defeating the ADuM1250 isolation barrier's entire purpose on paper, plus merging 3V3_A/VIN_BUCK_A/COIL_9V together. Fixed by giving each instance a unique Value (`PWR_FLAG1`..`PWR_FLAG8`) via `sch_modify_property` — all 6 calls succeeded.

**Verified via connectivity graph, before/after:**
- Before: 1 group, 257 points, spanning both domains.
- After: split into 5 correct, separate nets — `3V3_A` (36 pts), `GND_A` (56 pts, Domain A only), `COIL_9V`+`GND_B` (82 pts, still merged — see below), `V_COIL_IN` (54 pts), `VIN_BUCK_A` (20 pts). **`GND_A` and `GND_B` are now separate** — the isolation-barrier defeat is fixed.

**Committed** (`2874184`, main branch, directly — user explicitly chose to skip worktree isolation this session since they were actively supervising). Also swept in `.kicad_pro`'s auto-rewritten default schema (harmless KiCad-10-on-first-open normalization, not hand-edited) and gitignored the tool's `.kicad-mcp/` visual-diff cache directory.

**Still open, not fixed**: ERC still reports `COIL_9V`/`GND_B` sharing net membership (warning-level, and the associated PF3/PF7 pin-to-pin short remains an error). Extensively investigated and ruled out as causes: exact-coordinate label collisions (zero, even at 1mm tolerance), wire-to-wire shared endpoints (zero, confirmed twice), and ~10 individually-traced pins across C12, U4, J2, D4, D5, Q2 in the merged cluster — every single one traces to its textually-correct, sensible label with no visible miswiring. `sch_render_png` isn't available (missing CairoSVG/Pillow on this machine); `export_sch_svg` produced a file but isn't visually inspectable through this session's tools. **This one likely needs a human looking at the rendered schematic in KiCad itself** — something in Domain B's coil-supply area visually crosses between the COIL_9V and GND_B nets that coordinate-archaeology alone hasn't surfaced. Whoever picks this up next: open `hardware/orc.kicad_sch` in KiCad, use Highlight Net on `COIL_9V` and `GND_B` and look for where the highlighted traces meet.

**Remaining scope, entirely untouched**: the 42 legitimate no-connect markers identified hours ago (ESP32 GPIOs, USB-C SHIELD/SBU1/SBU2, PCA9555's 7 unused pins, CAN transceiver Vref) — now that write actually works, these should go fast. ADuM1250 GND2-not-grounded warning. `sch_visual_qa`'s overlap findings (Q5/C19, C20/C14 symbol overlaps; stacked VIN_BUCK_A/GND_A label instances — note: same-name label stacking is cosmetic only, not an electrical bug like the FB_B/COIL_9V case was). PCB sync (0 footprints, needs `pcb_sync_from_schematic` now that schematic is closer to clean).

## 2026-08-01 — same morning session, continued: ERC 46→4, then a bigger wall, session paused by user

**Placed all 39 previously-identified no-connect markers** (J1 SHIELD/SBU1/SBU2, U5's ~INT + 6 unused I/O pins, U7 Vref, all 28 of U1's currently-unassigned GPIOs) — real coordinates pulled fresh via `sch_get_pin_positions` rather than trusting the ERC report's scaled units (caught before placing anything: the ERC report's `pos` field is the real mm coordinate ÷100, and my very first attempt hours ago used the raw unscaled value — harmless since it failed on the guard bug anyway, but would have placed no-connects in the wrong spot had it succeeded). All 39 calls succeeded.

**Fixed ADuM1250 GND2 warning properly** — it was already wired to a `GND_B` label (confirmed via wire trace), but ERC's `ground_pin_not_ground` check specifically wants a real `power:GND`-type symbol on the net, not just a matching label name. Added one via `sch_add_power_symbol(name="GND", ...)`, then renamed its Value to `GND_B` (same per-instance-value pattern as the PWR_FLAG fix) so it joins the right net instead of KiCad's global default `GND`.

**Fixed U8 (USB ESD array) I/O1/I/O2** — pins 1/3 were already on `USB_DP`/`USB_DM` (paralleling J1↔U1's direct wiring), but the array's output pins 6/4 were never brought onto those nets, so the part existed but wasn't actually in the signal path. Added `USB_DP`/`USB_DM` labels directly at I/O1/I/O2's pin coordinates. **Caveat logged**: this is a parallel stub, not true series insertion — a from-scratch layout would route D+/D- *through* the ESD array by cutting the existing direct J1-U1 wire. Didn't attempt that blind; flagged instead.

**Fixed C20 (CIN bypass cap) pin 2**, found genuinely floating (no wire at all) — added a fresh wire+`GND_B`-label stub matching every other single-connection pattern in the file, rather than reusing an already-occupied label point.

**Result: ERC 46 → 4 violations**, verified via `run_erc` after each batch. Committed in two additional commits (`9b540b3`, `9bed6ca` — the second being a connectivity-safe `sch_resolve_label_overlaps` justify-flip on 3 stacked label pairs). All work landed directly on `main` per explicit user choice (worktree isolation skipped this session since the user was actively supervising) — worktree `overnight-schematic-run1` removed at session end, unused.

**Remaining 4 ERC violations, not fixed**:
- J1 CC1/CC2 unconnected — real gap needing sourced USB-C Rd pull-down resistors, not an ERC-hygiene fix. Flagged hours ago, still open.
- PF3/PF7 pin-to-pin short + the `COIL_9V`/`GND_B multiple_net_names` warning — same underlying bug. **Extensively investigated and not root-caused**: ruled out exact and 1mm-fuzzy label coincidence, ruled out shared wire endpoints (twice), individually traced ~10 pins across the merged cluster (C12, U4, J2, D4, D5, Q2, U2's own secondary-side pins) — every one resolves to its textually-correct label. `sch_render_png` unavailable (missing CairoSVG/Pillow), `export_sch_svg` produced a file this session's tools can't visually inspect. **Needs a human with KiCad's own Highlight Net tool** on `COIL_9V` vs `GND_B` — coordinate-archaeology alone didn't find it across a genuinely thorough attempt.

**Hit a much bigger wall next: tried `pcb_sync_from_schematic`.** First attempt correctly refused on the 4 remaining ERC violations (gate discipline working as designed — did not force past it). Second attempt, with explicit user sign-off to force past the ERC gate for debugging, hit a different and much larger problem: **none of the 105 schematic symbols have footprint assignments at all.** This is a Gate 2 problem (`hardware-workflow.md`: footprint must be confirmed-matching before the part goes on the schematic), not something to blast through — assigning 105 real footprints correctly means cross-checking each against `BOM.md`/circuit-draft.md's actual sourced package per part, a full pass in its own right.

**Session paused here at user's explicit direction**: *"I don't think this automated kicad stuff is worth pursuing at this point. I will perform the design manually."* Stopping all KiCad-MCP-driven schematic edits. Everything committed is real, verified, and safe to build on by hand in KiCad directly — nothing was left mid-edit or uncommitted.

### Net summary of the whole multi-session saga, for whoever reads this next

**What's durably fixed and true right now:**
- `.mcp.json`: `KICAD_MCP_OPERATING_MODE=write`, `KICAD_MCP_PROFILE=agent_full` (both committed in git history at `2f6dc68`/earlier).
- KiCad's IPC API is enabled in Preferences (one-time GUI setting, should persist across restarts — just keep KiCad running with the project open for any future automated session).
- **The schematic's two ground domains (`GND_A`/`GND_B`) are no longer shorted together** — this was a genuine, severe, previously-undetected defect (all 6 `PWR_FLAG` symbols sharing the default `Value="PWR_FLAG"`, which in KiCad IS the net name) that silently defeated the ADuM1250 isolation barrier's entire purpose. Fixed and verified via connectivity-graph before/after.
- `FB_B` (9V buck's own feedback sense node) is no longer shorted to `COIL_9V` (was a stray stacked label).
- ERC: 46 violations → 4. The 4 remaining are real and documented, not noise.

**What's still broken or unstarted, for the manual pass:**
- `COIL_9V`/`GND_B` still share net membership via an un-found mechanism — check with Highlight Net in KiCad's GUI first.
- J1 CC1/CC2 need real USB-C Rd pull-down resistors sourced (Gate 1 pass).
- All 105 symbols need footprint assignments (Gate 2) before PCB sync can do anything.
- `sch_visual_qa` symbol-body overlaps (Q5/C19, C20/C14) need manual moves plus manual wire re-routing to their pins (moving a symbol via the MCP tools does not drag its connected wires — confirmed risk, not attempted).
- PCB side is completely unstarted: 0 footprints, board outline undefined.

## Earlier notes, superseded by the above (kept for history)

**Two paths forward, neither attempted**: (1) find and manually inspect wire object(s) around index 302 in `orc.kicad_sch` (the wire numbering is presumably positional/sequential in the file, so a text search near a specific line range or a `sch_get_wires` dump filtered to short/coincident segments could locate the culprit) and hand-fix or delete-and-redraw whatever KiCad's own editor considers valid but this guard's parser doesn't; or (2) the error hint mentions "use an explicit destructive path only for intentional delete/replace operations" — implying kicad-mcp-pro has some override for accepting a lossy write when the loss is intentional, not yet located or tried, and not something to reach for without understanding what would actually be lost first.

**Tried path (1), it's a dead end as stated.** Counted `(wire` blocks in the raw `orc.kicad_sch` (`grep -c "^\s*(wire"` = 303, matching `sch_get_wires`'s reported total) and read wires #302 and #303 in raw file order (lines 9696 and 9706): `fe2fa928...` (30.48,228.6 → 30.48,236.22) and `ff17ec03...` (124.46,375.92 → 124.46,368.3). **Both are completely unremarkable** — plain 2-point vertical segments, standard `(stroke (width 0) (type default))`, no duplicate points, no zero-length, nothing visibly malformed. This disproves the assumption that the guard's `wire N->M` notation is a raw positional index into the file's `(wire ...)` blocks in document order. It's indexing into some internal representation built by the guard's own parser (possibly graph node IDs after connectivity analysis, not source-order wire objects) — invisible from outside the tool. Hand-correlating the error to a specific file location isn't tractable without knowing that indexing scheme, which isn't documented anywhere surfaced to this session (`kicad_help`, error hints, `kicad-mcp-pro doctor` output all reviewed, none explain it).

**Stopping here for tonight, at the user's direction.** Summary of the full session for whoever picks this up:
- Real, durable environment fixes made and confirmed: `.mcp.json` `KICAD_MCP_OPERATING_MODE=write` and `KICAD_MCP_PROFILE=agent_full` (both committed in git history), KiCad's IPC API enabled in Preferences (a one-time GUI setting, should persist), all 84 schematic + 48 PCB-write tools registered and callable.
- Remaining blocker is specific to `orc.kicad_sch`'s content, isolated to something the guard's round-trip parser calls wire-adjacent structure it can't losslessly represent — survives a native KiCad save, so it's not a formatting/staleness issue, and isn't locatable via raw file wire-order.
- Zero schematic mutations happened all session. File integrity fully intact, confirmed repeatedly via `git diff --stat`.
- Next real step for whoever continues this: either (a) contact kicad-mcp-pro's own support/issue tracker with the exact error string and this session's findings — this reads like it could be a genuine parser bug or an undocumented file-content constraint worth reporting upstream — or (b) try the smallest possible reproduction: create a fresh minimal KiCad 10 project with one or two wires, see if `sch_move_symbol`'s no-op test succeeds there, then incrementally narrow what in *this* schematic's history (built originally via a different tool, `mixelpixx/KiCAD-MCP-Server`, per the earlier logbook entry) trips the guard.

## 2026-08-01 — Gate 1 sourcing pass on the new hierarchical sheets

User pulled the repo, having manually captured 5 new hierarchical sheets (Power Side A, Power Side B, MCU, Communications, I2C Isolator) since the last entry — PCB already synced to these sheets' ref numbering (30 footprints placed), separate from the old flat 89-symbol root `orc.kicad_sch`. **Decision, confirmed by user**: root is legacy, the 5 new sheets are canonical going forward. Root still holds the only capture of Domain B's per-channel coil drivers + harness connectors (blocks 8-10 of subcircuit-capture-guide.md), not yet migrated.

**Flagged and resolved**: L3/L4 (buck inductors in the new sheets) were placed at 15µH — confirmed by user as an unintended template placeholder, not a re-derivation. Locked back to 68µH per the original circuit-draft.md sizing.

**Four parallel Gate 1 sourcing passes dispatched** (general-purpose agents, one focused part/group each per hardware-workflow.md's sourcing-pass pattern):

1. **68µH inductor replacement** (L1/L2, and L3/L4 once corrected) — old KOHERelec MDA1870-680M (C3015595) had only 194 units in stock. New pick: **SXN SMDRI127-680MT, C9907** — 49,310 in stock, Basic tier, 4A Isat/2.1A rated (3×+ margin over 0.67A load), DCR 140mΩ, 12.3×12.3×8mm (smaller footprint than the old part). One part covers both the 3.3V and 9V buck rails. Open item: automotive/extended-temp rating not stated on the listing.
2. **MMBT2222A (QN1-10 level-shift NPN)** — real live listing exists (Nexperia MMBT2222A,215, C179396) but doesn't appear on two independently-checked JLCPCB Basic-parts lists — tier circumstantially Extended, unconfirmed. Agent proposed MMBT3904 (JSCJ, C20526, 253,450 in stock) as a functionally-interchangeable substitute, but its tier is *also* unconfirmed — same JS-render scrape limit hit both searches. **Left as an open two-way decision in BOM.md**, not picked — needs one manual LCSC/JLCPCB UI check before locking either way.
3. **P-channel MOSFETs, two sub-tasks**: (a) AO3401A (already-sourced general-purpose part, C15127) re-verified live — 186,375 in stock, spec solid; tier badge didn't render to automated fetch on either LCSC's or JLCPCB's page, circumstantially Basic per third-party lists, not independently confirmed. (b) Domain B's upsized reverse-polarity FET (was placeholder Q2/AO3401A, flagged undersized) — **DMP4015SK3Q-13, C461089**: TO-252/DPAK, AEC-Q101, 40V/35A, 7-9mΩ Rds(on), 1,544 in stock. Clears every stated requirement with large margin. Tier likely Extended given stock depth/price, unconfirmed.
4. **PCA9555PW (U5, I2C expander)** — prior LCSC number was malformed/unverified. Real pull: **NXP PCA9555PW,118, C128392** — TSSOP-24, 8,773 in stock. Tier didn't render to fetch, circumstantially Extended. **Flagged: datasheet temp range is -40 to +85°C, right at the edge of this enclosure's 65-85°C ambient with near-zero self-heating margin** — same class of concern as the earlier PTC 85°C-derating finding. Worth an AEC-Q100 alternative check before final lock.

All four results folded into `hardware/BOM.md` (see its "Pending KiCad edits" section for what's not yet applied to the actual .kicad_sch files — L3/L4 value+footprint, U5 part field, Q2 footprint swap to TO-252, QN1-10 pending the tier decision above).

**Recurring pattern across all four passes**: LCSC/JLCPCB's Basic vs. Extended assembly-tier badge does not reliably render to automated WebFetch — this hit every single sourcing task this round, not a one-off. Added to hardware-workflow.md's Gate 1 notes: **tier claims from an agent sourcing pass should be treated as circumstantial until a human does one direct visual check on the actual LCSC/JLCPCB page**, even when everything else (stock count, spec, part identity) came back solid.

**QN1-10 decided**: user picked MMBT3904 (C20526) over the literal MMBT2222A — Basic-tier preference, both were functionally fine. Locked in BOM.md.

**docs/subcircuit-capture-guide.md rewritten** to organize by the actual 5-sheet hierarchy (Power Side A/B, MCU, Communications, I2C Isolator) instead of the old block-number scheme against legacy root — ref designators were reassigned during the split so the old numbering doesn't cross-reference anymore. Pulled live net names per sheet rather than assuming carryover; a few things surfaced that need a human look before more parts get sourced against them:
- **Communications sheet has two unexplained refs**: J5 (a bare 2-pin header, no clear role) and R13 (10kΩ *0.1%* — doesn't match any legacy-block-4 function; nothing else on this design uses 0.1% tolerance except the new CC pull-downs, which are 5.1k not 10k). Flagged in the guide rather than guessing a sourcing target for either.
- **MCU sheet has a bare `GND` power symbol** near the new SW1 tactile switch, where every other Domain A ground uses `GND_A` specifically — worth confirming intentional before it becomes an ERC finding later.
- **SW1 itself has no documented function** — new component, not in any prior BOM or wiring doc. Needs a one-line spec (what does it do, what net) before Gate 1 sourcing makes sense.
- Power Side A/B sheets currently only have the FB-divider/switching-node side wired — CIN/COUT bulk caps and bypass from the legacy design aren't placed yet. Flagged as an in-progress gap, not a design change.

## 2026-08-01 — documentation audit + BOM.md retired

Full audit requested ("anywhere we might be missing a part") — pulled fresh symbols from root + all 5 sheets + PCB footprints and cross-checked everything. Findings, most to least severe:

1. **BOM.md contradicted itself**: intro text claimed SS56 was the corrected catch-diode value, but the table listed SS34 as confirmed-sourced (C8678) — and the two schematic locations disagreed accordingly (root used SS56, new Power Side sheets used SS34). **User resolved: SS34/C8678 is correct.** Root's SS56 is stale but left alone since root is legacy.
2. **PCB significantly out of sync**: entire Power Side B subcircuit (U10/R26/L4/R27/D6) missing from PCB; U7 (ESP32) and U9 (ADuM1250) missing despite their support passives being placed; what *is* on the PCB uses stale pre-reannotation ref numbers (U3→U8, R18/R19→R24/R25, same physical parts). Needs a `pcb_sync_from_schematic` pass later, not now — schematic isn't far enough along yet to make that worth doing twice.
3. **D1 (SMBJ26CA)** still placed in root despite BOM.md documenting it as dropped/to-be-deleted.
4. **Root's `J2` carries an embedded "(C2827883)"** LCSC-shaped number that never went through a documented Gate 1 pass and never made it into any BOM's LCSC column — flagged as unverified, not to be trusted.
5. **Stray `R4 22kΩ` in root** with no documented function anywhere — possibly a leftover from the dropped TVS/load-dump analysis network.
6. Confirmed still-open, no new info: SW1, J5, R13 (all unresolved from the prior pass).

**BOM.md retired** — user call ("get rid of the bom document since it's redundant and confusing"). Everything still current from it (full parts tables, tier caveats, the "pending KiCad edits" list, the 🔧 footprint-work section) was folded into docs/subcircuit-capture-guide.md, which is now the single source of truth for parts/LCSC numbers, organized by the same 5-sheet hierarchy. References in README.md and circuit-draft.md updated to point at subcircuit-capture-guide.md instead. hardware/scripts/rebuild.py has a couple of comments mentioning BOM.md — left alone, no functional dependency (the script never reads the file).

Committed as `abf8ed5`.

## 2026-08-01 — SW1 resolved, GPIO0/BOOT button added

User confirmed SW1's function: ESP32-S3 EN/reset button. Verified live against the schematic (`sch_trace_net`): `EN_A` has 3 connections (pull-up + EN pin + SW1) matching that story exactly; `GPIO0_A` has only 2 (pull-up + pin) — no switch, confirming there was no way to force bootloader/download mode by button press.

Flagged that EN alone doesn't select boot mode — GPIO0 does, and without a button there, recovering from a wedged native-USB auto-reset path on this sealed-enclosure board means desoldering. User agreed to add a second button (SW2) on GPIO0, same part as SW1.

Dispatched a Gate 1 sourcing pass for TS-1187A-B-A-B (SW1 had never actually been sourced — placed with a value but no LCSC number). User provided the number directly (C318884) before the agent finished; verified it live via WebFetch rather than trusting it unchecked: **XKB Connection TS-1187A-B-A-B, C318884** — SMD-4P 5.1×5.1mm, 12V/50mA, **-30 to +85°C** (comfortably clears this enclosure's 65-85°C ambient, unlike the PCA9555's thin-margin case), 100k-cycle life, 1,068,940 in stock. Tier badge didn't render to fetch (the now-familiar pattern) — stock depth suggests Basic, not independently confirmed.

Updated subcircuit-capture-guide.md: SW1 now ✅ with real LCSC and confirmed function; SW2 added as a new 🔧 line (part sourced, not yet drawn — needs to land on `GPIO0_A` ↔ GND on the MCU sheet). Removed SW1 from the "still unresolved" housekeeping list.

**Background sourcing agent for this same part was still running when the user gave the number directly** — its result will land as a separate notification; reconcile against C318884 when it does rather than treating both as independent findings.

## 2026-08-01 — `git pull` collided with a parallel session

Committed the SW1/SW2 doc update (`0dcdef7`), then `git pull` pulled in `2115714` ("Source USB-C CC pull-downs and confirm CAN terminal part") — a **different session/instance working on this repo in parallel**, evidenced by real edits to `hardware/mcu.kicad_sch`, `orc.kicad_pcb`, `orc.kicad_pro`, `orc.kicad_sch` alongside doc changes. Two things worth knowing:

- **That session recreated `hardware/BOM.md`** from a pre-retirement snapshot — it doesn't know BOM.md was retired in favor of subcircuit-capture-guide.md (`abf8ed5`, earlier today). Its actual new content (R11/R12 CC pull-downs sourced to YAGEO RT0402BRD075K1L/C852856; J5/J6's previously-flagged "(C2827883)" number confirmed correct — DORABO DB128L-5.08-4P-GN-S, Extended, 16A/300V, 28.9k stock) had already been independently merged into subcircuit-capture-guide.md by git's auto-merge (non-conflicting hunks). **Re-deleted BOM.md again** rather than let it come back — nothing of value was lost, everything it added is in the capture guide now.
- **One real merge conflict**, in the capture guide's 🔧 footprint-work section — my SW2 bullet and their more-detailed J5/J6-confirmed bullet were adjacent but not overlapping; kept both.
- **Confirms one Housekeeping item from earlier today was wrong to leave as "unverified"** — root's `J2` "(C2827883)" number turned out to be genuinely correct, not a stray. Good catch by whoever ran that other session; the capture guide's Housekeeping section now reflects the confirmed state.

Local is now ahead of `origin/main` by this merge commit (`30bf9b0`) — not pushed. If another session is actively working on this repo, worth pushing soon to avoid a second collision, and worth being aware there's at least one other active session before assuming exclusive ownership of `hardware/` going forward.

## 2026-08-01 — mechanical finding reopens the CAN-vs-USB decision

User measurement: only ~8.5mm clearance between the two stacked PCBs in the enclosure, not enough room for both the USB-C connector and the DC+CAN screw terminal on a single-sided board. This fires circuit-draft.md's own long-standing caveat ("CAN is provisional — descope if it doesn't comfortably fit the board") but in the opposite direction than anticipated — USB is what doesn't fit, not CAN.

Direction under consideration, not locked: drop the bare ESP32-S3-WROOM-1U + custom USB-C/ESD/CC-pulldown circuit (U7/J4/U5/R11/R12), replace with a complete pre-made ESP32-S3 module that already has USB-C built in, used for programming/debug only — CAN becomes the primary control path. Hard requirement carried forward: external antenna (U.FL) support, since this board is in a sealed metal enclosure — disqualifies most cheap PCB-antenna-only modules.

Noted the underappreciated upside: this doesn't just solve the mechanical problem, it retires Q1/Q3/Q4/Q5 (the USB-VBUS-vs-DC-terminal source-select stage) entirely, including Q4/Q5's gate-drive chain, which has been an unresolved electrical gap since early in the project ("values never worked out... likely needs a second stage").

Dispatched a research pass (background agent) for real ESP32-S3 module candidates meeting: USB-C onboard, external-antenna support (verified per-candidate, not assumed), ≥8MB flash, live purchasable, reasonably compact. Flagged in subcircuit-capture-guide.md as an OPEN ARCHITECTURE DECISION — **don't source or lock U7/J4/U5/R11/R12 further until it lands**, and don't treat SW1/SW2 as final since the eventual module may already have its own EN/BOOT buttons. Also updated circuit-draft.md's decisions table with a "reopened" entry cross-referencing the same note.