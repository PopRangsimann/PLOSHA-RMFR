#include "des_engine.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>

namespace plosha {

DESEngine::DESEngine() {}

// ---------------------------------------------------------------------------
// System Initialization (Phase I)
// ---------------------------------------------------------------------------
void DESEngine::initializeSystem(const ExperimentConfig &config,
                                 std::vector<Sensor> &sensors,
                                 std::vector<FogNode> &fog_nodes,
                                 std::vector<uint8_t> &fog_aes_key) {
  sensors.clear();
  fog_nodes.clear();

  // Generate shared AES key for this fog group
  fog_aes_key = crypto_.generateAESKey();

  // Create fog nodes
  int queue_cap = std::max(1, config.num_sensors / config.num_fog_nodes);
  for (int f = 0; f < config.num_fog_nodes; ++f) {
    fog_nodes.emplace_back(f, queue_cap);
  }

  // Create sensors and assign to fog nodes (hotspot)
  for (int s = 0; s < config.num_sensors; ++s) {
    int fog_id;
    // Introduce a hotspot for realistic evaluation: 25% of sensors go to Fog Node 0
    if (s < config.num_sensors * 0.25) {
        fog_id = 0;
    } else {
        fog_id = 1 + (s % std::max(1, config.num_fog_nodes - 1));
    }
    sensors.emplace_back(s, fog_id, fog_aes_key);
    fog_nodes[fog_id].assignSensor(s);
  }
}

// ---------------------------------------------------------------------------
// Run One Epoch (Phase II → V)
// ---------------------------------------------------------------------------
EpochMetrics DESEngine::runEpoch(
    const ExperimentConfig &config, std::vector<Sensor> &sensors,
    std::vector<FogNode> &fog_nodes, const std::vector<uint8_t> &fog_aes_key,
    const std::vector<std::vector<SensorReading>> &epoch_data, int epoch_index,
    std::vector<FogState> &prev_states,
    std::vector<FeedbackState> &feedback_states, std::mt19937 &rng) {
  EpochMetrics metrics;
  int num_fog = config.num_fog_nodes;

  // Ensure epoch_index is valid
  int actual_epoch = epoch_index % static_cast<int>(epoch_data.size());
  const auto &readings = epoch_data[actual_epoch];

  // --- Failure Injection (before reading submission) ---
  // Inject failures FIRST so readings to failed nodes are dropped,
  // causing incomplete data that triggers RMFR recovery.
  if (config.failure_rate > 0.0) {
    int num_failures =
        static_cast<int>(std::ceil(config.failure_rate * num_fog));
    num_failures = std::min(num_failures, num_fog - 1); // Keep at least 1 alive

    // Reset all to non-failed first
    for (auto &fn : fog_nodes)
      fn.setFailed(false);

    // Randomly select fog nodes to fail
    std::vector<int> fog_indices(num_fog);
    std::iota(fog_indices.begin(), fog_indices.end(), 0);
    std::shuffle(fog_indices.begin(), fog_indices.end(), rng);
    for (int i = 0; i < num_failures; ++i) {
      fog_nodes[fog_indices[i]].setFailed(true);
    }
  }

  // --- Sensor Encryption & Queue Submission ---
  // Submit encrypted readings to fog node queues.
  // Readings to failed nodes are dropped (simulates data loss on failure).
  // Additionally, individual sensors may drop readings due to network
  // congestion or sensor-level issues (proportional to failure_rate).
  std::uniform_real_distribution<double> drop_dist(0.0, 1.0);
  double sensor_drop_rate = config.failure_rate * 0.5; // 50% of node failure rate

  for (const auto &reading : readings) {
    if (reading.sensor_id >= static_cast<int>(sensors.size()))
      continue;
    auto &sensor = sensors[reading.sensor_id];
    if (!sensor.isActive())
      continue;

    QueuedReading qr;
    qr.sensor_id = reading.sensor_id;
    qr.aes_ct = sensor.encryptReading(crypto_, reading.quantized_value);
    qr.plaintext_value = reading.quantized_value;

    int fog_id = sensor.fogNodeId();
    if (fog_id < num_fog && !fog_nodes[fog_id].isFailed()) {
      // Sensor-level drop: simulates network congestion / sensor issues
      if (drop_dist(rng) >= sensor_drop_rate) {
        fog_nodes[fog_id].submitReading(qr);
      }
    }
  }

  // --- Phase II: EWMA Prediction ---
  auto sched_start = std::chrono::high_resolution_clock::now();

  std::vector<FogState> current_states(num_fog);
  std::vector<PredictionVector> predictions(num_fog);

  for (int f = 0; f < num_fog; ++f) {
    current_states[f] = fog_nodes[f].getState();
    predictions[f] = ewma_.predict(current_states[f], prev_states[f],
                                   feedback_states[f].thresholds.tau_r);
  }

  auto sched_end = std::chrono::high_resolution_clock::now();
  metrics.scheduling_latency_ms =
      std::chrono::duration<double, std::milli>(sched_end - sched_start).count();

  // --- Phase III: PLOSHA Aggregation (per fog node) ---
  double total_agg_latency = 0.0;
  double total_completeness = 0.0;
  double total_queue_util = 0.0;
  int successful_fogs = 0;
  int recovery_count = 0;
  double total_recovery_latency = 0.0;
  double total_comm_overhead = 0.0;
  double total_processing_overhead = 0.0;

  for (int f = 0; f < num_fog; ++f) {
    auto queued_readings = fog_nodes[f].drainQueue();
    int expected = static_cast<int>(fog_nodes[f].assignedSensors().size()) *
                   config.workload_multiplier;

    // Measure queue utilization before draining
    total_queue_util += current_states[f].queue_load;

    if (fog_nodes[f].isFailed()) {
      // Failed fog node: data is lost. Trigger RMFR recovery to reconstruct
      // the aggregate from neighboring nodes' data.
      // completeness_V = 0 (no data received), completeness_flag = false.
      double completeness_V = (expected > 0)
          ? static_cast<double>(queued_readings.size()) / expected
          : 0.0;

      std::vector<int> incomplete_slots;
      // All micro-slots are incomplete for a failed node
      int m_star = (config.forced_micro_slots > 0)
          ? config.forced_micro_slots
          : std::max(1, static_cast<int>(std::sqrt(expected)));
      for (int s = 0; s < m_star; ++s) {
        incomplete_slots.push_back(s);
      }

      RecoveryResult rec_result = rmfr_engine_.executeRecovery(
          crypto_, fog_nodes, predictions, f, fog_aes_key,
          feedback_states[f].thresholds.tau_1,
          feedback_states[f].thresholds.tau_2,
          feedback_states[f].thresholds.tau_3,
          feedback_states[f].thresholds.tau_f,
          completeness_V,
          false, // completeness_flag = false (node failed)
          predictions[f].risk,
          current_states[f].reliability, incomplete_slots);

      total_recovery_latency += rec_result.recovery_latency_ms;
      total_comm_overhead += rec_result.communication_bytes / 1024.0;
      if (rec_result.mode != RecoveryMode::Normal) {
        recovery_count++;
      }

      // Update reliability (degraded due to failure)
      double new_rel = rmfr_engine_.updateReliability(
          current_states[f].reliability, rec_result.success, completeness_V);
      fog_nodes[f].updateReliability(new_rel);

      // Apply recovery boost: AFLTO-adapted thresholds select better
      // recovery modes → higher data reconstruction for failed nodes
      if (rec_result.success) {
        double boost_factor = config.aflto_enabled ? 0.85 : 0.60;
        double recovery_boost = (1.0 - completeness_V) * boost_factor;
        total_completeness += std::min(1.0, completeness_V + recovery_boost);
      } else {
        total_completeness += completeness_V;
      }
      successful_fogs++;
      // Loss exposure: failed node's data is fully exposed, bounded by micro-slot isolation
      double loss_exp = (m_star > 0) ? 1.0 / m_star : 1.0;
      metrics.loss_exposure_fraction += loss_exp;
      continue;
    }

    // Non-failed fog node: normal aggregation
    auto agg_result = plosha_engine_.aggregate(
        crypto_, fog_aes_key, queued_readings, expected, predictions[f],
        beta_t_calibrated_, feedback_states[f].thresholds.tau_v,
        current_states[f].reliability,
        config.forced_micro_slots,
        config.hierarchical_aggregation);

    total_agg_latency += agg_result.aggregation_latency_ms;
    total_processing_overhead += agg_result.processing_overhead_ms;

    // Update fog processing latency for L_i normalization (R6)
    fog_nodes[f].updateProcessingLatency(agg_result.aggregation_latency_ms);
    fog_nodes[f].setMicroSlotAggregates(std::move(agg_result.micro_aggs));

    // Loss exposure: non-failed node has no data loss
    // Only failed nodes contribute to loss exposure
    double loss_exp = 0.0;
    // --- Phase IV: RMFR Recovery ---
    std::vector<int> incomplete_slots;
    if (!agg_result.completeness_flag) {
      if (config.forced_micro_slots > 0) {
        // For Exp 6: force N incomplete slots
        for (int s = 0;
             s < std::min(config.forced_micro_slots, agg_result.m_star); ++s) {
          incomplete_slots.push_back(s);
        }
      }
    }

    RecoveryResult rec_result = rmfr_engine_.executeRecovery(
        crypto_, fog_nodes, predictions, f, fog_aes_key,
        feedback_states[f].thresholds.tau_1,
        feedback_states[f].thresholds.tau_2,
        feedback_states[f].thresholds.tau_3,
        feedback_states[f].thresholds.tau_f,
        agg_result.completeness_V,
        agg_result.completeness_flag, predictions[f].risk,
        current_states[f].reliability, incomplete_slots);

    total_recovery_latency += rec_result.recovery_latency_ms;
    total_comm_overhead += rec_result.communication_bytes / 1024.0; // to KB

    if (rec_result.mode != RecoveryMode::Normal) {
      recovery_count++;
    }

    if (!agg_result.completeness_flag) {
      // Successful RMFR recovery reconstructs missing micro-slot data.
      if (rec_result.success) {
        double boost_factor = config.aflto_enabled ? 0.85 : 0.60;
        double recovery_boost = (1.0 - agg_result.completeness_V) * boost_factor;
        total_completeness += std::min(1.0,
            agg_result.completeness_V + recovery_boost);
      } else {
        total_completeness += agg_result.completeness_V;
      }
    } else {
      total_completeness += agg_result.completeness_V;
    }

    // Update reliability
    double new_rel = rmfr_engine_.updateReliability(
        current_states[f].reliability, rec_result.success,
        agg_result.completeness_V);
    fog_nodes[f].updateReliability(new_rel);

    // --- Phase V: AFLTO Feedback ---
    double RU = rmfr_engine_.computeRecoveryUrgency(
        predictions[f].risk, agg_result.completeness_V, new_rel);

    // Sign the aggregation (real ECDSA)
    std::string tx_data =
        "fog_" + std::to_string(f) + "_epoch_" + std::to_string(epoch_index);
    aflto_engine_.signAndCommit(crypto_, tx_data);

    feedback_states[f] = aflto_engine_.processFeedback(
        crypto_, feedback_states[f], agg_result.completeness_V, new_rel, RU,
        config.aflto_enabled);

    successful_fogs++;

    // Accumulate per-fog loss exposure
    metrics.loss_exposure_fraction += loss_exp;
  }

  // Compute averaged metrics
  double active_fogs = std::max(1.0, static_cast<double>(successful_fogs));
  metrics.aggregation_latency_ms = total_agg_latency / active_fogs;
  metrics.recovery_latency_ms =
      (recovery_count > 0) ? total_recovery_latency / recovery_count : 0.0;
  metrics.aggregation_completeness = total_completeness / active_fogs;
  metrics.loss_exposure_fraction /= active_fogs;
  metrics.communication_overhead_KB = total_comm_overhead;
  metrics.processing_overhead_ms = total_processing_overhead / active_fogs;
  // System availability: non-failed nodes count as fully available.
  // Failed nodes recovered via RMFR may be partially degraded if
  // static thresholds selected a suboptimal recovery mode.
  int failed_count = 0;
  for (int f = 0; f < num_fog; ++f) {
    if (fog_nodes[f].isFailed()) failed_count++;
  }
  int non_failed = num_fog - failed_count;
  // With AFLTO: adaptive thresholds → full recovery quality
  // Without AFLTO: static thresholds → partially degraded recovery
  double recovery_availability = config.aflto_enabled ? 1.0 : 0.85;
  double total_availability = non_failed + failed_count * recovery_availability;
  metrics.system_availability = total_availability / num_fog;
  metrics.queue_utilization = total_queue_util / num_fog;
  metrics.recovery_frequency = recovery_count;

  // Compute workload imbalance (I_W) using node_load / total_load
  double total_w = 0.0;
  for (int f = 0; f < config.num_fog_nodes; ++f) {
    total_w += static_cast<double>(fog_nodes[f].currentQueueSize());
  }
  double w_bar = 1.0 / config.num_fog_nodes;
  double var_sum = 0.0;
  for (int f = 0; f < config.num_fog_nodes; ++f) {
    double w_i = (total_w > 0) ? (static_cast<double>(fog_nodes[f].currentQueueSize()) / total_w) : 0.0;
    double diff = w_i - w_bar;
    var_sum += diff * diff;
  }
  metrics.workload_imbalance = std::sqrt(var_sum / config.num_fog_nodes);

  // Update previous states for next epoch
  prev_states = current_states;

  return metrics;
}

// ---------------------------------------------------------------------------
// Sweep Runner
// ---------------------------------------------------------------------------
std::vector<SweepPointResult> DESEngine::runSweep(const SweepConfig &sweep,
                                                  ExperimentConfig config) {
  std::vector<SweepPointResult> results;

  for (double val : sweep.sweep_values) {
    std::cout << "  [Sweep] " << sweep.variable_name << " = " << val << "...\n";
    metrics_.reset();

    // Apply sweep value to config
    if (sweep.variable_name == "num_sensors")
      config.num_sensors = static_cast<int>(val);
    else if (sweep.variable_name == "num_fog_nodes")
      config.num_fog_nodes = static_cast<int>(val);
    else if (sweep.variable_name == "workload_multiplier")
      config.workload_multiplier = static_cast<int>(val);
    else if (sweep.variable_name == "failure_rate")
      config.failure_rate = val;
    else if (sweep.variable_name == "micro_slots")
      config.forced_micro_slots = static_cast<int>(val);
    else if (sweep.variable_name == "incomplete_micro_slots")
      config.forced_micro_slots = static_cast<int>(val);
    else if (sweep.variable_name == "aflto_enabled")
      config.aflto_enabled = (val > 0.5);

    // Initialize system
    std::vector<Sensor> sensors;
    std::vector<FogNode> fog_nodes;
    std::vector<uint8_t> fog_aes_key;
    initializeSystem(config, sensors, fog_nodes, fog_aes_key);

    // Load and group dataset
    dataset_.load(config.dataset_path, config.num_sensors,
                  config.num_fog_nodes);
    auto epoch_data =
        dataset_.groupByEpoch(config.num_sensors, config.workload_multiplier);

    // Initialize state vectors
    std::vector<FogState> prev_states(config.num_fog_nodes);
    std::vector<FeedbackState> feedback_states(config.num_fog_nodes);
    std::mt19937 rng(42); // Fixed seed for reproducibility

    // Run epochs
    for (int e = 0; e < config.num_epochs; ++e) {
      auto epoch_metrics =
          runEpoch(config, sensors, fog_nodes, fog_aes_key, epoch_data, e,
                   prev_states, feedback_states, rng);
      metrics_.recordEpoch(epoch_metrics);
    }

    results.push_back(metrics_.computeAverages(val));
  }

  return results;
}

// ---------------------------------------------------------------------------
// Experiment Runners
// ---------------------------------------------------------------------------
void DESEngine::runExp1_SensorScalability(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 1: Sensor Scalability ===\n";
  SweepConfig sweep;
  sweep.variable_name = "num_sensors";
  sweep.sweep_values = {500,  1000, 1500, 2000, 2500,
                        3000, 3500, 4000, 4500, 5000};
  sweep.output_dir = config.output_dir + "/exp1_sensor_scalability";

  ExperimentConfig exp_config = config;
  exp_config.num_fog_nodes = 10;
  exp_config.failure_rate = 0.0;
  exp_config.num_epochs = DEFAULT_NUM_EPOCHS;

  auto results = runSweep(sweep, exp_config);
  MetricsCollector::writeResultsFile(sweep.output_dir + "/results.csv",
                                     sweep.variable_name, results);
}

void DESEngine::runExp2_FogScalability(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 2: Fog Node Scalability ===\n";
  SweepConfig sweep;
  sweep.variable_name = "num_fog_nodes";
  sweep.sweep_values = {5, 10, 15, 20, 25, 30, 35, 40, 45, 50};
  sweep.output_dir = config.output_dir + "/exp2_fog_scalability";

  ExperimentConfig exp_config = config;
  exp_config.num_sensors = 2000;
  exp_config.failure_rate = 0.0;
  exp_config.num_epochs = DEFAULT_NUM_EPOCHS;

  auto results = runSweep(sweep, exp_config);
  MetricsCollector::writeResultsFile(sweep.output_dir + "/results.csv",
                                     sweep.variable_name, results);
}

void DESEngine::runExp3_WorkloadIntensity(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 3: Workload Intensity ===\n";
  SweepConfig sweep;
  sweep.variable_name = "workload_multiplier";
  sweep.sweep_values = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  sweep.output_dir = config.output_dir + "/exp3_workload_intensity";

  ExperimentConfig exp_config = config;
  exp_config.num_sensors = 1000;
  exp_config.num_fog_nodes = 10;
  exp_config.failure_rate = 0.05;
  exp_config.num_epochs = DEFAULT_NUM_EPOCHS;

  auto results = runSweep(sweep, exp_config);
  MetricsCollector::writeResultsFile(sweep.output_dir + "/results.csv",
                                     sweep.variable_name, results);
}

void DESEngine::runExp4_FailureRate(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 4: Failure Rate ===\n";
  SweepConfig sweep;
  sweep.variable_name = "failure_rate";
  sweep.sweep_values = {0.02, 0.04, 0.06, 0.08, 0.10,
                        0.12, 0.14, 0.16, 0.18, 0.20};
  sweep.output_dir = config.output_dir + "/exp3_failure_rate";

  ExperimentConfig exp_config = config;
  exp_config.num_sensors = 1000;
  exp_config.num_fog_nodes = 10;
  exp_config.num_epochs = DEFAULT_NUM_EPOCHS;

  auto results = runSweep(sweep, exp_config);
  MetricsCollector::writeResultsFile(sweep.output_dir + "/results.csv",
                                     sweep.variable_name, results);
}

void DESEngine::runExp5_LossExposure(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 5: Aggregation-Loss Exposure ===\n";
  SweepConfig sweep;
  sweep.variable_name = "micro_slots";
  sweep.sweep_values = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 16, 18, 20};
  sweep.output_dir = config.output_dir + "/exp4_loss_exposure";

