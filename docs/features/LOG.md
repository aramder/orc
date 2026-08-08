# Work-item ID log

Retired/active ID index for `docs/features/`. See [README.md](README.md) for
the shard convention. Next free ID: **FR-004** / **BUG-002**.

| ID | Title | Status |
|---|---|---|
| FR-001 | USB bench/debug interface firmware (`usb_bench`) | implemented (reopened + fixed + real-hardware re-verified 2026-08-05 — halted before `PING` was reachable without a PCA9555; fix confirmed live on the same bare devkit) |
| BUG-001 | `canopen_app` halts before joining the CAN bus if PCA9555 is absent | implemented (same bug shape as FR-001, found looking for it deliberately, 2026-08-05) |
| FR-002 | Signal relay-hardware faults on the CAN wire, not just Serial | open (logged as a to-do, 2026-08-05 — decided yes, not yet designed/implemented) |
| FR-003 | Make applied channel state readable on demand, not only after a command | open (2026-08-07 — blocks `rigos-core` FR-062's CAN path; SDO read preferred over periodic TPDO1) |
