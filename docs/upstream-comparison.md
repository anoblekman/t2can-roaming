# Comparison with Similar Projects / DBCs (2026-09-14)

A record of comparing T2CAN-ROAMING (this repository, Model 3 Highland HW4, LilyGo
T-2CAN) against other public Tesla CAN projects/DBCs. The goals are (1) cross-check
whether our implementation is correct, (2) separate what is worth borrowing from what
to avoid, and (3) establish authoritative sources for frame/bit definitions.

> Method: each repository was read down to the source, and frame IDs / bits are noted
> only where confirmed directly in code/DBC. README claims are kept distinct from
> code-verified facts. Frame values from other platforms (pre-Highland / single-bus)
> are treated as **unverified** for our car.

---

## Summary: our implementation is verified, and there is nothing to borrow right now

- **Nag (0x370 echo), Summon/EU (0x3FD bit19/47), and checksum** agree across three
  independent sources (opendbc, the 06066 repository, and us). In particular, opendbc
  independently verifies our HW4/Party choice.
- The "0x370 counter + checksum recompute" that would have been worth stealing is
  **something we already have exactly right** (`logic/fsd_nag.c` + `logic/fsd_checksum.h`).
- Both modification repositories have **weaker safety gating than ours** (no autopark
  state block, not listen-by-default, no speed gate). This reconfirms that our design
  was right.

---

## 1. commaai/opendbc — most important (HW4 / Party / verification)

- **What it is:** CAN DBC + Python vehicle-control API + C safety firmware. MIT,
  3,400+ stars, actively maintained.
- **Coverage:** `docs/CARS.md` officially supports **Model 3 (HW4) 2024-25 / Model Y
  (HW4) 2023-25**. `opendbc/dbc/tesla_model3_party.dbc` = **the Party bus (our CAN A)
  DBC**. <- the only same-generation, same-bus source.

### Items that independently verify our choices
| What we use | opendbc | Result |
|---|---|---|
| 0x39B `DAS_status`, `DAS_autopilotState` byte0[3:0] | `BO_ 923 DAS_status`, `DAS_autopilotState : 0\|4` | **match** |
| 0x370 nag frame | `BO_ 880 EPAS3S_sysStatus` | **match** |
| 0x370 counter = low nibble of byte6 | `get_counter`: `0x370 -> data[6] & 0x0F` | **match** |
| checksum `(id&0xFF)+(id>>8)+Σdata` | `tesla_compute_checksum` same formula | **exact match at source level** |

The checksum in opendbc `safety/modes/tesla.h` (verbatim source):
```c
chksum = (uint8_t)((msg->addr & 0xFFU) + ((msg->addr >> 8) & 0xFFU));
for (int i = 0; i < len; i++) if (i != checksum_byte) chksum += msg->data[i];
```
-> equivalent to our `logic/fsd_checksum.h` `tesla_additive_checksum` (we exclude the
checksum byte from the summation range from the start, so the result is identical).

### Autopark latch — the off-the-shelf answer for our FSD-era gate design
opendbc guards against the **same danger** as the autopark incident we experienced,
like this (`tesla_rx_hook`, `0x286` DI_state):
```c
int autopark_state = (msg->data[3] >> 1) & 0x0FU;      // DI_autoparkState
bool now = (autopark_state == 3) || (autopark_state == 4) || (autopark_state == 9);
if (now && !prev && !cruise_engaged_prev) tesla_autopark = true;   // rising-edge latch
if (!now) tesla_autopark = false;
// tx_hook:  if (tesla_autopark) violation = true;   // block all TX during autopark
// init:     tesla_autopark = true;                  // "assume" autopark at boot (fail-safe)
```
- Values 3=ACTIVE, 4=COMPLETE, 9=SELFPARK_STARTED.
- It **starts with autopark=true at boot**. Comment: "DI_state is low-frequency, so
  assume it to avoid faulting if we start up mid-autopark." In other words, **if
  unknown, block**.
- Top of the file: "Autopark not setting Autopark state properly, Only Summon is
  currently supported" — even comma does not trust the autopark state and blocks
  conservatively.
- **Our application:** currently we detect autopark via 0x39B `autopark_ready` (->
  block AP6). opendbc's `DI_autoparkState` (0x286) is a more direct signal and a
  candidate to replace/reinforce it. **But whether 0x286 arrives on our buses (2/3,
  13/14) is unconfirmed** — verify with a listen trace before applying. (0x118/0x257
  do arrive on our buses; 0x286 needs separate confirmation.) See `fsd_das.c`.

### Gating design worth referencing
opendbc `tesla.h` applies a TX allow-list + per-frame counter/checksum validation +
DI vs ESP **speed cross-check** (`speed_mismatch_check`) + angular-rate limits +
rising-edge latches for stock LKAS/AEB. The best reference source when we harden our
gate.

**Verdict:** on any frame-ID / bit conflict, treat **opendbc HW4 Party DBC + our
real-car trace as authoritative**.

---

## 2. 06066060606060/T2CAN-Nag-killer-EU-unlock — direct sibling (Model Y)

- **What it is:** the same LilyGo T-2CAN as us, the same two features. ESP32-S3 dual
  CAN 500kbps. CAN A (MCP2515) -> Party -> nag, CAN B (TWAI) -> Chassis -> Summon/EU.
  **No license (all rights reserved)**. Moved to V3, so this tree (V2.6.0) is frozen.
- **Coverage:** **Model Y** (AP >= 2026.20). Not Highland Model 3 -> frame values
  treated as unverified.