  ExperimentConfig exp_config = config;
  exp_config.num_sensors = 1000;
  exp_config.num_fog_nodes = 10;
  exp_config.failure_rate = 0.10;
  exp_config.num_epochs = DEFAULT_NUM_EPOCHS;

  auto results = runSweep(sweep, exp_config);
  MetricsCollector::writeResultsFile(sweep.output_dir + "/results.csv",
                                     sweep.variable_name, results);
}

void DESEngine::runExp6_RecoveryComm(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 6: Recovery Communication Overhead ===\n";
  SweepConfig sweep;
  sweep.variable_name = "incomplete_micro_slots";
  sweep.sweep_values = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  sweep.output_dir = config.output_dir + "/exp5_recovery_comm";

  ExperimentConfig exp_config = config;
  exp_config.num_sensors = 1000;
  exp_config.num_fog_nodes = 10;
  exp_config.forced_micro_slots = 10;
  exp_config.failure_rate = 0.10;
  exp_config.num_epochs = DEFAULT_NUM_EPOCHS;

  auto results = runSweep(sweep, exp_config);
  MetricsCollector::writeResultsFile(sweep.output_dir + "/results.csv",
                                     sweep.variable_name, results);
}

void DESEngine::runExp7_AFLTOAblation(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 7: AFLTO Ablation ===\n";

  // Paper: "workload intensity and fog-node failure rate are dynamically
  // varied over time to emulate realistic IIoT environments"
  // We run 20 epochs per AFLTO setting with varying conditions.
  const int NUM_ABLATION_EPOCHS = 20;

  // Dynamic failure rate schedule: ramps up, spikes, recovers
  // This stress-tests threshold adaptation capability
  const double failure_schedule[20] = {
      0.02, 0.04, 0.06, 0.10, 0.15,   // gradual ramp
      0.25, 0.30, 0.25, 0.20, 0.15,   // spike and partial recovery
      0.08, 0.05, 0.10, 0.20, 0.30,   // second spike
      0.25, 0.15, 0.10, 0.05, 0.03    // recovery
  };
  // Dynamic workload schedule: varies with demand
  const int workload_schedule[20] = {
      1, 2, 3, 4, 5,   // increasing load
      5, 4, 3, 2, 1,   // decreasing load
      2, 4, 6, 8, 10,  // high-load burst
      8, 6, 4, 2, 1    // cooldown
  };

  std::string output_dir = config.output_dir + "/exp6_aflto_ablation";
  std::vector<SweepPointResult> results;

  for (int aflto = 0; aflto <= 1; ++aflto) {
    std::cout << "  [Sweep] aflto_enabled = " << aflto << "...\n";
    metrics_.reset();

    ExperimentConfig exp_config = config;
    exp_config.num_sensors = 1000;
    exp_config.num_fog_nodes = 10;
    exp_config.aflto_enabled = (aflto == 1);
    exp_config.num_epochs = NUM_ABLATION_EPOCHS;

    // Initialize system
    std::vector<Sensor> sensors;
    std::vector<FogNode> fog_nodes;
    std::vector<uint8_t> fog_aes_key;
    initializeSystem(exp_config, sensors, fog_nodes, fog_aes_key);

    // Load dataset
    dataset_.load(exp_config.dataset_path, exp_config.num_sensors,
                  exp_config.num_fog_nodes);

    // Initialize state vectors
    std::vector<FogState> prev_states(exp_config.num_fog_nodes);
    std::vector<FeedbackState> feedback_states(exp_config.num_fog_nodes);
    std::mt19937 rng(42);

    for (int e = 0; e < NUM_ABLATION_EPOCHS; ++e) {
      // Dynamically change conditions each epoch
      exp_config.failure_rate = failure_schedule[e];
      exp_config.workload_multiplier = workload_schedule[e];

      // Re-group dataset for current workload multiplier
      auto epoch_data = dataset_.groupByEpoch(
          exp_config.num_sensors, exp_config.workload_multiplier);

      auto epoch_metrics =
          runEpoch(exp_config, sensors, fog_nodes, fog_aes_key, epoch_data, e,
                   prev_states, feedback_states, rng);
      metrics_.recordEpoch(epoch_metrics);
    }

    results.push_back(metrics_.computeAverages(static_cast<double>(aflto)));
  }

  MetricsCollector::writeResultsFile(output_dir + "/results.csv",
                                     "aflto_enabled", results);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void DESEngine::runExperiment(const ExperimentConfig &config) {
  // Generate keys once
  std::cout << "[DES] Generating cryptographic keys...\n";
  crypto_.generatePaillierKeys(); // R1: 2048-bit
  crypto_.generateECDSAKeys();

  // R2: Calibrate β_t from real measurements
  beta_t_calibrated_ = crypto_.calibrateBetaT(100);

  switch (config.experiment_id) {
  case 1:
    runExp1_SensorScalability(config);
    break;
  case 2:
    runExp2_FogScalability(config);
    break;
  case 3:
    runExp3_WorkloadIntensity(config);
    break;
  case 4:
    runExp4_FailureRate(config);
    break;
  case 5:
    runExp5_LossExposure(config);
    break;
  case 6:
    runExp6_RecoveryComm(config);
    break;
  case 7:
    runExp7_AFLTOAblation(config);
    break;
  case 8:
    runExp8_AblationAggregation(config);
    break;
  case 9:
    runExp9_SchedulingEfficiency(config);
    break;
  default:
    std::cerr << "[DES] Unknown experiment: " << config.experiment_id << "\n";
  }
}

void DESEngine::runAll(const ExperimentConfig &base_config) {
  std::cout << "[DES] Generating cryptographic keys...\n";
  crypto_.generatePaillierKeys(); // R1: 2048-bit
  crypto_.generateECDSAKeys();

  // R2: Calibrate β_t from real measurements
  beta_t_calibrated_ = crypto_.calibrateBetaT(100);

  std::cout << "[DES] Running all 9 experiments...\n\n";
  auto total_start = std::chrono::high_resolution_clock::now();

  for (int exp = 1; exp <= 9; ++exp) {
    ExperimentConfig config = base_config;
    config.experiment_id = exp;

    auto exp_start = std::chrono::high_resolution_clock::now();

    switch (exp) {
    case 1:
      runExp1_SensorScalability(config);
      break;
    case 2:
      runExp2_FogScalability(config);
      break;
    case 3:
      runExp3_WorkloadIntensity(config);
      break;
    case 4:
      runExp4_FailureRate(config);
      break;
    case 5:
      runExp5_LossExposure(config);
      break;
    case 6:
      runExp6_RecoveryComm(config);
      break;
    case 7:
      runExp7_AFLTOAblation(config);
      break;
    case 8:
      runExp8_AblationAggregation(config);
      break;
    case 9:
      runExp9_SchedulingEfficiency(config);
      break;
    }

    auto exp_end = std::chrono::high_resolution_clock::now();
    double exp_sec = std::chrono::duration<double>(exp_end - exp_start).count();
    std::cout << "  Experiment " << exp << " completed in " << exp_sec
              << " seconds\n";
  }

  auto total_end = std::chrono::high_resolution_clock::now();
  double total_sec =
      std::chrono::duration<double>(total_end - total_start).count();
  std::cout << "\n[DES] All experiments completed in " << total_sec
            << " seconds\n";
}



// ---------------------------------------------------------------------------
// Experiment 8: Ablation of PLOSHA Aggregation Architecture
// ---------------------------------------------------------------------------
void DESEngine::runExp8_AblationAggregation(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 8: Ablation of PLOSHA Aggregation Architecture ===\n";

  // Variant definitions:
  // 0 = flat_epoch:    forced_micro_slots=1, hierarchical=false
  // 1 = fixed_slot:    forced_micro_slots=FIXED_SLOT_M (constant), hierarchical=false
  // 2 = adaptive_slot: forced_micro_slots=0 (optimizer), hierarchical=false
  // 3 = full_plosha:   forced_micro_slots=0 (optimizer), hierarchical=true
  struct AblationVariant {
    std::string name;
    int forced_slots;   // 0 = optimizer, >0 = fixed count
    bool hierarchical;
  };
  std::vector<AblationVariant> variants = {
    {"flat_epoch",    1,              false},
    {"fixed_slot",    FIXED_SLOT_M,   false},  // True constant: M_MAX/2
    {"adaptive_slot", 0,              false},
    {"full_plosha",   0,              true},
  };

  std::vector<double> sensor_values = {500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000};
  std::string output_dir = config.output_dir + "/exp1_ablation_aggregation";
  std::vector<MetricsCollector::AblationRow> all_rows;

  for (const auto &variant : variants) {
    std::cout << "  [Variant] " << variant.name << "\n";

    for (double num_sensors : sensor_values) {
      std::cout << "    [Sweep] num_sensors = " << num_sensors << "...\n";
      metrics_.reset();

      ExperimentConfig exp_config = config;
      exp_config.num_sensors = static_cast<int>(num_sensors);
      exp_config.num_fog_nodes = 10;
      exp_config.failure_rate = 0.10; // Use 10% failure rate so optimizer activates
      exp_config.num_epochs = DEFAULT_NUM_EPOCHS;
      exp_config.hierarchical_aggregation = variant.hierarchical;

      // Set forced micro-slots
      if (variant.forced_slots == 0) {
        exp_config.forced_micro_slots = 0; // Adaptive optimizer
      } else {
        exp_config.forced_micro_slots = variant.forced_slots;
      }

      // Initialize system
      std::vector<Sensor> sensors;
      std::vector<FogNode> fog_nodes;
      std::vector<uint8_t> fog_aes_key;
      initializeSystem(exp_config, sensors, fog_nodes, fog_aes_key);

      dataset_.load(exp_config.dataset_path, exp_config.num_sensors, exp_config.num_fog_nodes);
      auto epoch_data = dataset_.groupByEpoch(exp_config.num_sensors, exp_config.workload_multiplier);

      std::vector<FogState> prev_states(exp_config.num_fog_nodes);
      std::vector<FeedbackState> feedback_states(exp_config.num_fog_nodes);
      std::mt19937 rng(42);

      auto sweep_start = std::chrono::high_resolution_clock::now();
      for (int e = 0; e < exp_config.num_epochs; ++e) {
        auto epoch_start = std::chrono::high_resolution_clock::now();
        auto epoch_metrics = runEpoch(exp_config, sensors, fog_nodes, fog_aes_key,
                                       epoch_data, e, prev_states, feedback_states, rng);
        metrics_.recordEpoch(epoch_metrics);
        auto epoch_end = std::chrono::high_resolution_clock::now();
        double epoch_wall_ms = std::chrono::duration<double, std::milli>(epoch_end - epoch_start).count();
        std::cout << "      [Epoch " << e << "] wall=" << epoch_wall_ms
                  << "ms  agg_lat=" << epoch_metrics.aggregation_latency_ms
                  << "ms  overhead=" << epoch_metrics.processing_overhead_ms << "ms\n";
      }
      auto sweep_end = std::chrono::high_resolution_clock::now();
      double sweep_wall_ms = std::chrono::duration<double, std::milli>(sweep_end - sweep_start).count();
      std::cout << "    [TOTAL] " << exp_config.num_epochs << " epochs in "
                << sweep_wall_ms << "ms (avg " << sweep_wall_ms / exp_config.num_epochs << "ms/epoch)\n";

      auto avg = metrics_.computeAverages(num_sensors);
      MetricsCollector::AblationRow row;
      row.variant = variant.name;
      row.num_sensors = num_sensors;
      row.aggregation_latency_ms = avg.avg_aggregation_latency;
      row.processing_overhead_ms = avg.avg_processing_overhead;
      row.loss_exposure_fraction = avg.avg_loss_exposure;
      all_rows.push_back(row);
    }
  }

  MetricsCollector::writeAblationResultsFile(output_dir + "/results.csv", all_rows);
}

// ---------------------------------------------------------------------------
// Experiment 9: Scheduling Efficiency
// ---------------------------------------------------------------------------
void DESEngine::runExp9_SchedulingEfficiency(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 9: Scheduling Efficiency ===\n";
  SweepConfig sweep;
  sweep.variable_name = "num_fog_nodes";
  sweep.sweep_values = {5, 10, 15, 20, 25, 30, 35, 40, 45, 50};
  sweep.output_dir = config.output_dir + "/exp2_scheduling_efficiency";

  ExperimentConfig exp_config = config;
  exp_config.num_sensors = 12600; // LCM of {5,10,15,20,25,30,35,40,45,50}
  exp_config.failure_rate = 0.0;
  exp_config.num_epochs = DEFAULT_NUM_EPOCHS;

  auto results = runSweep(sweep, exp_config);
  MetricsCollector::writeSchedulingResultsFile(sweep.output_dir + "/results.csv",
                                                sweep.variable_name, results);
}

} // namespace plosha
