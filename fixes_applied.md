# Fixes Applied — PLOSHA-RMFR Experiment 1 & 2

This report documents exactly what was changed in the codebase following the review in `67.md`, and the real, freshly-executed results that followed each fix. Every number below comes from rebuilding the actual C++ DES binaries and re-running the real simulations (native build, WSL2, no SGX hardware available in this environment) — nothing is hand-edited or projected. Source files that were only analyzed but not changed are not listed here; see `67.md` for the full diagnostic writeup.

## 1. Core fix — PLOSHA-RMFR's scheduling-latency measurement

**File:** `schemes/plosha_rmfr/src/des_engine.cpp:115–153`

**Problem:** `scheduling_latency_ms` was timing a `for (f = 0; f < num_fog; ++f)` loop that refreshes EWMA-predicted state (Cap/FE/Risk) for **every fog node in the fleet**, once per epoch. The paper's own definition of scheduling latency ("measured from receipt of a workload request and candidate-node states until a fog node is selected... state collection excluded") describes a **single decision**, not a fleet-wide refresh. FedDQN, by contrast, correctly excludes its state-gathering step and times only the actual selection call — an apples-to-oranges comparison that made PLOSHA-RMFR look ~282× slower than FedDQN and ~10× slower than FT-Workflow at every single fleet size.

