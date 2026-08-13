# License

ORC is licensed per directory, because hardware and software are separate works.

| Path | License | Full text |
|---|---|---|
| `hardware/` | CERN-OHL-W-2.0 | [LICENSES/CERN-OHL-W-2.0.txt](LICENSES/CERN-OHL-W-2.0.txt) |
| `firmware/` | Apache-2.0 | [LICENSES/Apache-2.0.txt](LICENSES/Apache-2.0.txt) |
| `tools/` | Apache-2.0 | [LICENSES/Apache-2.0.txt](LICENSES/Apache-2.0.txt) |
| `docs/` | CC-BY-4.0 | [LICENSES/CC-BY-4.0.txt](LICENSES/CC-BY-4.0.txt) |

Each of those directories also carries its own `LICENSE` file, and source files carry `SPDX-License-Identifier` headers. Where a file is not covered by any of the above (repo-root files such as this one, `README.md`, `NOTICE`, `TRADEMARKS.md`, and `DISCLAIMER.md`), CC-BY-4.0 applies.

Copyright (c) 2026 Aram Dergevorkian.

The hardware and software halves are licensed separately on purpose: under CERN-OHL's own definitions the ESP32-C3 is an Available Component, so a hardware license on the board does not reach the code running on it.

See [NOTICE](NOTICE) for the summary and third-party attribution, [TRADEMARKS.md](TRADEMARKS.md) for trademark and affiliation, and [DISCLAIMER.md](DISCLAIMER.md) for the safety and liability disclaimer that applies to anything built from this repository.
