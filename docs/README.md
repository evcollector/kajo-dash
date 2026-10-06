# Documentation index

Grouped by what the document is for. Start here rather than guessing from
filenames.

## Contracts — read before changing the thing they describe

These are binding. Code comments and `AGENTS.md` reference them by path.

| Document | Covers |
| --- | --- |
| [ui-components.md](ui-components.md) | The canonical visual and interaction system for the 320x240 UI: which constructor to use, layout rules, colour ownership, translation checklist. Required reading before editing any screen. |
| [controller-backend-interface.md](controller-backend-interface.md) | The controller boundary and the contract for adding another controller or transport, plus the rationale behind it. |
| [ride-log-format.md](ride-log-format.md) | The on-card ride file: header, record layout, availability mask, version policy. |
| [companion-protocol.md](companion-protocol.md) | The Bluetooth Link protocol the companion app speaks. |
| [ota-update-design.md](ota-update-design.md) | The signed firmware update protocol and trust model. |

## Overview

| Document | Covers |
| --- | --- |
| [architecture.md](architecture.md) | The tech stack, which tasks run on which core, how telemetry reaches the screen, and the threading rules. Start here to get oriented. |

## Features — what the firmware does

| Document | Covers |
| --- | --- |
| [ride-replay.md](ride-replay.md) | On-device replay of a recorded ride: controls, charts, summary, data handling. |
| [automatic-gauge-ranges.md](automatic-gauge-ranges.md) | How gauge ranges learn themselves, and what Manual restores. |
| [demo-mode.md](demo-mode.md) | The synthetic ride used for demos and screenshots. |

## Procedures — things you run

| Document | Covers |
| --- | --- |
| [wiring.md](wiring.md) | Connecting the CYD to a controller: the VESC UART wiring diagram, power, pins to leave alone, and the CYD pin map. |
| [development.md](development.md) | Building and flashing, the `kajo.bat` menu, preview renderer, simulator, layout editor, fonts, boot splash, tests, and building a signed release. |
| [simulator.md](simulator.md) | Running the UI on Windows: interactive simulator, state captures, the ctest suite, what the host fakes do and do not cover, and how to debug a native crash. |
| [device-smoke-test.md](device-smoke-test.md) | The physical-hardware matrix to run before tagging a firmware checkpoint. The only source of authoritative performance and hardware claims. |
| [test-senders.md](test-senders.md) | The fake VESC and FarDriver senders: flashing a spare CYD that impersonates a controller over Bluetooth. |

## History — context, not current state

Useful for understanding why something is the way it is. Do not read these as
descriptions of how the firmware behaves today.

| Document | Covers |
| --- | --- |
| [design-decisions.md](design-decisions.md) | Rationale carried over from the development history that preceded this repository. |
| [dashboard-rendering-performance.md](dashboard-rendering-performance.md) | A dated work log from the redraw optimisation of the Cyber HUD and Dual Gauge themes. |

## Reference

| Document | Covers |
| --- | --- |
| [resources.md](resources.md) | External references for the CYD board and this class of project. |

Elsewhere in the tree: [`../AGENTS.md`](../AGENTS.md) holds project-wide
conventions, [`../CONTRIBUTING.md`](../CONTRIBUTING.md) the contribution rules,
and [`../tools/firmware_update/README.md`](../tools/firmware_update/README.md)
the key generation, signing and upload commands for the Windows uploader.
