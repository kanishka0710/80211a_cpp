# Engineering Review — 802.11a PHY (C++ / Python)

Staff-level review of the finished TX/RX implementation (sync, equalization, FEC,
OFDM working end-to-end, decoding from ~5dB SNR). This is a prioritized punch
list for what to work on next, in the same spirit as `REDESIGN_PLAN.md`.

## Priority summary


| #   | Item                                                | Category          | Effort                                           |
| --- | --------------------------------------------------- | ----------------- | ------------------------------------------------ |
| 1   | Monte Carlo statistics in the BER sweep             | Test validity     | Small                                            |
| 2   | Multipath/fading channel model                      | Realism           | Medium                                           |
| 3   | No SIGNAL/PLCP header (blind rate/length detection) | Realism           | Medium-Large                                     |
| 4   | Soft-decision Viterbi                               | DSP / coding gain | Medium                                           |
| 5   | Static vs. per-symbol-tracked equalization          | DSP correctness   | Small-Medium (decision + maybe revive dead code) |
| 6   | Test coverage (unit + regression + cross-language)  | Testing           | Medium                                           |
| 7   | Demapper efficiency / soft-LLR readiness            | Code quality      | Small                                            |
| 8   | Multithreading                                      | Performance       | Small (scoped to Monte Carlo loop only)          |
| 9   | Fixed-point                                         | Performance       | Skip unless targeting embedded/FPGA              |
| 10  | Repo hygiene carryover from `REDESIGN_PLAN.md`      | Hygiene           | Small                                            |


---

## 1. BER curves are likely not statistically valid yet

`cpp/tests/loopback_test.cpp` and `python/loopback_test.py` both run **one** trial
of 1024 bits per (config, SNR) point:

```cpp
constexpr int NUM_BITS = 1024;
const std::vector<double> SNR_RANGE = {0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30};
```

but the plots go down to `1e-5`/`1e-6` on the y-axis. Rule of thumb: you need
~100 observed bit errors before a BER estimate is trustworthy. At BER=1e-4 that's
~1M bits, not 1024 — so the high-SNR tail of every curve right now is mostly
single-trial noise (0-3 bit errors), not a real BER estimate.

**Action:**

