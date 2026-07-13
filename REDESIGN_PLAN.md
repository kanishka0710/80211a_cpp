# C++ Redesign Plan — 802.11a PHY

Notes from an architecture review of `cpp/` against the fixed `python/` reference implementation. Goal: redo the C++ implementation with better design, not just a 1:1 port.

## Decisions already made

| Question | Decision |
|---|---|
| Equalization strategy | **LTF-only static H**, applied to the whole packet (matches `python/rx_chain.py`'s `_equalize_with_ltf`). Not per-symbol pilot tracking. |
| Front-end / passband module (`front_end_module.*` — RRC pulse shaping, upsampling, 5.8 GHz IQ mixing) | **Dropped from scope.** No Python equivalent exists; revisit later as a separate milestone if ever. |
| Paradigm for stateless DSP stages (FEC, interleaver, modulation mapping, OFDM) | **Free functions in `namespace wifi80211a`**, not classes with all-`static` methods. Reserve real classes/objects for things that actually carry state across calls (e.g. `PilotLFSR`, future sync/framing state). |

## ⚠️ Immediate blocker — fix first

`cpp/src/fec_module.cpp`'s `precompute_trellis_()` is broken mid-refactor:

- Header (`fec_module.h`) declares it returning `std::tuple<int[64][2], int[64][2], int[64][2]>`.
- Implementation (`fec_module.cpp`) defines it `void`, builds the trellis tables as **local arrays**, then does `return trellis_A, trellis_B, next_states;` — that's the comma operator, not a tuple construction, and the arrays are about to go out of scope regardless. This won't compile, and even if it did, arrays aren't returnable by value like this.
- `viterbi_decode_()` already assumes the tuple-returning version (`auto [trellis_A, trellis_B, next_states] = precompute_trellis_();`).

Fix: make `precompute_trellis_()` actually return three `std::array<std::array<int,2>,64>` (or one struct with three members) by value — that's cheap to copy (fits in registers/stack, no heap) and avoids the old hidden-static-singleton problem for good, since it's just a pure function called explicitly instead of lazily memoized.

## Progress so far

- ✅ `fec_module` converted from a static-method class to free functions (`fec_module.h`/`.cpp`).
- ✅ `interleaver_module` converted from a static-method class to free functions.
- ✅ `pmr::vector` dropped back to plain `std::vector` in `helpers.h` (`complexVector` alias) — see rationale below.
- ⬜ `modulation_module`, `ofdm_module` still use the static-method-class shape — convert to free functions too, for consistency.
- ⬜ `transceiver.cpp` still has leftover `std::pmr::vector<int> block(...)` construction (line ~24) that's now unnecessary dead weight since everything downstream is plain `std::vector` — clean up once `modulation_module`/`ofdm_module` are converted.

## Remaining findings from the review (not yet addressed)

1. **No synchronization subsystem exists in C++.** This is the actual centerpiece of the redo — `python/sync_module.py` (coarse Schmidl & Cox timing/CFO → LTF-based fine timing/CFO → LTF channel estimate `H`) has no C++ counterpart at all. `python/preamble_module.py` (STF/LTF generation) is also missing. Everything else in this plan is prep work for building this well.
2. **`equalizer_module.h/.cpp` implements a dead/superseded algorithm.** `python/equalizer_module.py` is unused by any Python entry point (confirmed via grep) — it was replaced by the LTF-static approach in `rx_chain.py`. The C++ version is a straight port of the superseded approach. **Delete both files** rather than porting; the LTF-equalize step should be a small free function living near/called by the receive pipeline once sync exists, not a standalone "module."
3. **Cross-language constant drift:** `python/tx_chain.py` defaults `scrambler_seed=0x5D`; `cpp/src/transceiver.cpp` hardcodes `0x5B`. Nothing currently catches this kind of drift. Fix the immediate mismatch, and going forward rely on golden-vector tests (see Testing section) rather than eyeballing both languages side by side.
4. **Hidden global mutable state pattern** (the trellis singleton) — being fixed as part of the blocker above. Keep an eye out for the same "lazy static inside something that looks stateless" pattern elsewhere as you convert `modulation_module`/`ofdm_module`.
5. **`LinkSettings` mutability vs. derived values.** `changeCodingRate`/`changeModulationType` let you mutate a `LinkSettings` after construction, but derived values (`NCPBS`, etc.) depend on invariants that mutation can silently break. No call site actually needs mutation — every real usage constructs a fresh `LinkSettings`. Consider making it immutable after construction (drop the `changeX` methods) once you get to it.
6. **`front_end_module.h/.cpp`** — since it's out of scope (decision above), consider pulling it out of the `wifi_phy` CMake target entirely (or clearly marking it experimental) so it doesn't look like load-bearing code.

## Testing strategy

- Extend the existing golden-vector approach (`test_forward_error_correction.cpp`, tested against IEEE 802.11a Annex G) to the other stages as they're rewritten: interleaver (round-trip identity is an easy assertion), modulation mapping (against IEEE constellation tables), OFDM modulate/demodulate, and eventually preamble/sync (known STF/LTF + known impairment → known timing/CFO/H).
- `cpp/tests/test_ofdm_modulator.cpp` is **stale/dead** — references a renamed class (`OFDMModulator` → `OFDMModule`, header `ofdm_modulator.h` → `ofdm_module.h`) and isn't wired into `CMakeLists.txt`/CTest. Fix and wire in, or delete.
- End goal: a C++ equivalent of `loopback_test.py`'s AWGN/delay sweep, but as a **real regression test with assertions** (e.g. "BER at SNR=20dB for QPSK R1/2 must be below X"), not just a printed table.

## Suggested build order (dependency order, sync last since it's riskiest)

1. Fix `precompute_trellis_` (blocker above).
2. Finish converting `modulation_module` and `ofdm_module` to free functions; clean up `transceiver.cpp`.
3. Delete `equalizer_module.*` (both languages) and the stale `test_ofdm_modulator.cpp` (or fix + wire into CTest).
4. Fix the scrambler-seed mismatch (`0x5B` → `0x5D`, or pick one deliberately and update both languages).
5. Add golden-vector unit tests for interleaver, modulation mapping, OFDM.
6. Port `preamble_module.py` (STF/LTF generation) to C++.
7. Design and build the sync subsystem: coarse timing/CFO (Schmidl & Cox) → fine timing/CFO (LTF cross-correlation) → LTF channel estimate.
8. Implement LTF-static equalization as a small free function using the sync-derived `H`.
9. Wire it all into a `Transceiver`-level receive pipeline mirroring `rx_chain.py`'s stage order.
10. Build the AWGN/delay channel harness + BER sweep as a real regression test.

## Repo hygiene (small, do whenever convenient)

- `python/__pycache__/*.pyc` files are tracked in git — add to `.gitignore` and remove from tracking.
