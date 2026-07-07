# PLOSHA-RMFR Scheme — DES Implementation Plan

Build the discrete-event simulation (DES) engine for the `schemes/plosha_rmfr/` folder, implementing all 5 framework phases and producing `results.csv` for all 7 experiments.

## Current State

| Asset | Status |
|-------|--------|
| Shared crypto: [paillier.cpp](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/src/crypto/paillier.cpp) | ✅ Complete (KeyGen, Encrypt, Decrypt, Aggregate) |
| Shared crypto: [modified_ecdsa.cpp](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/src/crypto/modified_ecdsa.cpp) | ✅ Complete (KeyGen, Sign, Verify, BatchVerify) |
| Dataset: [plosha_dataset.csv](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/dataset/plosha_dataset.csv) | ✅ 10,001 rows (100 sensors × 5 fog nodes) |
| Dataset transformer: [dataset_transformer.py](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/dataset/dataset_transformer.py) | ✅ `transform_for_plosha()` ready |
| PLOSHA scheme `src/` | ❌ Empty (only `.gitkeep`) |
| Experiment folders `exp1–exp7` | ❌ Empty (only `.gitkeep`) |

## Architecture Overview

```
schemes/plosha_rmfr/
├── src/
│   ├── main.cpp              # Entry point, CLI arg parsing, experiment dispatch
│   ├── config.hpp             # All parameters, thresholds, constants
│   ├── dataset_loader.hpp/cpp # CSV parser → SensorReading structs
│   ├── des_engine.hpp/cpp     # Core DES loop (epoch-driven simulation)
│   ├── fog_node.hpp/cpp       # FogNode entity: state, queues, aggregation
│   ├── sensor.hpp/cpp         # Sensor entity: AES encryption
│   ├── ewma_predictor.hpp/cpp # Phase II: EWMA prediction + capacity/risk
│   ├── plosha.hpp/cpp         # Phase III: adaptive micro-slot aggregation
│   ├── rmfr.hpp/cpp           # Phase IV: multi-layer fault recovery
│   ├── aflto.hpp/cpp          # Phase V: AFLTO feedback + threshold optimization
│   ├── crypto_wrapper.hpp/cpp # Thin wrapper around shared Paillier/ECDSA + AES-GCM
│   ├── metrics.hpp/cpp        # Timing, latency, completeness measurement
│   └── Makefile               # Build targets
├── exp1_sensor_scalability/
│   └── results.csv
├── ...
└── exp7_aflto_ablation/
    └── results.csv
```

---

## Proposed Changes

### Component 1: Configuration & Data Types

#### [NEW] [config.hpp](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr/src/config.hpp)

Central configuration header defining all simulation parameters:

- **EWMA parameters**: α = 0.3 (smoothing coefficient)
- **Capacity weights**: ω_w = ω_q = ω_l = 1/3
- **Risk weights**: η₁ = η₂ = 0.5
- **Micro-slot limits**: m_max = 20
- **PLOSHA optimization weights**: λ₁ = λ₂ = λ₃ = 1/3, β_t (per-slot overhead)
- **RMFR thresholds** (initial): τ₁ = 0.3, τ₂ = 0.5, τ₃ = 0.7, τ_f = 0.2, τ_v = 0.8, τ_r = 0.6
- **RMFR candidate weights**: α_c = α_r = α_k = 1/3
- **Recovery urgency weights**: ρ₁ = ρ₂ = ρ₃ = 1/3
- **Reliability update**: β_r = 0.7, λ_s = λ_v = 0.5
- **AFLTO**: γ = 0.8, α_h = 0.6, κ₁ = κ₂ = κ₃ = 1/3, μ_x = 0.05
- **AFLTO scoring**: ω₁ = ω₂ = 0.5
- **Paillier key**: 2048-bit
- **AES-GCM**: 256-bit key, 12-byte IV, 16-byte tag
- **Experiment defaults**: configurable via CLI (num_sensors, num_fog, failure_rate, etc.)

