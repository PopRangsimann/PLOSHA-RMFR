"""
PLOSHA-RMFR Configuration Module
=================================
All system parameters, notation constants, and default thresholds
as defined in the paper (Table 1 and throughout Phases I-V).

Reference: PLOSHA-RMFR Paper - Table 1 (Major Notations)
"""

import dataclasses
from typing import Dict, Any


@dataclasses.dataclass
class SystemConfig:
    """Complete configuration for the PLOSHA-RMFR framework."""

    # =========================================================================
    # System Scale Parameters (Section V - Experimental Setup)
    # =========================================================================
    num_sensors: int = 1000          # |S| - Number of IIoT sensors
    num_fog_nodes: int = 10          # |F| - Number of fog nodes
    num_epochs: int = 50             # Number of aggregation epochs to simulate
    epoch_duration: float = 1.0      # Δ - Duration of one aggregation epoch (seconds)

    # =========================================================================
    # Cryptographic Parameters
    # =========================================================================
    paillier_key_size: int = 1024    # Security parameter λ (bits) for Paillier keygen
    aes_key_size: int = 256          # AES-GCM key size (bits)

    # =========================================================================
    # Phase II: Predictive Capacity and Risk Estimation
    # =========================================================================

    # Step 2: EWMA smoothing coefficient α ∈ (0,1)
    # Ŝtate_i(t+1) = α · State_i(t) + (1-α) · Ŝtate_i(t)
    alpha_ewma: float = 0.3

    # Step 3: Effective Aggregation Capacity weights
    # Cap_i(t+1) = 1 - (ω_w · Ŵ_i + ω_q · Q̂_i + ω_l · L̂_i)
    # Constraint: ω_w + ω_q + ω_l = 1
    omega_w: float = 0.4   # Workload weight
    omega_q: float = 0.3   # Queue utilization weight
    omega_l: float = 0.3   # Communication latency weight

    # Step 5: Operational Risk weights
    # Risk_i(t) = γ_1 · (1 - Cap_i(t+1)) + γ_2 · FE_i(t)
    # Constraint: γ_1 + γ_2 = 1
    gamma_1: float = 0.6   # Capacity degradation weight
    gamma_2: float = 0.4   # Failure exposure weight

    # Step 6: Risk classification threshold
    # Status = "Normal" if Risk < τ_r, else "At-Risk"
    tau_r: float = 0.5

    # =========================================================================
    # Phase III: PLOSHA - Adaptive Hierarchical Slot Aggregation
    # =========================================================================

    # Step 1: Micro-slot optimization
    # m* = argmin_{1≤m≤m_max} [φ_1·T_proc(m) + φ_2·L_agg(m) + φ_3·(1-Rel_i)]
    m_max: int = 20                  # Maximum number of micro-slots
    beta_t: float = 0.01            # Average processing overhead per micro-slot
    phi_1: float = 0.4              # Processing overhead weight
    phi_2: float = 0.35             # Aggregation-loss exposure weight
    phi_3: float = 0.25             # Reliability weight

    # Step 6: Aggregation completeness threshold
    # Φ_i(t) = 1 if V_i(t) < τ_v (incomplete), 0 otherwise (complete)
    tau_v: float = 0.9

    # =========================================================================
    # Phase IV: RMFR - Risk-Aware Multi-Layer Fault Recovery
    # =========================================================================

    # Step 1: Recovery Urgency weights
    # RU_i(t) = ρ_1·Risk_i + ρ_2·(1-V_i) + ρ_3·(1-Rel_i)
    # Constraint: ρ_1 + ρ_2 + ρ_3 = 1
    rho_1: float = 0.4    # Risk weight
    rho_2: float = 0.3    # Incompleteness weight
    rho_3: float = 0.3    # Unreliability weight

    # Step 2: Recovery Escalation Thresholds
    # Normal:        Φ_i=0 ∧ RU < τ_1
    # Delegation:    Φ_i=0 ∧ τ_1 ≤ RU < τ_2
    # MicroRecovery: Φ_i=1 ∧ RU < τ_3
    # Failover:      RU ≥ τ_3 ∨ Rel_i ≤ τ_f
    # Constraint: 0 ≤ τ_1 < τ_2 < τ_3 ≤ 1
    tau_1: float = 0.3    # Normal → Delegation threshold
    tau_2: float = 0.5    # Delegation → MicroRecovery threshold
    tau_3: float = 0.7    # MicroRecovery → Failover threshold
    tau_f: float = 0.2    # Failover reliability threshold

    # Step 3: Recovery Candidate Utility weights
    # U_j(t) = α_c·Cap_j + α_r·Rel_j + α_k·(1-Risk_j)
    # Constraint: α_c + α_r + α_k = 1
    alpha_c: float = 0.4    # Capacity weight
    alpha_r: float = 0.35   # Reliability weight
    alpha_k: float = 0.25   # Risk-free weight

    # Step 7: Reliability Reinforcement
    # Rel_i(t+1) = min{1, β_r·Rel_i(t) + (1-β_r)·[λ_s·Succ_i + λ_v·V_i]}
    beta_r: float = 0.7     # Reliability EWMA smoothing
    lambda_s: float = 0.6   # Recovery success weight
    lambda_v: float = 0.4   # Completeness weight

    # =========================================================================
    # Phase V: AFLTO - Adaptive Feedback Learning & Threshold Optimization
    # =========================================================================

    # Step 2: Quality score weights
    # Score_i(t) = ω_1·V_i(t) + ω_2·Rel_i(t+1)
    # Constraint: ω_1 + ω_2 = 1
    omega_score_1: float = 0.6   # Completeness weight
    omega_score_2: float = 0.4   # Reliability weight

    # Step 3: Historical learning
    # Hist_i(t+1) = γ_h·Hist_i(t) + (1-γ_h)·Score_i(t)
    gamma_hist: float = 0.5      # γ_h ∈ (0,1) - History blend factor

    # Score_i*(t) = γ_h·Hist_i(t+1) + (1-γ_h)·Score_i(t)
    # (uses same γ_h as above)

    # Adaptive control error weights
    # e_i(t) = ε_1·(S_target - Score_i*) + ε_2·RU_i + ε_3·(1-Rel_i)
    # Constraint: ε_1 + ε_2 + ε_3 = 1
    s_target: float = 0.9       # Target quality score
    epsilon_1: float = 0.4      # Quality gap weight
    epsilon_2: float = 0.3      # Recovery urgency weight
    epsilon_3: float = 0.3      # Unreliability weight

    # Step 4: Threshold learning rates
    # τ_x(t+1) = Π_{[0,1]}(τ_x(t) + μ_x · e_i(t))
    mu_v: float = 0.05    # Learning rate for τ_v
    mu_r: float = 0.05    # Learning rate for τ_r
    mu_1: float = 0.05    # Learning rate for τ_1
    mu_2: float = 0.05    # Learning rate for τ_2
    mu_3: float = 0.05    # Learning rate for τ_3
    mu_f: float = 0.05    # Learning rate for τ_f

    # =========================================================================
    # Simulation Parameters
    # =========================================================================
    failure_rate: float = 0.05        # Probability of fog-node failure per epoch
    sensor_drop_rate: float = 0.02    # Probability of sensor report loss
    workload_intensity: float = 1.0   # Multiplier for sensor reporting rate
    random_seed: int = 42             # For reproducibility

    # =========================================================================
    # Timing Model Parameters (calibrated to known operation costs)
    # =========================================================================
    t_aes: float = 0.00001           # T_AES: AES-GCM encrypt/decrypt (seconds)
    t_penc: float = 0.005            # T_PEnc: Paillier encryption (seconds)
    t_padd: float = 0.0001           # T_PAdd: Paillier homomorphic add (seconds)
    t_pdec: float = 0.005            # T_PDec: Paillier decryption (seconds)
    t_tee: float = 0.0001            # T_TEE: TEE ciphertext transformation (seconds)
    t_sig: float = 0.001             # T_Sig: Digital signature (seconds)
    t_pred: float = 0.0001           # T_pred: EWMA prediction (seconds)
    t_risk: float = 0.0001           # T_risk: Risk assessment (seconds)
    t_sch: float = 0.00005           # T_sch: Scheduling operation (seconds)

    # Communication sizes (bytes)
    ct_aes_size: int = 48            # |CT_AES|: AES-GCM ciphertext size
    ct_paillier_size: int = 256      # |CT_P|: Paillier ciphertext size
    meta_size: int = 64              # |Meta|: Micro-slot metadata size
    dsp_size: int = 512              # |DSP|: Delegation state packet size
    fsm_size: int = 1024             # |FSM|: Failover state migration size
    sig_size: int = 64               # |Sig|: Digital signature size

    def validate(self):
        """Validate weight constraints from the paper."""
        assert abs(self.omega_w + self.omega_q + self.omega_l - 1.0) < 1e-9, \
            "Capacity weights must sum to 1: ω_w + ω_q + ω_l = 1"
        assert abs(self.gamma_1 + self.gamma_2 - 1.0) < 1e-9, \
            "Risk weights must sum to 1: γ_1 + γ_2 = 1"
        assert abs(self.rho_1 + self.rho_2 + self.rho_3 - 1.0) < 1e-9, \
            "Recovery urgency weights must sum to 1: ρ_1 + ρ_2 + ρ_3 = 1"
        assert abs(self.alpha_c + self.alpha_r + self.alpha_k - 1.0) < 1e-9, \
            "Candidate utility weights must sum to 1: α_c + α_r + α_k = 1"
        assert abs(self.lambda_s + self.lambda_v - 1.0) < 1e-9, \
            "Reliability reward weights must sum to 1: λ_s + λ_v = 1"
        assert abs(self.omega_score_1 + self.omega_score_2 - 1.0) < 1e-9, \
            "Quality score weights must sum to 1: ω_1 + ω_2 = 1"
        assert abs(self.epsilon_1 + self.epsilon_2 + self.epsilon_3 - 1.0) < 1e-9, \
            "Error weights must sum to 1: ε_1 + ε_2 + ε_3 = 1"
        assert abs(self.phi_1 + self.phi_2 + self.phi_3 - 1.0) < 1e-9, \
            "Micro-slot opt weights must sum to 1: φ_1 + φ_2 + φ_3 = 1"
        assert 0 <= self.tau_1 < self.tau_2 < self.tau_3 <= 1.0, \
            "Escalation ordering: 0 ≤ τ_1 < τ_2 < τ_3 ≤ 1"
        assert 0 < self.alpha_ewma < 1, "EWMA coefficient: α ∈ (0,1)"
        assert 0 < self.gamma_hist < 1, "History blend: γ_h ∈ (0,1)"

    def to_dict(self) -> Dict[str, Any]:
        """Export configuration as dictionary."""
        return dataclasses.asdict(self)


# Default configuration instance
DEFAULT_CONFIG = SystemConfig()
DEFAULT_CONFIG.validate()
