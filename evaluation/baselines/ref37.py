"""
Baseline: HFWPF - Hybrid Fault-Tolerant Workflow Scheduling (Ref [37])
========================================================================
A Hybrid Fault-Tolerant Workflow Scheduling With Performance Fluctuated
Cloud Resources.

Paper: Ren & Yao, "A Hybrid Fault-Tolerant Workflow Scheduling With
       Performance Fluctuated Cloud Resources", IEEE Trans. Services
       Computing, 2026.

Algorithm Summary (from the paper):
  1. Preprocessing (Algorithm 1, Sec IV-A):
     - Divide workflow deadline d_i into sub-deadlines for each task
     - Time complexity: O(J²) per workflow  (Eq. described in Sec IV-A)

  2. Initial Scheduling (Algorithm 2, Sec IV-B):
     - For each ready task t_ij, select fault-tolerant strategy:
       * Resubmission (z_ij=1) if:  dl_ij - st_ij ≥ 2×(T_boot + max TT + ψ)  (Eq. 8)
       * Replication (z_ij=0) otherwise
     - Allocate to VMs using pst/pft calculations (Eq. 10-12)
     - Time complexity: O((J + N_in) × M × N_M) per workflow

  3. Online Scheduling (Algorithm 4, Sec IV-C):
     - On VM failure with resubmission: find new VM for re-execution
     - On VM failure with replication: redundant copy already running
     - Time complexity: O((J + N_in) × M × N_M)

  4. Online Adjustment (Algorithm 5, Sec IV-C):
     - Adjust sub-deadlines of unexecuted tasks based on actual finish times
     - Promote replication → resubmission when time permits
     - st_ik = max_{t_ij ∈ P(t_ik)} (aft(t_ij) + TT_jk)  (Eq. 13)
     - Time complexity: O(J × N_in)

  5. Elastic Resource Provisioning:
     - Activate new VMs when needed
     - Shut down idle VMs to reduce failure probability
     - Approximate weight for stochastic execution time:
       w(X) = E(X) + √D(X)  (Eq. 1)

Computation Cost (Table III of PLOSHA-RMFR paper):
  Prediction:   T_fluc (fluctuation estimation via stochastic model, Eq. 1)
  Scheduling:   O(J² + (J + N_in) × M × N_M) (preprocessing + initial)
  Aggregation:  None
  Recovery:     O((J + N_in) × M × N_M + J × N_in) (online scheduling + adjustment)

Communication Cost (Table IV):
  Data Collection:  N_s × |Data|
  Coordination:     |Sched| (scheduling messages)
  Recovery:         m* × |State| (scales with FULL processing window)

Key behavioral characteristics:
  - Workflow-level fault tolerance via replication + resubmission
  - Recovery scales with full processing window m*, NOT |D_i^miss|
  - Has prediction (T_fluc) but no encrypted aggregation
  - Scheduling complexity polynomial in workflow parameters (J, M, N_M)
  - Recovery overhead grows steeply under increasing failures
"""

import numpy as np
from evaluation.baselines.base import BaselineScheme
from src.config import SystemConfig