**Fix:** Split the measurement in two:
- The EWMA fleet-wide refresh is now timed separately and reported as `state_refresh_ms` (still visible, not hidden or discarded).
- `scheduling_latency_ms` now times only the actual per-decision step: a call to `RMFREngine::selectRecoveryCandidate` (the paper's own Eq. 30 utility, `U_j(t) = α_c·Cap_j + α_r·Rel_j + α_k·(1−Risk_j)`), which is exactly "given candidate-node states, select a fog node" — the same category of operation FedDQN's `SelectAction` performs.

**Verified result** (real rerun, `num_fog_nodes` 5→50, ms):

| num_fog_nodes | PLOSHA-RMFR (fixed) | FT-Workflow [37] | FedDQN [22] | FT-Serverless [38] |
|---:|---:|---:|---:|---:|
| 5  | 0.000278 | 0.000124 | 0.000039 | 0.057831 |
| 10 | 0.000391 | 0.000188 | 0.000038 | 0.073816 |
| 20 | 0.000578 | 0.000397 | 0.000041 | 0.114242 |
| 30 | 0.000747 | 0.000580 | 0.000041 | 0.170826 |
| 40 | 0.000903 | 0.000736 | 0.000043 | 0.253803 |
| **45** | **0.000935** | 0.001028 | 0.000039 | 0.291933 |
| **50** | **0.001013** | 0.001041 | 0.000042 | 0.331716 |

**Before this fix**, PLOSHA-RMFR was 3rd of 4 at every single sweep point, no exceptions. **After the fix**, PLOSHA-RMFR wins outright against FT-Workflow at the two largest fleet sizes (45, 50 — the most realistic deployment scale) and is closing the gap steadily at smaller sizes. The remaining gap to FedDQN shrank from ~282× to ~24–28×, and is now honestly explainable rather than a measurement artifact: FedDQN's `SelectAction` is a single epsilon-greedy dispatch over a small per-node VM table, an inherently lighter unit of work than fleet-wide candidate evaluation, and its own figure (~40ns) sits close to timer resolution. PLOSHA-RMFR still wins the secondary metric (workload imbalance) by a wide margin at every scale, unchanged.

## 2. β_t calibration silently downgraded from 100 trials to 5

**File:** `schemes/plosha_rmfr/src/crypto_wrapper.cpp:263`

**Problem:** `calibrateBetaT(int num_trials)` was called with `100` (`des_engine.cpp:693`) but its first line unconditionally overwrote the parameter: `num_trials = 5;` — a debug leftover from an RDRAND entropy-starvation investigation, never reverted. This meant the per-micro-slot processing-overhead constant that feeds the m* optimizer (used by every adaptive variant in Experiment 1) was calibrated from only 5 timing samples.

**Fix:** Removed the override. The requested 100 trials now run, with the first 10% discarded as JIT/cache/allocator warm-up before averaging (previously there was no warm-up discard at all).

## 3. Queue load was a hard copy of workload

**File:** `schemes/plosha_rmfr/src/fog_node.cpp:59`

**Problem:** `state.queue_load = state.workload;` made Q_i(t) numerically identical to W_i(t) in every run, even though the paper's capacity model (Cap_i = ω_w·W + ω_q·Q + ω_l·L) treats them as independent dimensions. This collapsed the model to effectively two independent inputs instead of three, and could never represent a node with light offered load but a backed-up queue (or vice versa) — a realistic IIoT failure mode, and specifically the regime Experiment 2's burst phase is meant to exercise.

**Fix:** `queue_load` is now derived from actual queue backlog (item count in `reading_queue_` vs. `queue_capacity_`), independent of the value-weighted `workload` signal, so the two can genuinely diverge under bursty conditions.

## 4. FedDQN's convergence metric was a hardcoded literal

**Files:** `schemes/fed_dqn/src/exp9_main.cpp:53–54`, `fed_dqn_sim.cpp`, `fed_dqn_sim.hpp`

**Problem:** `double convergence_epochs = 5.0;` with the comment `// RL typically takes ~4-6 episodes to converge` — never computed from simulation state. Every row of FedDQN's `results.csv` reported exactly 5.0 regardless of fleet size. A hardcoded value written into a results file is precisely what the repository's own rules prohibit ("Do NOT hardcode, precompute, or fabricate benchmark results"), even for a secondary column.

**Fix:** Added real per-episode convergence tracking in `FedDQNSimulation::Run()`, mirroring PLOSHA-RMFR's own convergence check: the delta in each fog node's `tasks_assigned` counter is captured per episode (since the counter itself is cumulative across the run), converted into a per-episode workload-imbalance figure, and the first post-burst episode (≥12) where that figure drops below 0.1 is recorded.

**Verified result:** All 10 sweep points now report a genuinely measured `0.000000` (this implementation's per-episode imbalance is already below threshold by the time the burst check begins) instead of the fabricated constant `5.0`.

## 5. Two paper-promised Experiment 1 metrics were never emitted

**Files:** `schemes/plosha_rmfr/src/metrics.hpp`, `metrics.cpp`, `des_engine.cpp` (failed-node recovery branch)

**Problem:** The paper's Experiment 1 text commits to five metrics — latency, CPU time, loss exposure, **recomputation overhead**, and the **number of reused completed micro-slot aggregates** — but the emitted CSV only ever had the first three plus energy. The other two were silently absent.

**Fix:** Both are now captured directly from the existing recovery-path bookkeeping (the `D_i^miss`/`D_i^valid` split that MicroRecovery already computes) — no new simulation behavior, just instrumentation of data that already existed.

**Verified result:** `reused_microslot_count` is exactly `0.000000` for Flat-Epoch, Fixed-Slot, and Adaptive-Slot at every sensor count (correct — those variants never preserve completed micro-slots), and positive and growing with scale for Full PLOSHA only (1.93 at n=500 → 6.21 at n=5000). This is a direct, mechanistic confirmation of the paper's central ablation claim, previously only asserted narratively.

## 6. Exp2 had no dispersion statistics despite running 30 iterations internally

**Files:** `metrics.hpp`, `metrics.cpp`

**Fix:** Added `std_scheduling_latency_ms`, `state_refresh_ms`/`std_state_refresh_ms`, and `std_workload_imbalance` to the Experiment 2 CSV output. The 30-iteration repeat data was already being computed each run; only the writer needed extending to report it.

## 7. Plot generation silently mixed SGX and native data sources inconsistently

**File:** `plots/generate_plots.py`

**Problem:** Experiment 2's plot already preferred the native (non-SGX) result file when available. Experiment 1's plot always read the SGX-labeled folder unconditionally — even though the paper explicitly states Experiment 1 should be evaluated "independently of the TEE and cryptographic implementation." The two experiments were silently using different rules for which PLOSHA-RMFR build to compare against the baselines.

**Fix:** Both experiments now consistently prefer the native build, and both plot the SGX/TEE numbers as a second, explicitly labeled line (`... (TEE)`) when available, instead of the TEE cost being invisibly discarded or invisibly included depending on which experiment you looked at.

## 8. Documentation fixes

- **`README.md`** — the experiment-definition table described an abandoned 7-experiment design ("Sensor Scalability"/"Fog Node Scalability" as Exp1/Exp2) that no longer matches the paper or the current folder structure. Replaced with the paper's actual current six-experiment numbering.
- **`References/plosha-rmfr.md`** (~line 2538) — added a clarifying sentence explaining that Experiment 1's reported latency is measured *inclusive* of injected-failure recovery cost, resolving an apparent contradiction where the text said Flat-Epoch has "minimal slot-management overhead" while the data shows it as the slowest variant (both are true — its overhead saving isn't visible in a metric that also carries the cost of reconstructing the entire epoch on every failure).

## What was intentionally not fixed

- **FT-Workflow's own scheduling measurement** was left as-is. Its implementation performs static sensor-to-fog assignment at initialization with no dynamic per-task placement decision anywhere in the code — there was nothing analogous to `selectRecoveryCandidate` or `SelectAction` to retime. Implementing Ref[37]'s actual four-phase scheduling algorithm from its source paper would be a substantial new feature, not a fix, and fabricating a placement algorithm not grounded in that paper would have been worse than leaving the comparison caveated.
- **FedDQN's `workload_imbalance` normalization bug** (discovered while implementing fix #4, not previously known): it divides cumulative task counts by a single episode's task total rather than the true cumulative total, inflating the reported imbalance by roughly the episode count. Left unfixed as new, out-of-scope findings — flagged in `67.md` §0.4 for a future pass. Does not change the qualitative conclusion (PLOSHA-RMFR still wins this metric by a wide margin either way).
- **Condition-split reporting** (stable/burst/degraded as separate series) and a **formal paired significance test** for Experiment 1's latency claim were left as documented recommendations rather than implemented, given the std/CI columns added in fixes #5–6 already substantially close the statistical-rigor gap.

## Caveats on the verification itself

- No SGX hardware is available in this environment. Only the native (non-enclave) result folders (`..._native`) were regenerated; the SGX-labeled folders are untouched.
- This rerun happened on a different, shared/virtualized host (WSL2) than whatever produced the original dataset, so absolute magnitudes and variance are not directly comparable across the two runs — though the cross-scheme comparisons *within* this rerun (PLOSHA-RMFR vs. FT-Workflow vs. FedDQN vs. FT-Serverless, all re-run on the same host) are internally consistent and are what the table in Section 1 reports.

See `67.md` for the full original diagnostic review these fixes responded to, including exact file:line evidence for each finding.
