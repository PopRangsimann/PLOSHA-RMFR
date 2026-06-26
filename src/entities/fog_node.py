"""
Fog Node Entity Module
========================
Models fog computing nodes that perform adaptive micro-slot aggregation
over encrypted IIoT data using TEE enclaves.

Paper Reference:
  - System Model: F = {F_1, F_2, ..., F_n} - set of fog nodes
  - "Each fog node executes the proposed PLOSHA mechanism, which dynamically
     partitions aggregation epochs into adaptive micro-slots."
  - State vector: State_i(t) = [W_i(t), Q_i(t), L_i(t), Rel_i(t)]
"""

import random
from typing import List, Optional, Dict, Any

from src.crypto.tee import TEEEnclave


class FogNode:
    """
    Fog Node entity (F_i).

    Serves as the primary processing layer. Maintains state vectors,
    TEE enclave, aggregation buffers, and reliability tracking.
    """

    def __init__(self, node_id: int, capacity: float = 1.0):
        """
        Initialize a fog node.

        Args:
            node_id: Unique fog node identifier i.
            capacity: Base processing capacity (normalized).
        """
        self.node_id = node_id
        self.capacity = capacity
        self.operational = True

        # TEE enclave for this fog node
        self.tee = TEEEnclave(f"fog_{node_id}")

        # Neighbor fog nodes (N_i) - set during initialization
        self.neighbors: List[int] = []

        # Assigned sensors
        self.assigned_sensors: List[int] = []

        # =====================================================================
        # State Vector: State_i(t) = [W_i(t), Q_i(t), L_i(t), Rel_i(t)]
        # All normalized to [0, 1]
        # =====================================================================
        self.workload = 0.0         # W_i(t): Current workload
        self.queue_util = 0.0       # Q_i(t): Queue utilization
        self.latency = 0.0          # L_i(t): Communication latency
        self.reliability = 1.0      # Rel_i(t): Reliability score

        # Predicted state (EWMA)
        self.pred_workload = 0.0    # Ŵ_i(t+1)
        self.pred_queue = 0.0       # Q̂_i(t+1)
        self.pred_latency = 0.0     # L̂_i(t+1)
        self.pred_reliability = 1.0 # R̂el_i(t+1)

        # Prediction outputs (Phase II)
        self.cap = 1.0              # Cap_i(t+1): Effective aggregation capacity
        self.failure_exposure = 0.0 # FE_i(t): Failure exposure score
        self.risk = 0.0             # Risk_i(t): Operational risk
        self.status = "Normal"      # Status_i(t): Normal or At-Risk

        # Aggregation state (Phase III)
        self.micro_slots: List[List] = []           # D_i = {δ_1, ..., δ_{m*}}
        self.micro_aggregates: List = []            # C_{micro,k} for each slot
        self.fog_aggregate = None                    # C_{agg,i}
        self.optimal_m = 1                           # m* - optimal micro-slot number
        self.completeness = 1.0                      # V_i(t) = N_recv / N_exp
        self.incomplete_flag = 0                     # Φ_i(t): 0=complete, 1=incomplete

        # Recovery state (Phase IV)
        self.recovery_urgency = 0.0  # RU_i(t)
        self.recovery_mode = "Normal"  # Mode_i(t)
        self.recovery_success = 1     # Succ_i(t)

        # AFLTO state (Phase V)
        self.quality_score = 1.0     # Score_i(t)
        self.history_score = 1.0     # Hist_i(t)
        self.control_error = 0.0     # e_i(t)
        self.aggregation_seq = 0     # Seq_i(t): Aggregation sequence number

        # Timing accumulators for evaluation
        self.epoch_agg_time = 0.0
        self.epoch_recovery_time = 0.0
        self.epoch_comm_bytes = 0

    def get_state_vector(self) -> Dict[str, float]:
        """
        Get current state vector State_i(t) = [W_i(t), Q_i(t), L_i(t), Rel_i(t)].

        Paper: Phase II, Step 1 - Runtime State Collection
        """
        return {
            'workload': self.workload,
            'queue_util': self.queue_util,
            'latency': self.latency,
            'reliability': self.reliability
        }

    def get_prediction_vector(self) -> Dict[str, float]:
        """
        Get prediction vector Pred_i(t) = [Cap_i(t+1), FE_i(t), Risk_i(t)].

        Paper: Phase II, Step 7 - Prediction Output
        """
        return {
            'capacity': self.cap,
            'failure_exposure': self.failure_exposure,
            'risk': self.risk
        }

    def update_runtime_state(self, num_reports: int, max_capacity: int):
        """
        Update the runtime operational state based on current workload.

        Simulates dynamic workload, queue, and latency conditions.

        Args:
            num_reports: Number of sensor reports received this epoch.
            max_capacity: Maximum reports the node can handle per epoch.
        """
        if not self.operational:
            self.workload = 1.0
            self.queue_util = 1.0
            self.latency = 1.0
            return

        # Workload: proportion of capacity used
        self.workload = min(1.0, num_reports / max(1, max_capacity))

        # Queue utilization: correlated with workload + noise
        self.queue_util = min(1.0, max(0.0,
            self.workload * 0.8 + random.gauss(0.0, 0.05)))

        # Latency: increases with workload
        self.latency = min(1.0, max(0.0,
            0.1 + self.workload * 0.4 + random.gauss(0.0, 0.03)))

    def fail(self):
        """Simulate fog node failure."""
        self.operational = False
        self.workload = 1.0
        self.queue_util = 1.0
        self.latency = 1.0

    def recover(self):
        """Restore fog node to operational state."""
        self.operational = True

    def reset_epoch_metrics(self):
        """Reset per-epoch timing and communication accumulators."""
        self.epoch_agg_time = 0.0
        self.epoch_recovery_time = 0.0
        self.epoch_comm_bytes = 0

    def increment_sequence(self):
        """Increment aggregation sequence number for replay resistance."""
        self.aggregation_seq += 1

    def __repr__(self):
        return (f"FogNode(id={self.node_id}, op={self.operational}, "
                f"rel={self.reliability:.3f}, risk={self.risk:.3f})")