---

### Component 2: Dataset Loader

#### [NEW] [dataset_loader.hpp](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr/src/dataset_loader.hpp)
#### [NEW] [dataset_loader.cpp](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr/src/dataset_loader.cpp)

Reads `plosha_dataset.csv` and produces:
- `SensorReading` structs with: sensor_id, fog_node_id, temperature, pressure, vibration, is_failure, timestamp
- Sensor-to-fog assignment map Γ
- Methods to subsample/scale for different sensor counts (Exp 1) and fog node counts (Exp 2)
- Quantize sensor values to integers in `[0, 65535]` for Paillier encryption (matching `dataset_transformer.py` logic)

---

### Component 3: Crypto Wrapper

#### [NEW] [crypto_wrapper.hpp](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr/src/crypto_wrapper.hpp)
#### [NEW] [crypto_wrapper.cpp](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr/src/crypto_wrapper.cpp)

Thin wrapper that provides:
- **AES-GCM encrypt/decrypt** via OpenSSL EVP API (sensor-side encryption, TEE-side decryption)
- Wraps the shared [Paillier](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/src/crypto/paillier.hpp) class for encrypt, decrypt, aggregate operations
- Wraps the shared [ModifiedECDSA](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/src/crypto/modified_ecdsa.hpp) for sign and batch-verify
- All operations are timed with `std::chrono::high_resolution_clock` and latencies recorded
- Helper: `TEETransform(CT_aes) → C_paillier` (AES decrypt inside "TEE" → Paillier re-encrypt)

---

### Component 4: Sensor Entity

#### [NEW] [sensor.hpp](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr/src/sensor.hpp)
#### [NEW] [sensor.cpp](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr/src/sensor.cpp)

Models an IIoT sensor:
- Holds sensor_id, assigned fog_node_id, fog-scoped AES key k_i
- `encryptReading(value) → CT_j`: AES-GCM encrypt the quantized reading
- Track active/inactive status for completeness calculations

---

### Component 5: Fog Node Entity

#### [NEW] [fog_node.hpp](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr/src/fog_node.hpp)
#### [NEW] [fog_node.cpp](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr/src/fog_node.cpp)

Models a fog node with state vector `State_i(t) = [W_i, Q_i, L_i, Rel_i]`:
- **Workload** W_i(t): normalized from queue occupancy and processing backlog
- **Queue** Q_i(t): bounded `std::queue` with real producer-consumer threading (per `fugkaew_implement_plan.md` §2.2)
- **Latency** L_i(t): simulated network latency (configurable delay parameter)
- **Reliability** Rel_i(t): initialized to 1.0, updated per recovery outcomes
- Holds a list of assigned sensor IDs
- Holds micro-slot state: `vector<MicroSlotAggregate>` containing Paillier ciphertexts
- Methods: `receiveReading()`, `processEpoch()`, `getStateVector()`

---

### Component 6: EWMA Predictor (Phase II)

#### [NEW] [ewma_predictor.hpp](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr/src/ewma_predictor.hpp)
#### [NEW] [ewma_predictor.cpp](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr/src/ewma_predictor.cpp)

Implements Phase II from the paper:
- **Step 2**: `predictState(State_i(t)) → State_hat_i(t+1)` using EWMA with α
- **Step 3**: `computeCapacity(State_hat) → Cap_i(t+1)` using weighted formula
- **Step 4**: `computeFailureExposure(State_hat) → FE_i(t)`
- **Step 5**: `computeRisk(Cap, FE) → Risk_i(t)` using η₁, η₂ weights
- **Step 6**: `classifyStatus(Risk, τ_r) → {Stable, Critical}`
- **Output**: `PredictionVector {Cap, FE, Risk}`

---

### Component 7: PLOSHA Aggregation Engine (Phase III)