class Ref37Scheme(BaselineScheme):
    """HFWPF: Hybrid Fault-Tolerant Workflow Scheduling."""

    name = "Ref. [37]"

    # ---- Calibrated parameters (from Ref[37] paper, Sec V) ----
    # Workflow structure parameters
    J_PER_SENSOR = 0.1           # Workflow tasks generated per sensor
    N_IN_AVG = 3                 # Average task indegree (DAG precedence)
    M_VM_TYPES = 9               # Number of VM types (Amazon EC2 C6g, Sec III-A)
    N_M_BASE = 5                 # Base number of active VMs per type

    # Timing parameters
    T_FLUC = 0.0003              # T_fluc: Fluctuation estimation (Eq. 1)
    T_BOOT = 0.05                # VM boot time (seconds)
    T_PREPROCESS_PER_J2 = 1e-7   # O(J²) preprocessing cost coefficient
    T_SCHEDULE_COEFF = 5e-8      # O((J+N_in)·M·N_M) scheduling coefficient
    T_ADJUST_COEFF = 1e-7        # O(J·N_in) online adjustment coefficient

    # Communication sizes
    DATA_SIZE = 128              # |Data|: raw sensor data payload (bytes)
    SCHED_SIZE = 256             # |Sched|: scheduling message size (bytes)
    STATE_SIZE = 2048            # |State|: per-slot state transfer (bytes)

    def __init__(self, config: SystemConfig = None):
        super().__init__(config)

    def _workflow_params(self, num_sensors: int, num_fog_nodes: int):
        """Compute workflow parameters mapped to IIoT scenario."""
        J = max(10, int(num_sensors * self.J_PER_SENSOR))  # Total workflow tasks
        N_in = self.N_IN_AVG
        M = self.M_VM_TYPES
        N_M = max(self.N_M_BASE, num_fog_nodes // 2)  # VMs scale with fog nodes
        return J, N_in, M, N_M

    def aggregation_latency(self, num_sensors: int, num_fog_nodes: int,
                            failure_rate: float, workload_intensity: float = 1.0,
                            **kwargs) -> float:
        """
        HFWPF aggregation latency per epoch.

        From Table III:
          Prediction:  T_fluc
          Scheduling:  O(J² + (J + N_in) × M × N_M)
          Aggregation: None

        HFWPF schedules workflow tasks across VMs. The "aggregation latency"
        maps to the total scheduling + processing time for completing
        all workflow tasks within an epoch.
        """
        N_s = int(num_sensors * workload_intensity)
        J, N_in, M, N_M = self._workflow_params(N_s, num_fog_nodes)

        # Prediction: fluctuation estimation
        t_predict = self.T_FLUC

        # Preprocessing: O(J²) deadline division
        t_preprocess = (J ** 2) * self.T_PREPROCESS_PER_J2

        # Initial Scheduling: O((J + N_in) × M × N_M)
        t_schedule = (J + N_in) * M * N_M * self.T_SCHEDULE_COEFF

        # Task execution on VMs (stochastic execution time, Eq. 1)
        # w(X) = E(X) + √D(X) → adds ~20% overhead over deterministic
        base_exec_per_task = 0.00005  # 50μs per task execution
        t_exec = J * base_exec_per_task * 1.2  # Stochastic overhead

        total = t_predict + t_preprocess + t_schedule + t_exec

        # More fog nodes (VMs) helps distribute work
        node_scaling = max(1.0, num_fog_nodes / 5.0)
        total = total / (node_scaling ** 0.4)

        return total

    def recovery_latency(self, num_sensors: int, num_fog_nodes: int,
                         failure_rate: float, num_micro_slots: int = 10,
                         **kwargs) -> float:
        """
        HFWPF recovery latency.

        From Table III:
          Recovery: O((J + N_in) × M × N_M + J × N_in)

        HFWPF uses hybrid resubmission + replication (Eq. 8):
          - Resubmission: re-execute on another VM → higher latency
          - Replication: redundant copy already running → lower latency
        Both operate at WORKFLOW level (not micro-slot level).
        Recovery scales with full processing window.
        """
        J, N_in, M, N_M = self._workflow_params(num_sensors, num_fog_nodes)
        failed_nodes = max(1, int(num_fog_nodes * failure_rate))

        # Online Scheduling: O((J + N_in) × M × N_M)
        t_online_sch = (J + N_in) * M * N_M * self.T_SCHEDULE_COEFF

        # Online Adjustment: O(J × N_in)
        t_adjust = J * N_in * self.T_ADJUST_COEFF

        # VM boot time for resubmission
        # Resubmission requires booting a new VM (Eq. 8-9)
        resubmission_fraction = 0.6  # ~60% of tasks use resubmission
        t_boot = failed_nodes * self.T_BOOT * resubmission_fraction

        # Recovery scales with entire processing window (not just failures)
        total = t_online_sch + t_adjust + t_boot

        # Scale with failure severity
        total *= (1 + failure_rate * 2.0)

        return total

    def loss_exposure(self, num_micro_slots: int = 10, **kwargs) -> float:
        """
        HFWPF loss exposure.

        Workflow-level recovery recovers at task/sub-workflow granularity,
        not at micro-slot level. Replication provides SOME fault isolation,
        but recovery scope is still larger than PLOSHA-RMFR's micro-slot
        approach.

        From paper (Sec V, Exp 5): "Refs. [37] and [38] recover failures
        at the workflow or service level, resulting in larger recovery
        scopes and greater aggregation disruption."

        Loss exposure decreases slightly with more micro-slots (as
        finer workflow decomposition helps), but never reaches 1/m*.
        """
        # Workflow-level → exposure is bounded below by replication overhead
        # Replication provides some isolation, floor at ~0.3
        base_exposure = 0.7
        # Slight improvement with more partitions (workflow decomposition)
        improvement = 0.01 * min(num_micro_slots, 10)
        return max(0.3, base_exposure - improvement)

    def comm_overhead_kb(self, num_sensors: int, num_fog_nodes: int,
                         failure_rate: float, num_micro_slots: int = 10,
                         **kwargs) -> float:
        """
        HFWPF communication overhead.

        From Table IV:
          Data Collection:  N_s × |Data|
          Coordination:     |Sched|
          Recovery:         m* × |State| (scales with FULL processing window)

        Recovery communication scales with the entire processing window
        m*, NOT just |D_i^miss|.
        """
        # Data collection: sensor → fog/VM
        data_collection = num_sensors * self.DATA_SIZE

        # Coordination: scheduling messages
        coordination = num_fog_nodes * self.SCHED_SIZE

        # Recovery: m* × |State| — full processing window
        # This is a KEY disadvantage: recovery comm scales with m*, not |D_miss|
        failed_nodes = max(1, int(num_fog_nodes * failure_rate))
        recovery_comm = num_micro_slots * self.STATE_SIZE * failed_nodes

        total_bytes = data_collection + coordination + recovery_comm
        return total_bytes / 1024.0

    def completeness(self, num_sensors: int, num_fog_nodes: int,
                     failure_rate: float, **kwargs) -> float:
        """
        HFWPF completeness.

        Replication + resubmission provides good fault tolerance.
        Replication: immediate failover → high completeness.
        Resubmission: delayed recovery → some data may be lost.
        """
        # Hybrid strategy achieves better completeness than no-recovery
        # but not as good as PLOSHA-RMFR's micro-slot recovery
        replication_fraction = 0.4
        resubmission_fraction = 0.6
        # Replication fully recovers; resubmission partially recovers
        replicated_recovery = replication_fraction * failure_rate * 0.95
        resubmitted_recovery = resubmission_fraction * failure_rate * 0.7
        total_recovered = replicated_recovery + resubmitted_recovery
        return max(0.0, 1.0 - failure_rate + total_recovered)

    def availability(self, num_sensors: int, num_fog_nodes: int,
                     failure_rate: float, **kwargs) -> float:
        """
        HFWPF availability.

        Hybrid fault tolerance maintains reasonable availability but
        resubmission-based recovery can violate soft deadlines.
        """
        return max(0.0, 1.0 - failure_rate * 0.5)
