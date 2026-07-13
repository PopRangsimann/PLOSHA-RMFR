# Evaluation Graphs — Explanation and Rationale

This document explains the purpose, methodology, and key findings behind each evaluation graph produced by the PLOSHA-RMFR benchmark.

---

## Graph 1 — Ablation of PLOSHA Aggregation Architecture

**File:** `graph1_ablation_aggregation.png`  
**Type:** Grouped bar chart (PLOSHA-only, no baselines)  
**X-axis:** Number of Sensors (1000–5000)  
**Y-axis:** Aggregation Latency (ms)

### Purpose

This graph answers the question: *"Does every component of the PLOSHA hierarchical slot aggregation architecture actually contribute to reducing latency, or could a simpler design achieve the same result?"*

It isolates the contribution of each architectural layer by comparing four increasingly complete variants of the PLOSHA aggregation pipeline:

| Variant | Description |
|---------|-------------|
| **Flat-Epoch** | No hierarchical structure; all sensors report in a single flat epoch. Represents the simplest possible aggregation. |
| **Fixed-Slot** | Introduces fixed-size time slots within epochs, but without adaptive sizing. |
| **Adaptive-Slot** | Adds EWMA-based predictive slot sizing that adapts to workload patterns. |
| **Full PLOSHA** | The complete architecture with adaptive slots, micro-slot subdivision, and risk-aware scheduling. |

### Why This Matters

Ablation studies are essential for validating that a complex system's components are individually justified. Without this graph, a reviewer could argue that the multi-layer slot design is unnecessary overhead. The results demonstrate that each successive layer reduces aggregation latency, with Full PLOSHA consistently achieving the lowest latency across all sensor scales.

---

## Graph 2 — Scheduling Efficiency

**File:** `graph2_scheduling_efficiency.png`  
**Type:** Line plot (log scale on Y-axis)  
**X-axis:** Number of Fog Nodes (5–50)  
**Y-axis:** Scheduling Latency (ms)  
**Schemes compared:** PLOSHA-RMFR (Ours), Ref[22] (FedDQN), Ref[37] (FT-Workflow), Ref[38] (FT-Serverless Edge)

### Purpose

This graph evaluates how efficiently each scheme assigns incoming sensor tasks to fog nodes as the number of available fog nodes increases. Scheduling latency measures the time from when a task is ready to be dispatched until it is actually assigned to a fog node.

### Why This Matters

In real-world IIoT deployments, the number of fog nodes scales dynamically. A scheduling algorithm that becomes a bottleneck as the infrastructure grows is impractical. This graph demonstrates that:

- **PLOSHA-RMFR** maintains sub-millisecond scheduling latency across all fog node counts, scaling gracefully.
- **Ref[22] (FedDQN)** achieves the lowest raw scheduling latency due to its pre-trained DQN policy that produces instant decisions — but at the cost of no runtime adaptability.
- **Ref[38] (FT-Serverless Edge)** exhibits the worst scaling behavior, with latency increasing by orders of magnitude as fog nodes grow, reflecting the overhead of its serverless function orchestration model.

---

## Graph 3 — Failure Rate vs. Recovery Latency

**File:** `graph3_failure_rate.png`  
**Type:** Line plot  
**X-axis:** Failure Rate (2%–20%)  
**Y-axis:** Recovery Latency (ms)  
**Schemes compared:** PLOSHA-RMFR (Ours), Ref[22] (FedDQN), Ref[37] (FT-Workflow), Ref[38] (FT-Serverless Edge)

### Purpose

This graph measures how quickly each scheme recovers from fog-node failures under increasing failure rates. The failure rate represents the probability that any given fog node becomes unresponsive during an aggregation epoch.

### Why This Matters

Fault tolerance is one of the core claims of the PLOSHA-RMFR framework. This experiment directly validates the **RMFR (Risk-Aware Multi-Layer Fault Recovery)** component by showing:

- **PLOSHA-RMFR achieves near-zero recovery latency** (~0.13 ms) across all failure rates, because its micro-slot architecture means only a small fraction of work is ever at risk. Failed micro-slots are reassigned instantly without restarting the entire epoch.
- **Ref[38] (FT-Serverless Edge)** shows the worst recovery performance, with latency increasing sharply as failure rate rises — reflecting the cost of its container-based recovery model where failed functions must be cold-started on new nodes.
- **Ref[22] (FedDQN)** maintains constant but relatively high recovery latency (~2 ms), as its DQN model was not explicitly trained for failure recovery optimization.
- **Ref[37] (FT-Workflow)** performs well but cannot match PLOSHA's micro-slot-level granularity.

