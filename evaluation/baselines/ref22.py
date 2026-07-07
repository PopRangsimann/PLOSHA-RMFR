"""
Baseline: FedDQN (Ref [22])
=============================
Adaptive Task Scheduling using Federated DQN and K-Means Clustering.

Paper: Choppara & Mangalampalli, "Adaptive Task Scheduling in Fog Computing
       Using Federated DQN and K-Means Clustering", IEEE Access, 2025.

Algorithm Summary (from the paper):
  1. K-Means Clustering (K=3) classifies tasks by execution_time × deadline
     into High/Medium/Low priority buckets (Algorithm 1).
  2. Federated DQN trains per-node local Q-networks; global model aggregated
     via FedAvg θ_global = (1/N)·Σθ_i (Eq. 20).
  3. State vector s = [queue_length, cpu_requirement, priority] (Eq. 25).
  4. Action = select VM with highest Q-value: a* = argmax_a Q(s,a) (Eq. 26).
  5. Q-value update: Q(s,a) ← Q(s,a) + α[r + γ·max_a' Q(s',a') - Q(s,a)]  (Eq. 18).

Computation Cost (Table III of PLOSHA-RMFR paper):
  Prediction:   T_DQN
  Scheduling:   N_s × T_KM
  Aggregation:  T_FedAgg
  Recovery:     None

Communication Cost (Table IV):
  Data Collection:  N_s × |Data|
  Coordination:     |Model|
  Recovery:         None

Key behavioral characteristics:
  - Learning-based scheduling with DQN + K-Means (K=3, Eq. 1-9)
  - Federated model aggregation (NOT encrypted data aggregation)
  - No recovery mechanism → any failure = full epoch lost
  - No micro-slot partitioning → loss exposure = 1.0
  - DQN inference (forward pass through NN) + K-Means overhead
  - Model sync overhead |Model| grows with fog-node count
"""

import numpy as np
from evaluation.baselines.base import BaselineScheme
from src.config import SystemConfig


class Ref22Scheme(BaselineScheme):
    """FedDQN: Federated Deep Q-Network with K-Means task scheduling."""

    name = "Ref. [22]"

    # ---- Calibrated timing parameters (from Ref[22] paper) ----
    # DQN architecture: 3-input → 64-hidden → 64-hidden → N_actions
    # Forward pass ~ 0.5ms; Training step with replay buffer ~ 2ms
    T_DQN_INFERENCE = 0.0005     # DQN forward pass per scheduling decision
    T_DQN_TRAIN = 0.002          # DQN training step per episode batch

    # K-Means: K=3 clusters, features = [exec_time, deadline]
    # Convergence typically 5-10 iterations over N tasks
    T_KM_PER_SENSOR = 0.00002   # K-Means per-task assignment cost

    # Federated aggregation: θ_global = (1/N)Σθ_i (Eq. 20)
    # Model size ~50K parameters (~200KB) → serialized collection
    # from each fog node at fog-edge bandwidth (~100Mbps LAN):
    # 200KB × 8 / 100Mbps = 16ms per node upload + averaging overhead
    T_FEDAGG_BASE = 0.005        # Base federated aggregation time
    T_FEDAGG_PER_NODE = 0.02     # Per-node model collection + averaging

    # Communication sizes
    DATA_SIZE = 128              # |Data|: raw sensor data payload (bytes)
    MODEL_SIZE = 200_000         # |Model|: DQN model parameters ~200KB

    def __init__(self, config: SystemConfig = None):
        super().__init__(config)

    def aggregation_latency(self, num_sensors: int, num_fog_nodes: int,
                            failure_rate: float, workload_intensity: float = 1.0,
                            **kwargs) -> float:
        """
        FedDQN aggregation latency per epoch.

        From Table III:
          Prediction:  T_DQN (DQN inference per scheduling decision)
          Scheduling:  N_s × T_KM (K-Means clustering of all tasks)
          Aggregation: T_FedAgg (federated model aggregation round)

        In federated architecture, each fog node runs DQN + K-Means
        for its local N_s/F sensors in PARALLEL. The bottleneck is
        the per-node processing time plus FedAvg model sync overhead.

        FedAvg sync is communication-bound: each node uploads its
        local model (200KB+) to the coordinator at fog-edge bandwidth.
        """
        N_s = int(num_sensors * workload_intensity)
        sensors_per_node = N_s / max(1, num_fog_nodes)

        # Per-node parallel work:
        # DQN inference per task + K-Means assignment per task
        t_per_node = (sensors_per_node * self.T_DQN_INFERENCE +
                      sensors_per_node * self.T_KM_PER_SENSOR)

        # DQN training on local replay buffer (once per epoch per node)
        t_per_node += self.T_DQN_TRAIN

        # FedAvg model aggregation: communication-bound
        # Each node uploads model → coordinator averages → broadcasts
        # At fog-edge bandwidth (~10Mbps), 200KB model ≈ 160ms per node
        # Serial collection from all nodes (FedAvg coordinator pattern)
        t_agg = self.T_FEDAGG_BASE + num_fog_nodes * self.T_FEDAGG_PER_NODE

        # Total: parallel per-node work + serial model aggregation
        total = t_per_node + t_agg

        return total

    def recovery_latency(self, num_sensors: int, num_fog_nodes: int,
                         failure_rate: float, num_micro_slots: int = 10,
                         **kwargs) -> float:
        """
        FedDQN has NO recovery mechanism.

        From Table III: Recovery = "—" (None).
        When a fog node fails, FedDQN does not perform recovery.
        The failed tasks are simply lost. Any re-scheduling is part
        of the next epoch's normal scheduling phase, not recovery.
        """
        return 0.0

    def loss_exposure(self, num_micro_slots: int = 10, **kwargs) -> float:
        """
        FedDQN loss exposure = 1.0 (constant).

        FedDQN performs federated model aggregation, NOT encrypted data
        aggregation. There is no micro-slot partitioning. A single
        failure affects the entire processing window.

        From the paper (Sec V, Exp 5): "Ref. [22] does not appear in
        this experiment" — it is excluded because it doesn't perform
        data aggregation. We assign L=1.0 for comparison purposes.
        """
        return 1.0

    def comm_overhead_kb(self, num_sensors: int, num_fog_nodes: int,
                         failure_rate: float, num_micro_slots: int = 10,
                         **kwargs) -> float:
        """
        FedDQN communication overhead.

        From Table IV:
          Data Collection:  N_s × |Data|
          Coordination:     |Model| (DQN model sync per round)
          Recovery:         None
        """
        # Data collection: each sensor sends raw data to fog
        data_collection = num_sensors * self.DATA_SIZE

        # Coordination: federated model sync (each node uploads model)
        model_sync = num_fog_nodes * self.MODEL_SIZE

        total_bytes = data_collection + model_sync
        return total_bytes / 1024.0  # Convert to KB

    def completeness(self, num_sensors: int, num_fog_nodes: int,
                     failure_rate: float, **kwargs) -> float:
        """
        Aggregation completeness for FedDQN.

        Without recovery, all data from failed nodes is lost.
        Completeness = 1 - failure_rate.
        FedDQN performs federated MODEL aggregation, not data recovery.
        """
        return max(0.0, 1.0 - failure_rate)

    def availability(self, num_sensors: int, num_fog_nodes: int,
                     failure_rate: float, **kwargs) -> float:
        """
        System availability for FedDQN.

        No fault recovery mechanism. Epochs with failed nodes
        lose that partition's data entirely.
        """
        return max(0.0, 1.0 - failure_rate)
