# 802.11a PHY (and MAC) simulator

Software implementation of an **IEEE 802.11a OFDM PHY** in Python and C++, plus a small **CSMA/CA (DCF) MAC** simulation in Python.

The stack covers the usual TX/RX chain: scramble → convolutional FEC → puncture → interleave → constellation map → OFDM (pilots, IFFT, CP) → preamble + SIGNAL header, and the matching receive path with timing/CFO sync, channel estimation, equalization, and Viterbi decode.

```
python/     reference PHY + MAC demos
cpp/        C++ PHY port (library, app, GoogleTest)
```

## What works


| Area                                                                    | Status                          |
| ----------------------------------------------------------------------- | ------------------------------- |
| Full PHY TX/RX (BPSK/QPSK/16-QAM/64-QAM, R=1/2, 2/3, 3/4)               | Working                         |
| Ideal and AWGN loopback (Python + C++)                                  | Working                         |
| Packet sync (coarse/fine timing, CFO, LTF channel estimate)             | Working                         |
| SIGNAL field encode/decode (RATE + LENGTH)                              | Working                         |
| C++ unit tests (FEC, preamble, interleaver, pilots, modulation, SIGNAL) | Working (CI via GitHub Actions) |
| File transfer over DCF (`sim_mac_file_demo.py`)                         | Working                         |
| bladeRF over-the-air TX/RX (`bladerf_transciever.py`)                   | Experimental / hardware-only    |




## WIP / not polished

- No C++ MAC — MAC is Python-only.
- `cpp/tests/loopback_test.cpp` exists but is **not** wired into CMake yet (use the Python loopback or `wifi80211a_app` instead).
- `python/main.py` is a stub.
- Equalizer / older `transceiver` helpers are secondary to the main `tx_chain` / `rx_chain` path.
- Docs and packaging (root README was missing; Python package metadata is minimal).
- bladeRF path needs a device, manual gain tuning, and is not covered by CI.

---



## C++ — build / run / test

**Requirements:** CMake ≥ 3.16, C++23 compiler, [vcpkg](https://vcpkg.io/) (or equivalent system packages for FFTW3, Eigen3, Matplot++), Ninja recommended.

Dependencies are declared in `cpp/vcpkg.json` (`fftw3`, `eigen3`, `matplotplusplus`).

```bash
export VCPKG_ROOT=/path/to/vcpkg   # if not already set

cd cpp
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"

cmake --build build --parallel
```

**Run the demo app** (ideal loopback of a short text message):

```bash
./build/wifi80211a_app
```

**Run tests:**

```bash
cd build
ctest --output-on-failure --parallel
# or:
./wifi80211a_tests
```

CI runs the same configure → build → `ctest` flow on pushes to `main` (see `.github/workflows/`).

---



## Python — setup / run / test

**Requirements:** Python ≥ 3.13, [uv](https://github.com/astral-sh/uv) (or pip with the deps in `python/pyproject.toml`).

```bash
cd python
uv sync
```

> `pyproject.toml` pulls in the bladeRF Python bindings from Nuand’s repo. That is only needed for the hardware script; the sim demos need numpy/matplotlib (and the rest of the listed deps).

Run everything from `python/`:

```bash
# PHY: AWGN BER sweep across rates (saves ber_vs_snr.png)
uv run python loopback_test.py

# MAC: two-station DCF demo (PHY-backed AWGN by default)
uv run python sim_mac_demo.py
uv run python sim_mac_demo.py --bernoulli   # fast MAC-only path

# MAC: send a text file A→B and reassemble
uv run python sim_mac_file_demo.py
uv run python sim_mac_file_demo.py --file sample_data/sherlock_holmes_adventure_1.txt

# Hardware (needs a bladeRF; tune RX_GAIN / TX_GAIN in the script)
uv run python bladerf_transciever.py
```

There is no formal Python unit-test suite yet; the loopback and MAC demos are the practical checks.

---



## Layout (quick map)

- `python/tx_chain.py`, `rx_chain.py` — end-to-end PHY
- `python/sync_module.py`, `fec_module.py`, `ofdm_module.py`, … — PHY blocks
- `python/mac/` — frames, DCF, simulated medium, PHY SAP
- `cpp/src/`, `cpp/include/phy/` — C++ port of the PHY
- `cpp/tests/` — GoogleTest coverage for core blocks

---



## AI Use

In this project, I mainly used AI for help with understanding the protocol in the [802.11a documentation](802.11a.pdf), writing tests, demo scripts, and debugging. All tests were written by AI based on the tests given in the documentation. I have verified all the numbers to match the pdf as well. Demo scripts and some `Python` to `C++` or `C++` to `Python` translations were also created with the help of AI. With that being said, the vast majority of the source code was hand written. 