This is a key differentiator for PLOSHA-RMFR: while it may trade baseline aggregation speed for privacy (via TEE), it reclaims its advantage precisely when things go wrong.

---

## Graph 4 — Loss Exposure Fraction

**File:** `graph4_loss_exposure.png`  
**Type:** Line plot  
**X-axis:** Number of Micro-slots (1–20)  
**Y-axis:** Loss Exposure Fraction  
**Schemes compared:** PLOSHA-RMFR (Ours), Ref[24] (Robust IIoT), Ref[37] (FT-Workflow)  
**Excluded:** Ref[38] (FT-Serverless Edge — does not support micro-slot subdivision)

### Purpose

Loss exposure fraction measures the proportion of aggregation work that is lost when a fog-node failure occurs mid-epoch. This graph shows how increasing the number of micro-slots (finer-grained checkpointing within a time slot) reduces the amount of data at risk.

### Why This Matters

This graph demonstrates the fundamental advantage of PLOSHA's **micro-slot subdivision** design:

- **PLOSHA-RMFR's loss exposure decreases as 1/K** (where K = number of micro-slots), dropping from 10% with 1 micro-slot to below 0.5% with 20 micro-slots. This is because a failure can only destroy the current micro-slot's partial work.
- **Ref[24] (Robust IIoT)** maintains a constant ~20% loss exposure regardless of configuration, because it uses epoch-level Paillier encryption with no sub-epoch checkpointing — if a failure occurs, the entire encrypted aggregation must be restarted.
- **Ref[37] (FT-Workflow)** also shows constant ~9% loss exposure, reflecting its coarser checkpointing granularity.

This validates that micro-slot subdivision is not just an architectural nicety but a quantifiable data protection mechanism.

---

## Graph 5 — Recovery Communication Overhead

**File:** `graph5_recovery_comm.png`  
**Type:** Line plot  
**X-axis:** Incomplete Micro-slots (1–10)  
**Y-axis:** Communication Overhead (KB)  
**Schemes compared:** PLOSHA-RMFR (Ours), Ref[37] (FT-Workflow), Ref[38] (FT-Serverless Edge)

### Purpose

This graph measures the network bandwidth consumed during fault recovery as the number of incomplete (failed) micro-slots increases. It answers: *"How much extra network traffic does recovery generate?"*

### Why This Matters

Recovery is not free — every recovery operation involves re-transmitting state, reassigning tasks, and synchronizing nodes. Excessive communication overhead can saturate the network and cascade into further failures. This graph shows:

- **PLOSHA-RMFR** maintains near-constant and minimal communication overhead (~0.5–5 KB), because each micro-slot contains only a small fraction of the epoch's data. Recovering a micro-slot requires retransmitting only that micro-slot's partial aggregation.
- **Ref[38] (FT-Serverless Edge)** shows linearly increasing overhead (reaching ~400 KB at 10 incomplete micro-slots), because its serverless recovery model requires full function state migration including container images and execution context.
- **Ref[37] (FT-Workflow)** exhibits moderate overhead (~15–55 KB), reflecting its checkpoint-based recovery where larger workflow segments must be retransmitted.

This confirms that PLOSHA's localized micro-slot recovery is bandwidth-efficient, an important property for resource-constrained IIoT networks.

---

## Graph 6 — AFLTO Ablation Study

**File:** `graph6_aflto_ablation.png`  
**Type:** Grouped bar chart (PLOSHA-only, no baselines)  
**X-axis:** Metric (Aggregation Completeness, System Availability)  
**Y-axis:** Score (0–1)  
**Conditions:** AFLTO Disabled vs. AFLTO Enabled

### Purpose

This graph validates the contribution of the **AFLTO (Adaptive Feedback Loop for Timeout Optimization)** component by toggling it on and off and measuring two key reliability metrics.

### Why This Matters

AFLTO dynamically adjusts timeout thresholds based on observed system behavior. Without it, fixed timeouts may be too aggressive (causing premature failure declarations) or too lenient (wasting time on genuinely failed nodes). The results show:

| Metric | AFLTO Disabled | AFLTO Enabled | Improvement |
|--------|---------------|---------------|-------------|
| **Aggregation Completeness** | 0.882 | 0.954 | +8.2% |
| **System Availability** | 0.974 | 1.000 | +2.7% |

- **Aggregation Completeness** improves by ~8% because AFLTO prevents premature timeout-triggered failures from discarding valid partial results.
- **System Availability** reaches a perfect 1.000 with AFLTO enabled, meaning the system never experiences a period where it is unable to accept and process new tasks.

This ablation confirms that AFLTO is a meaningful component — not just architectural complexity — and should be included in the final system.