#### [NEW] [plosha.hpp](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr/src/plosha.hpp)
#### [NEW] [plosha.cpp](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr/src/plosha.cpp)

Implements Phase III from the paper:
- **Step 1**: `computeOptimalMicroSlots(Pred_i) → m*` — minimizes the objective function from Eq. (16) via brute-force scan over [1, m_max]. This is a lightweight optimization (≤20 evaluations) so exhaustive search is appropriate.
- **Step 2**: `partitionEpoch(m*) → vector<MicroSlot>` — evenly divides readings into m* slots
- **Step 3**: `teeTransform(CT_j) → C_j` — AES decrypt + Paillier encrypt inside "TEE boundary" (timed)
- **Step 4**: `aggregateMicroSlot(slot_k) → C_micro,k` — Paillier homomorphic product mod n²
- **Step 5**: `aggregateFog(micro_aggregates) → C_agg,i` — hierarchical Paillier product
- **Step 6**: `assessCompleteness(N_recv, N_exp) → V_i(t), Φ_i(t)`
- **Output**: `AggregationState {C_agg, m*, V_i, Φ_i, Cap_i, Risk_i, Rel_i}`

> [!IMPORTANT]
> All crypto operations (AES, Paillier encrypt/decrypt/aggregate) are **real** — not mocked. Latencies come from actual execution of OpenSSL and GMP operations as required by the README's "Do NOT skip the simulation" rule.

---

### Component 8: RMFR Recovery Engine (Phase IV)

#### [NEW] [rmfr.hpp](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr/src/rmfr.hpp)
#### [NEW] [rmfr.cpp](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr/src/rmfr.cpp)

Implements Phase IV from the paper:
- **Step 1**: `computeRecoveryUrgency(Risk, V, Rel) → RU_i(t)`
- **Step 2**: `determineRecoveryMode(Φ, RU, Rel) → {Normal, Delegation, MicroRecovery, Failover}`
- **Step 3**: `selectRecoveryCandidate(neighbors, Cap, Rel, Risk) → F_i*` — evaluates U_j utility for each neighbor
- **Step 4**: **Delegation** — constructs DSP_i(t), serializes state, measures payload size
- **Step 5**: **MicroRecovery** — identifies D_i^miss, re-aggregates only incomplete slots (real Paillier ops)
- **Step 6**: **Failover** — constructs FSM_i(t), reassigns sensors, measures payload size
- **Step 7**: `updateReliability(Rel, Succ, V) → Rel_i(t+1)` using β_r, λ_s, λ_v

Failure injection:
- Driven by the dataset's `Is_Failure` column and configurable failure rate override (Exp 4)
- For Exp 4: randomly mark fog nodes as failed at the specified rate per epoch

---

### Component 9: AFLTO Feedback Engine (Phase V)

#### [NEW] [aflto.hpp](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr/src/aflto.hpp)
#### [NEW] [aflto.cpp](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr/src/aflto.cpp)

Implements Phase V from the paper:
- **Step 1**: `determineFinalAggregate(C_agg, C_agg_rec, Φ, RecStatus) → C_final`
- **Step 1b**: `signAndCommit(T_i) → σ_i` using ECDSA (real signature, timed)
- **Step 2**: `evaluatePerformance(V, Rel) → Score_i(t)` with ω₁, ω₂
- **Step 3**: `updateHistory(Hist, Score) → Hist_i(t+1)`, compute fused score, compute error e_i(t)
- **Step 4**: `updateThresholds(τ_x, e_i) → τ_x(t+1)` with projection Π_{[0,1]} and ordering enforcement τ₁ < τ₂ < τ₃
- **Step 5**: Generate `FeedbackState FB_i(t)` fed back to next epoch

---

### Component 10: Metrics Collector

#### [NEW] [metrics.hpp](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr/src/metrics.hpp)
#### [NEW] [metrics.cpp](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr/src/metrics.cpp)

