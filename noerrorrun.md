# System Requirements — Error-Free Run

Requirements for building and running the PLOSHA-RMFR experiment suite without
errors. Every item below was verified against this repository's Makefiles,
sources, and plotting scripts.

---

## 1. Platform

| | Requirement |
|---|---|
| OS | Linux x86-64 — Ubuntu 22.04 LTS or newer |
| Architecture | **x86-64** |
| Shell | `bash` (scripts use `set -e`, `[ ]`, `mkdir -p`) |

Ubuntu 22.04 (GCC 11, OpenSSL 3.0) and 26.04 (current GCC, OpenSSL 3.x) both
satisfy every requirement below; the sources need only C++17. Confirm what the
host actually runs rather than assuming the AMI you selected — verify with:

```bash
lsb_release -d && uname -m && g++ --version
```

On a newer toolchain the `-Wall -Wextra` flags may emit additional warnings.
These are warnings, not build failures.

macOS is usable for local development, but see §6 — **a binary built on macOS
cannot be committed and run on the Linux benchmark host.**

Windows is **not** a supported run target. `metrics.cpp` shells out to
`mkdir -p` via `system()` when creating output directories, and the run scripts
are POSIX shell. Use WSL2 or a Linux host.

### Recommended hardware

Driven by the largest sweep point in Experiment 1 (`5000` sensors,
`num_fog_nodes = sensors/100`, `failure_rate = 0.50`, 30 epochs), which performs
Paillier `teeTransform` + aggregation over every reading in each incomplete
micro-slot during recovery.

| | Minimum | Recommended |
|---|---|---|
| vCPU | 4 | 8 |
| RAM | 8 GB | 16 GB |
| Free disk | 2 GB | 5 GB |

---

## 2. Build toolchain

All five scheme Makefiles require:

- `g++` supporting **`-std=c++17`**
- `make`
- `pthread` support (`-pthread` / `-lpthread`)

```bash
sudo apt-get update
sudo apt-get install -y build-essential
```

Compiler flags in use: `-std=c++17 -O2 -Wall -Wextra -pthread`.

---

## 3. Runtime / link libraries

**OpenSSL development headers are mandatory.** Every scheme links `-lssl
-lcrypto`; the Paillier and modified-ECDSA implementations under `src/crypto/`
depend on them.

```bash
sudo apt-get install -y libssl-dev
```

Missing this produces link-time errors such as:

```
/usr/bin/ld: cannot find -lssl
/usr/bin/ld: cannot find -lcrypto
```

The `robust_iiot` scheme links `-lssl -lcrypto` only; the other four also link
`-lpthread`.

---

## 4. Plotting dependencies

`plots/generate_plots.py` imports `pandas`, `matplotlib` (`pyplot`, `ticker`),
`pathlib`, and `os`. There is no `requirements.txt` in the repository.

```bash
sudo apt-get install -y python3 python3-pip
pip3 install pandas matplotlib
```

Plot generation is independent of the C++ build — it consumes the `results.csv`
files. Matplotlib needs no display; it writes PNGs to `plots/output/`.

---

## 5. Required input data

```
dataset/plosha_dataset.csv     ~1.6 MB
```

Expected header:

```
Timestamp,Sensor_ID,Fog_Node_ID,Temperature,Pressure,Vibration,Is_Failure,
Protocol,Connection_Duration,Flow_Rate,Bytes_Transferred,Packet_Size,
Device_Role,Source_IP,Dest_IP,Label
```

The run scripts resolve this as `$(pwd)/dataset/plosha_dataset.csv`, so **they
must be invoked from the repository root**, not from inside `schemes/`.

---

## 6. Binaries are not distributed — build from source

Compiled executables are `.gitignore`d and must be rebuilt on the target host.

This is a hard requirement, not a preference. A macOS `arm64` build of
`plosha_rmfr` was previously committed to the repository; on a Linux x86-64 host
it fails because the file is a Mach-O image, not an ELF one. Verify before
running:

```bash
file schemes/plosha_rmfr/src/plosha_rmfr
# required: ELF 64-bit LSB ... x86-64 ... for GNU/Linux
# wrong:    Mach-O 64-bit arm64 executable
```

---

## 7. Line endings

Shell scripts must use **LF**. `.gitattributes` enforces this with
`*.sh text eol=lf`.

A script saved with CRLF fails on Linux with errors like:

```
run_exp1.sh: line 2: set: -: invalid option
run_exp1.sh: line 3: $'\r': command not found
cd: $'/path\r/schemes/...': No such file or directory
```

Repair an affected script with `sed -i 's/\r$//' <script>.sh`.

---

## 8. Verifying the environment

```bash
g++ --version                       # C++17-capable
make --version
echo '#include <openssl/bn.h>
int main(){return 0;}' > /tmp/t.c && gcc /tmp/t.c -lcrypto -o /tmp/t && echo "OpenSSL OK"
python3 -c "import pandas, matplotlib; print('plotting OK')"
ls -l dataset/plosha_dataset.csv
```

---

## 9. Running

From the repository root:

```bash
chmod +x run_exp1.sh
./run_exp1.sh                       # builds, then runs Experiment 1 (ablation)
```

The binary's own interface:

```
plosha_rmfr --experiment <1-9|all> [options]
  --experiment <N|all>    experiment number, or 'all'
  --epochs <N>
  --dataset <path>
  --output <dir>
  --sensors <N>
  --fog-nodes <N>
  --failure-rate <f>
  --help
```

`run_exp1.sh` invokes `--experiment 8 --epochs 30`, which is the aggregation
ablation reported as Experiment 1.

---

## 10. Known failure modes

| Symptom | Cause | Fix |
|---|---|---|
| `make: command not found` | no build toolchain | `apt-get install build-essential` (§2) |
| `cannot find -lssl` / `-lcrypto` | OpenSSL headers absent | `apt-get install libssl-dev` (§3) |
| `cannot execute binary file` | macOS/arm64 binary on Linux | rebuild from source (§6) |
| `$'\r': command not found` | CRLF line endings | `sed -i 's/\r$//'` (§7) |
| `Cannot open .../results.csv` | run from wrong directory | run from repository root (§5) |
| `ModuleNotFoundError: pandas` | plotting deps absent | `pip3 install pandas matplotlib` (§4) |

---

## 11. Reproducibility notes

- All experiments summarise epochs with the **same estimator**: mean with
  population standard deviation, computed in
  `MetricsCollector::computeAverages`. The `std_*` columns in every
  `results.csv` are standard deviations about the reported mean.
- Per-epoch records are **not** persisted; `metrics_.reset()` clears them after
  each sweep point. Only aggregated `results.csv` files survive a run, so the
  underlying epoch distribution cannot be audited after the fact.
- Experiment 1 runs at `failure_rate = 0.50`, outside the `0.02`–`0.35` range
  swept in the failure-rate experiment. This is intentional (it amplifies
  recovery-cost differences between variants) and should be stated explicitly
  wherever Experiment 1 results are reported.