### Same as us
- **0x370 echo + rolling counter (low nibble of byte6 +1) + additive checksum
  (magic 0x73 = 0x70+0x03).** Same method as our `assemble()` (`logic/fsd_nag.c`).
- **0x3FD mux1, bit19=0 + bit47=1.** **Same bits and polarity** as our
  `fsd_apctl_build()`.
- 0x3F8 `selfParkRequest = data[3]>>4`, ACA = 0x118 `data[6]&0x04` — same as us.

### Different from us (we are the safer side)
| Item | 06066 | Us |
|---|---|---|
| AP state frame | **0x399**, `&0x07` (3 bits) | **0x39B**, byte0[3:0] (4 bits) |
| Nag-allowed states | 3/4/5/**6** (allows 6) | 3/4/5, **6 blocked**, 8/9/14/15 blocked |
| Distinguish fault states 8-15 | impossible (3 bits) | possible (4 bits) |
| Stabilization debounce | none (only boot 15s) | 1000ms |
| Default TX posture | **enabled at boot** | **listen-only (hardware)** |
| Speed gate | none | reads 0x257 |
| EU injection while driving | keeps injecting during AP driving via `forceMode` | scoped to Summon context |

- **State-6 injection is the key danger:** on Highland, 6 = autopark -> this
  repository allows, on the nag path, exactly the incident path we already fixed.
  (Ironically, this repository's ULC path excludes 6 with 4 bits, contradicting
  itself.)

### Only in 06066 (high-risk, not adopted)
- **TLSSC** (0x3FD mux0 bit38/39, stop-sign / green-light control), **TLSSC Restore**
  (0x331 -> 0x1B) — the README warns "using this on a non-banned car triggers an
  immediate account ban." Do not touch.
- **ULC blind spot** (0x3F8 bit52-53) — different bits from the 0x3F8 bits we target
  (bit1 etc.), but useful as an observation reference for a real 0x3F8 use example.

### What to borrow
- Already have it all (counter+checksum, polarity). No new code to borrow.
- Concept reference: `isOurs` self-echo suppression, 500ms-period 0x3FD
  retransmission, MCP EFLG overflow / bus-off recovery — we have similar logic, so
  for comparison only.

---

## 3. ColinM-sys/tesla-can-boost — different lineage (low relevance)

- **What it is:** manipulates **acceleration / drive mode** over a single bus via
  ELM327 (OBDLink MX+). CC BY-NC (README says MIT, contradiction). 9 stars, a
  1-week AI experiment, abandoned ~5 months.
- **Coverage:** 2019 Model 3 SR+ **HW3**. A generation and bus unrelated to us.
- **Zero frames overlapping with us** — none of 0x370/0x3FD/0x39B/0x399/0x3F8 present.
- **Characteristics (mostly unverified / bluster):** 0x334 drive mode (the key byte
  value differs between files), "640kW/10300Nm" (not encoded in code), gamepad torque
  injection (0x1D8, no gate at all), "drift / TC off" (labels only, with no CAN TX).
  **The additive checksum may not be the real Tesla CRC** -> "confirmed working" means
  the ELM327 received ASCII, not that the car responded.
- **Verdict:** a cautionary example. The only usable idea is auto-search of the
  checksum offset for unknown frames (for hypothesis generation). Do not trust its
  frame values for our HW4.

---

## 4. Three reference DBCs (authoritative frame/bit sources)

Derived from the ColinM README `## Related Projects`.

| DBC | What it is | For us |
|---|---|---|
| **joshwardell/model3dbc** | community-canonical DBC, MIT, 406 stars, stopped 2023 | **The authoritative source that fixes the names 0x3FD bit19 (`UI_applyEceR79`) / bit47 (`UI_hardCoreSummon`).** Has the full 0x3F8 bitmap (ulcStalkConfirm bit1 etc.). pre-Highland / Chassis, so re-confirm frame IDs. Puts `DAS_status` on **0x399** (caution) |
| **onyx-m2/onyx-m2-dbc** | superset of josh's, **no license**, 40 stars | Largest signal-name dictionary. For cross-checking 0x3FD bits. Maps 0x370 to `SCS_alertMatrix2` (a different frame) — do not trust |
| **commaai/opendbc** | #1 above | HW4 Party canonical. Top priority |

### * Version split (era split) — the key lesson of this analysis
- josh / onyx (the popular community DBCs) put `DAS_status` on **0x399 / Chassis** and
  0x370 on a different frame.
- Only opendbc (HW4 Party) puts `DAS_status` on **0x39B** and 0x370 on
  `EPAS3S_sysStatus` = **our values**.
- **If we had trusted a popular DBC as authoritative, we would have gated on the wrong
  frame.** Our confirming 0x39B via a real-car trace and identifying flipper's byte1
  misread was more correct than the conventional wisdom.
- **Rule: reference community DBC bit layouts, but fix HW4 frame IDs only via the
  opendbc HW4 Party DBC + our real-car trace.**

---

## Follow-up candidates (not executed)
- **Confirm 0x286 (DI_state) visibility on our buses** -> if it arrives, implement the
  FSD-era autopark gate more directly with an opendbc-style `DI_autoparkState` latch
  (replace/reinforce the current 0x39B autopark_ready).
- **Observe 0x3F8** (planned) and compare against the josh/onyx bitmaps to confirm our
  car's fixed values.
- opendbc's speed cross-check (DI vs ESP) pattern is a reference against our using a
  single speed source (0x257) only.