Collects and records:
- **Aggregation latency** (ms): wall-clock time for one complete epoch (Phase II→V)
- **Recovery latency** (ms): time for RMFR execution only
- **Aggregation completeness**: V_i(t) = N_recv / N_exp
- **Loss exposure fraction**: 1/m* per micro-slot failure
- **Communication overhead** (KB): serialized payload sizes of DSP, FSM, micro-slot data
- **System availability**: fraction of epochs completed successfully
- **Queue utilization**: average Q_i(t) across fog nodes
- **Recovery frequency**: count of recovery events per epoch
- CSV writer: outputs `results.csv` in the required format

---

### Component 11: DES Engine & Main

#### [NEW] [des_engine.hpp](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr/src/des_engine.hpp)
#### [NEW] [des_engine.cpp](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr/src/des_engine.cpp)

The core simulation loop:
```
for each experiment_config:
    for each variable_value in sweep_range:
        initialize system (Phase I)
        for each epoch:
            Phase II: EWMA predict → Cap, FE, Risk
            Phase III: PLOSHA aggregate (real crypto)
            Inject failures (if applicable)
            Phase IV: RMFR recover
            Phase V: AFLTO commit + feedback
            Record metrics
        Write average metrics → results.csv row
```

#### [NEW] [main.cpp](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr/src/main.cpp)

CLI entry point:
```
./plosha_rmfr --experiment <1-7> [--sensors N] [--fog-nodes N] [--failure-rate F] [--output PATH]
```

Or run all experiments sequentially with `--experiment all`.

---

### Component 12: Build System

#### [NEW] [Makefile](file:///e:/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr/src/Makefile)

```makefile
CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall
LIBS = -lssl -lcrypto -lgmp -lpthread
CRYPTO_DIR = ../../../src/crypto

SRCS = main.cpp des_engine.cpp dataset_loader.cpp crypto_wrapper.cpp \
       sensor.cpp fog_node.cpp ewma_predictor.cpp plosha.cpp rmfr.cpp \
       aflto.cpp metrics.cpp \
       $(CRYPTO_DIR)/paillier.cpp $(CRYPTO_DIR)/modified_ecdsa.cpp

TARGET = plosha_rmfr
```

---

## Experiment Execution Details

Each experiment varies a single independent variable. The DES engine runs a sweep and writes one `results.csv`.

### Exp 1: Sensor Scalability
- **Sweep**: sensors ∈ {500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000}
- **Fixed**: fog_nodes=10, failure_rate=0%, epochs=10
- **Metric**: aggregation latency (ms)
- **How**: For each sensor count, replicate dataset rows proportionally. Measure wall-clock time for Phase III (PLOSHA) per epoch, average across epochs.

### Exp 2: Fog Node Scalability
- **Sweep**: fog_nodes ∈ {5, 10, 15, 20, 25, 30, 35, 40, 45, 50}
- **Fixed**: sensors=2000, failure_rate=0%, epochs=10
- **Metric**: aggregation latency (ms)
- **How**: Redistribute sensors round-robin across fog nodes. Measure aggregation latency.

### Exp 3: Workload Intensity
- **Sweep**: reporting_rate_multiplier ∈ {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}
- **Fixed**: sensors=1000, fog_nodes=10, failure_rate=5%, epochs=10
- **Metric**: aggregation latency (ms); secondary: queue_utilization, recovery_frequency
- **How**: Multiply sensor readings per epoch by the multiplier (replicating dataset rows). Measure all metrics.

### Exp 4: Failure Rate
- **Sweep**: failure_rate ∈ {2%, 4%, 6%, 8%, 10%, 12%, 14%, 16%, 18%, 20%}
- **Fixed**: sensors=1000, fog_nodes=10, epochs=10
- **Metric**: recovery latency (ms); secondary: aggregation_completeness, system_availability
- **How**: At each epoch, randomly mark `failure_rate × num_fog` fog nodes as failed. Measure RMFR recovery time and outcome metrics.

