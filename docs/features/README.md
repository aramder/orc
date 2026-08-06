# Work-item shards (`docs/features/`)

Adapted from the sibling `rigos-core` repo's own `docs/features/` convention
(`rigos-core/docs/features/README.md`) — same shard shape, no tooling ported
over (`orc` is a single-contributor-at-a-time repo so far; no
`tools/alloc_id.py`, no race to guard against). If that ever changes, port the
allocator too rather than hand-picking IDs by convention.

Each work item is **one file**, `docs/features/<ID>.md`. `<ID>` is
`FR-NNN` | `BUG-NNN` | `RES-NNN` | `CHORE-NNN`. IDs are allocated by hand —
check [LOG.md](LOG.md) for the next free number before creating a shard, and
add the new ID to LOG.md in the same commit that creates the shard, so the
existence of an LOG.md entry is the source of truth for "this ID is taken,"
same rule as rigos-core.

- Resolved shards may be deleted once genuinely done, keeping the full
  write-up in git history; LOG.md keeps the ID retired either way.
- `status` is a field **inside** the shard.
- No generated rollup exists yet (no `gen_features_rollup.py` here) — LOG.md
  doubles as the index for now. Add a rollup script if/when the shard count
  makes that worthwhile.

## Shard format

```markdown
---
id: FR-001
title: Short imperative title
type: FR            # FR | BUG | RES | CHORE
severity: MEDIUM    # LOW | MEDIUM | HIGH  (omit/none for RES)
status: open        # open | in-progress | implemented | done
owner:
branch:
files:               # anticipated/actual touched paths
  - firmware/src/example/main.cpp
depends_on:          # other IDs, or empty
---

**Goal:** one or two sentences.

**Background / Scope / Acceptance criteria / Depends on / Open questions:**
free markdown, same sections rigos-core's shards use.
```

`status` lifecycle: `open → in-progress → implemented → done`. `RES-` items
use `open → in-progress → done`.
