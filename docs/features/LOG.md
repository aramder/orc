# Work-item ID log

Retired/active ID index for `docs/features/`. See [README.md](README.md) for
the shard convention. Next free ID: **FR-006** / **BUG-006**.

## Firmware build order — read before picking up work

**FR-005 -> FR-003.** FR-005 is done (2026-08-29, merged to `main`, verified
on real hardware); **FR-003 is next up.**

> ⚠️ **Severity inverts against the order here, deliberately.** FR-005 is
> MEDIUM and FR-003 is HIGH, so picking by severity alone selects FR-003 —
> which cannot be built first. FR-003 needs a second SDO object, and the
> handler is a hard-coded single (index, sub-index) pair; adding one without
> FR-005 means a parallel branch that FR-005 then has to unpick.
>
> **FR-002** also sits behind FR-005 *if* it signals faults via a new
> object-dictionary entry. If it uses TPDO2's spare byte 7 instead, it does
> not. **Decide that mechanism before starting FR-002**, then keep or drop the
> dependency.

Downstream: `rigos-core` **FR-062**'s CAN transport waits on FR-003. Its I2C
and USB transports are already unblocked, so this chain gates one transport,
not the feature.

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
| BUG-004 | `printBestEffort()` commits to partial `Serial` writes, gluing a truncated diagnostic onto the next protocol line and silently killing Heartbeat parsing | implemented (found 2026-08-29 by rigos-core session; real root cause was an undersized `snprintf` buffer at the `twaiSend()` backlog-notice call site, not `printBestEffort()` itself — see shard for the misdiagnosis-then-correction trail; verified 14/14 clean on real hardware) |
| BUG-005 | An unpowered/absent PCA9555 stalls `loop()` to ~0.1 Hz, stretching CAN heartbeat/TPDO2 far past their 1000 ms cadence so the host reads the node as flapping | open (found 2026-08-30 by a rigos-core session on real hardware, isolated DC side disconnected; measured 9450 ms heartbeat period against a 1000 ms target. Mechanism suspected but NOT confirmed — see shard. Same symptom class as BUG-003, different cause) |