### Exp 5: Aggregation-Loss Exposure
- **Sweep**: micro_slots ∈ {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 16, 18, 20}
- **Fixed**: sensors=1000, fog_nodes=10, failure_rate=10%, epochs=10
- **Metric**: loss_exposure_fraction (= 1/m* for single-slot failure)
- **How**: Force m* to the sweep value (bypass optimization). Inject one micro-slot failure per epoch. Measure affected fraction.

### Exp 6: Recovery Communication Overhead
- **Sweep**: incomplete_micro_slots ∈ {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}
- **Fixed**: sensors=1000, fog_nodes=10, m*=10, epochs=10
- **Metric**: communication_overhead_KB
- **How**: Force exactly N micro-slots to be incomplete. Measure serialized DSP + FSM payload sizes.

### Exp 7: AFLTO Ablation
- **Sweep**: AFLTO_enabled ∈ {0 (disabled), 1 (enabled)}
- **Fixed**: sensors=1000, fog_nodes=10, failure_rate=10%, epochs=50 (longer to show convergence)
- **Metric**: aggregation_completeness; secondary: system_availability
- **How**: Run with static thresholds (AFLTO off) vs. adaptive thresholds (AFLTO on). Compare completeness and availability over time.

---

## Open Questions

> [!IMPORTANT]
> **Q1: Target build environment.** The README specifies Ubuntu 22.04 with Gramine for final benchmarks, but you're developing on Windows. Should I:
> - (a) Write the code to compile on **Linux only** (targeting `g++` on Ubuntu), or
> - (b) Add **cross-platform** support (MSVC/MinGW on Windows for dev, g++ on Linux for final runs)?

> [!IMPORTANT]
> **Q2: Multi-threading for queue contention.** The `fugkaew_implement_plan.md` suggests using `pthreads` for realistic queue contention (Tier 2 §2.2). Should I:
> - (a) Implement **real multi-threaded** producer-consumer queue in the DES (more realistic but complex), or
> - (b) Use a **single-threaded simulation** of queue depth (simpler, deterministic, still measures real crypto latencies)?

> [!IMPORTANT]
> **Q3: Number of simulation epochs per experiment point.** Each `variable_value` point needs enough epochs to produce stable averages. I default to **10 epochs** (Exp 1–6) and **50 epochs** (Exp 7). Should I use more/fewer?

> [!IMPORTANT]
> **Q4: Dataset scaling strategy.** The dataset has 10,000 rows with 100 sensors and 5 fog nodes. For experiments with 500–5000 sensors, I plan to:
> - Create virtual sensor IDs (S0–S4999) and assign existing readings cyclically
> - Scale fog node assignment round-robin
> 
> Is this approach acceptable, or do you prefer a different strategy?

---

## Verification Plan

### Automated Tests
1. **Unit tests** for each component:
   - `crypto_wrapper`: verify AES round-trip, Paillier encrypt-decrypt-aggregate correctness
   - `ewma_predictor`: verify EWMA convergence with known inputs
   - `plosha`: verify m* optimization returns valid range, micro-slot aggregation correctness
   - `rmfr`: verify mode selection logic matches paper's escalation rules
   - `aflto`: verify threshold update stays in [0,1] and ordering τ₁ < τ₂ < τ₃ maintained

2. **Integration test**: Run one full epoch with 100 sensors, 5 fog nodes, verify:
   - Paillier aggregate decryption matches plaintext sum
   - ECDSA signature verifies
   - `results.csv` format matches README spec

3. **Build verification**: `make clean && make` succeeds without errors

### Manual Verification
- Run each experiment and inspect `results.csv` for reasonable values
- Verify latency trends match expected behavior (e.g., latency increases with sensor count)
- Cross-check loss exposure: L_agg(m*) ≈ 1/m*