- Run N independent trials per SNR point (different seeds) and accumulate total
bit errors / total bits compared across all of them.
- Use a stopping rule: keep adding trials until either a bit budget is spent or
~100 errors have been observed, whichever comes first.
- This loop (config × SNR × repetition) is embarrassingly parallel — it's the
right place to reach for multithreading (see #8), not the DSP chain itself.

## 2. Multipath / fading channel is a stub

```cpp
// cpp/src/channel_simulator.cpp
complexVector add_multipath(const complexVector& signal)
{
    return signal;
}
```

Nothing in either language's test harness produces frequency-selective fading —
only AWGN + integer sample delay + CFO. The equalizer's entire reason to exist
is to correct frequency-selective channels, so "decodes from 5dB+ SNR" is
currently a claim about a **flat** channel only.

**Action:**

- Implement a simple tapped-delay-line multipath model (e.g. 2-3
exponentially-decaying complex Rayleigh taps, or lift the standard 802.11
Channel Model B/D taps).
- Re-run the BER sweep with it on. Expect QAM64 R3/4 to be the first to suffer.

## 3. No SIGNAL field / PLCP header — rate & length aren't blind-decoded

`generate_tx_signal` / `receive` take `modulation` and `coding_rate` as explicit
arguments in both languages — the receiver never has to detect rate from a
BPSK-R1/2 SIGNAL symbol like real 802.11a, and there's no LENGTH field, so
packet boundaries are implicit ("demod the rest of the buffer").

This is a fine simplification for getting sync/equalization working first, but
it's the biggest remaining gap versus a complete PHY.

**Action (when ready to tackle it):**

- Add SIGNAL symbol generation on TX: BPSK, R=1/2, rate + length + parity per
spec, in its own OFDM symbol right after the LTF.
- Add blind SIGNAL decode on RX before touching the data symbols, and derive
modulation/coding-rate/length from it instead of function arguments.
- This also unlocks explicit packet-length handling, so RX only demodulates
exactly the right number of symbols instead of "the rest of the buffer."

## 4. Soft-decision Viterbi — likely the single highest-leverage DSP change

Branch metric is Hamming distance on **hard** bits:

```cpp
// cpp/src/fec_module.cpp
int branchMetric = (rx_A ^ trellis_A[s][u]) * mask_bits[2*t]
                  + (rx_B ^ trellis_B[s][u]) * mask_bits[2*t+1];
```

fed from hard constellation decisions in `map_constellation_to_bits`. Hard-decision
decoding gives up roughly 2 dB of coding gain versus soft-decision (LLR /
Euclidean-distance branch metric computed straight from the equalized symbol,
before slicing to bits).

**Action:**

- Compute per-bit soft metrics (LLR or squared Euclidean distance to
constellation points) directly from the equalized symbols.
- Feed those into the Viterbi branch metric instead of XOR'd hard bits.
- Pairs naturally with #7 (demapper rewrite) since both touch the same code path.

## 5. Dead per-symbol pilot-tracking equalizer — decide, don't just delete

`equalize_with_ltf` (used in `rx_chain.cpp`) applies **one static LTF-derived H**
to the whole packet. `perform_equalization` (unused) does per-OFDM-symbol pilot
tracking with the actual pilot LFSR — `REDESIGN_PLAN.md` currently says to delete
it as "dead/superseded."

Before deleting: for longer packets, residual CFO/phase noise will drift across
OFDM symbols, and static-H equalization will degrade toward the end of a packet.

**Action:**

- Sweep packet length (at fixed SNR/CFO) and measure where static-H BER starts
climbing.
- If it climbs within realistic packet sizes, revive per-symbol pilot tracking
instead of deleting it — the pilot subcarriers are already extracted by the
OFDM demod, so most of the plumbing exists.
- If it doesn't matter in practice for your target packet sizes, delete with
confidence (data-driven, not by decree).

## 6. Test coverage is thin relative to the DSP complexity

Currently: golden-vector FEC tests + a non-asserting BER sweep "test." Missing,
roughly in order of value added per unit effort:

- [ ] Interleaver round-trip identity test.
- [ ] Modulation mapping vs. IEEE Annex constellation tables (same pattern as
  ```
  the existing FEC golden-vector test).
  ```
- [ ] OFDM modulate → demodulate identity (no noise; should reproduce symbols
  ```
  exactly).
  ```
- [ ] Sync unit tests: known STF/LTF + known injected timing offset/CFO →
  ```
  assert recovered timing/CFO within tolerance. Highest value given sync is
  the newest, riskiest code.
  ```
- [ ] A **real** regression test: assert BER at a fixed SNR/config stays below
  ```
  a threshold, so a future refactor that quietly regresses sync or
  equalization fails CI instead of just producing a slightly worse PNG.
  ```
- [ ] Cross-language differential test: same bits/seed through both Python and
  ```
  C++, assert identical output end-to-end. Would have caught the
  `0x5B`/`0x5D` scrambler-seed drift that was only found by manual
  inspection.
  ```
- [ ] Minimal CI (e.g. GitHub Actions) running `ctest` on every push — nothing
  ```
  runs automatically today.
  ```

## 7. Demapper does brute-force search; not soft-LLR-ready

```cpp
// cpp/src/modulation_module.cpp — map_constellation_to_bits, QAM16 case
double min_distance = std::numeric_limits<double>::max();
int closest_key = 0;
for (const auto& [key, value] : qam16_map) {
    double distance = std::abs(symbol / k_mod - value);
    ...
}
```

For Gray-coded square QAM this can be done per-bit with sign/threshold tests
instead of an O(constellation size) search per symbol, and that same per-bit
structure is what naturally produces soft LLRs for #4. Not urgent on its own,
but worth doing at the same time as the soft-decision Viterbi work.

## 8. Multithreading — scope it to the Monte Carlo loop, not the DSP chain

The per-packet DSP chain (64-point FFTs, single packet, fully data-dependent
sequential stages) isn't a good multithreading target. The Monte Carlo sweep
from #1 (config × SNR × repetition) is embarrassingly parallel and is where
threading actually pays off — a simple thread pool or `std::async`/OpenMP over
trials would let you push bit counts up by 100-1000x in the same wall-clock time.

## 9. Fixed-point — skip unless targeting embedded/FPGA/ASIC

Depends entirely on the end target:

- Host-based simulator or driving an SDR (USRP/HackRF/LimeSDR) from a PC →
stay in `double`. Simpler, faster to develop, plenty fast at 20 MHz sample
rates.
- Actual FPGA/ASIC/microcontroller PHY implementation → fixed-point (Q-format,
bit-true modeling) earns its complexity, but that's a distinct project with
its own toolchain concerns.

Decide the target explicitly before investing here.

## 10. Repo hygiene carryover from `REDESIGN_PLAN.md`

Still open, cheap to close out:

- [ ] `LinkSettings` mutability vs. derived-value invariants (`changeCodingRate`
  ```
  / `changeModulationType` can silently break `NCPBS` etc.).
  ```
- [ ] `front_end_module` scope decision (drop from `wifi_phy` CMake target or
  ```
  mark experimental).
  ```
- [ ] `python/__pycache__/*.pyc` still tracked in git (currently showing as
  ```
  modified in `git status`) — add to `.gitignore` and `git rm --cached`.
  ```

---

## If you only do three things next

1. Fix Monte Carlo statistics (#1) + add a multipath channel model (#2) so the
  BER numbers mean something.
2. Add soft-decision Viterbi (#4).
3. Decide the static-vs-tracked equalization question with data (#5), not by
  decree.

Everything else here (threading, fixed-point, CI) is real but lower-stakes than
those three.