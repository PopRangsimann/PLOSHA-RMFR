# PLOSHA-RMFR: Predictive Load-Sharing Hierarchical Aggregation with Risk-Aware Multi-Layer Fault Recovery

A secure and resilient aggregation framework for IIoT edge-fog environments. This repository implements the complete five-phase PLOSHA-RMFR system as described in the paper, along with a simulation-based evaluation harness.

## System Architecture

```
Industrial IoT Sensors (S)  ──AES-GCM──►  Fog Nodes (F)  ──Paillier──►  Cloud Server (C)
                                              │   ▲
                                              │   │
                                          Key & Reliability
                                          Management (KRM)
```

## Five-Phase Framework

| Phase | Name | Description |
|-------|------|-------------|
| I   | System Initialization | Entity attestation, key provisioning, Paillier setup |
| II  | Predictive Estimation | EWMA-based capacity, failure exposure, and risk prediction |
| III | PLOSHA Aggregation | Adaptive micro-slot partitioning, TEE ciphertext transformation, homomorphic aggregation |
| IV  | RMFR Recovery | Progressive escalation: Normal → Delegation → MicroRecovery → Failover |
| V   | AFLTO Optimization | Quality scoring, historical learning, adaptive threshold optimization |

## Project Structure

```
PLOSHA-RMFR/
├── src/
│   ├── config.py              # System parameters (Table 1)
│   ├── framework.py           # Orchestrator (Algorithm 1)
│   ├── entities/              # Sensor, FogNode, KRM, CloudServer
│   ├── phases/                # Phase I-V implementations
│   └── crypto/                # AES-GCM, Paillier, TEE
├── evaluation/
│   ├── simulator.py           # Simulation engine
│   ├── metrics.py             # 6 evaluation metrics
│   ├── baselines/             # 4 comparison schemes
│   └── experiments/           # 7 experiments (Figs. 2-8)
├── results/                   # Generated plots
└── References/                # Paper PDF and markdown
```

## Installation

```bash
pip install -r requirements.txt
```

## Quick Test

Validate that the full 5-phase pipeline executes correctly:

```bash
python -m src.framework
```

## Run All Experiments

```bash
python -m evaluation.experiments.run_all
```

This generates comparison plots in `results/` corresponding to Figures 2-8 in the paper.

## Run Individual Experiments

```bash
python -m evaluation.experiments.exp1_sensor_scalability
python -m evaluation.experiments.exp2_fog_scalability
python -m evaluation.experiments.exp3_workload_intensity
python -m evaluation.experiments.exp4_failure_rate
python -m evaluation.experiments.exp5_loss_exposure
python -m evaluation.experiments.exp6_recovery_comm
python -m evaluation.experiments.exp7_aflto_ablation
```

## Key Equations

- **Capacity**: `Cap_i(t+1) = 1 - (ω_w·Ŵ + ω_q·Q̂ + ω_l·L̂)`
- **Failure Exposure**: `FE_i(t) = Ŵ · Q̂ · (1 - R̂el)`
- **Risk**: `Risk_i(t) = γ_1·(1 - Cap) + γ_2·FE`
- **Loss Exposure**: `L_agg(m*) = 1/m*`
- **Recovery Urgency**: `RU_i = ρ_1·Risk + ρ_2·(1-V) + ρ_3·(1-Rel)`
- **Quality Score**: `Score_i = ω_1·V + ω_2·Rel`
- **Threshold Update**: `τ_x(t+1) = Π_{[0,1]}(τ_x(t) + μ_x·e_i(t))`
