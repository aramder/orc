# Work-item ID log

Retired/active ID index for `docs/features/`. See [README.md](README.md) for
the shard convention. Next free ID: **FR-006** / **BUG-004**.

| ID | Title | Status |
|---|---|---|
| FR-001 | USB bench/debug interface firmware (`usb_bench`) | superseded by FR-004 (2026-08-10) — `usb_bench` retired, its function consolidated into `canopen_app`. Real-hardware findings/fixes stay valid history. |
| BUG-001 | `canopen_app` halts before joining the CAN bus if PCA9555 is absent | implemented (same bug shape as FR-001, found looking for it deliberately, 2026-08-05) |
| FR-002 | Signal relay-hardware faults on the CAN wire, not just Serial | open (logged as a to-do, 2026-08-05 — decided yes, not yet designed/implemented) |
| FR-003 | Make applied channel state readable on demand **over CAN** | open — **narrowed 2026-08-29.** The USB half (`GET`/`STATUS`) shipped with FR-004 and is done; only the CAN half remains. Still blocks `rigos-core` FR-062's **CAN** path (its I2C and USB paths are unblocked). Depends on FR-005. SDO read still preferred over periodic TPDO1. |
| FR-004 | Consolidate `usb_bench` into `canopen_app` — drive relays from CAN or USB | implemented (2026-08-10, real hardware — `9eea0e6`). Status reconciled 2026-08-29; see the shard for which acceptance items were independently re-verified and which rest on the commit record. Delivered the USB `GET`/`STATUS` read-back that closed FR-003's USB half. |
| BUG-002 | `canopen_app`'s TPDO1 confirmation lags/drops one cycle due to a blocking `Serial.printf()` ahead of the send | implemented (found via real-hardware bench sweep + driver-source read, fixed with `Serial.setTxTimeoutMs(20)`, re-verified 12/12 on real hardware, 2026-08-10) |
| BUG-003 | `canopen_app`'s `loop()` appears to enter a persistently slow state under sub-second RPDO1 commanding, starving Heartbeat/TPDO2 | implemented (2026-08-10, real hardware, 2 rounds — TX-queue backlog fix + `printBestEffort()` fixed the firmware-side contributor, confirmed via same-session A/B: 1/15→15/15 with a console reader, 1/15→6/15 without; remaining ~60% failure below ~1s cadence traced to real `CAN_ERR_CRTL` bus-level error frames, not firmware — resolved as a documented ~1s safe commanding interval in `docs/can-protocol-research.md`/`tools/README.md` rather than further firmware iteration) |
| FR-005 | Replace the hard-coded SDO object handler with an object-dictionary table | open (2026-08-29 — split out of FR-003; shared prerequisite for FR-003 and FR-002) |
