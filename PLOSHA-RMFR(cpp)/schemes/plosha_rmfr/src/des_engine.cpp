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

  // Create sensors and assign to fog nodes (round-robin)
  for (int s = 0; s < config.num_sensors; ++s) {
    int fog_id = s % config.num_fog_nodes;
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

  // --- Sensor Encryption & Queue Submission ---
  // Submit encrypted readings to fog node queues (using real pthreads via
  // mutex)
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
      fog_nodes[fog_id].submitReading(qr);
    }
  }

  // --- Failure Injection ---
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

  // --- Phase II: EWMA Prediction ---
  std::vector<FogState> current_states(num_fog);
  std::vector<PredictionVector> predictions(num_fog);

  for (int f = 0; f < num_fog; ++f) {
    current_states[f] = fog_nodes[f].getState();
    predictions[f] = ewma_.predict(current_states[f], prev_states[f],
                                   feedback_states[f].thresholds.tau_r);
  }

  // --- Phase III: PLOSHA Aggregation (per fog node) ---
  double total_agg_latency = 0.0;
  double total_completeness = 0.0;
  double total_queue_util = 0.0;
  int successful_fogs = 0;
  int recovery_count = 0;
  double total_recovery_latency = 0.0;
  double total_comm_overhead = 0.0;

  for (int f = 0; f < num_fog; ++f) {
    if (fog_nodes[f].isFailed())
      continue;

    auto queued_readings = fog_nodes[f].drainQueue();
    int expected = static_cast<int>(fog_nodes[f].assignedSensors().size()) *
                   config.workload_multiplier;

    // Measure queue utilization before draining
    total_queue_util += current_states[f].queue_load;

    // Aggregate
    auto agg_result = plosha_engine_.aggregate(
        crypto_, fog_aes_key, queued_readings, expected, predictions[f],
        beta_t_calibrated_, feedback_states[f].thresholds.tau_v,
        current_states[f].reliability,
        config.forced_micro_slots);

    total_agg_latency += agg_result.aggregation_latency_ms;
    total_completeness += agg_result.completeness_V;

    // Update fog processing latency for L_i normalization (R6)
    fog_nodes[f].updateProcessingLatency(agg_result.aggregation_latency_ms);
    fog_nodes[f].setMicroSlotAggregates(std::move(agg_result.micro_aggs));

    // Loss exposure: 1/m* for single-slot failure
    double loss_exp = (agg_result.m_star > 0) ? 1.0 / agg_result.m_star : 1.0;

    // --- Phase IV: RMFR Recovery (if incomplete) ---
    RecoveryResult rec_result;
    if (!agg_result.completeness_flag) {
      // Determine incomplete slot indices
      std::vector<int> incomplete_slots;
      if (config.forced_micro_slots > 0) {
        // For Exp 6: force N incomplete slots
        for (int s = 0;
             s < std::min(config.forced_micro_slots, agg_result.m_star); ++s) {
          incomplete_slots.push_back(s);
        }
      }

      rec_result = rmfr_engine_.executeRecovery(
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
  metrics.system_availability = static_cast<double>(successful_fogs) / num_fog;
  metrics.queue_utilization = total_queue_util / num_fog;
  metrics.recovery_frequency = recovery_count;

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
  sweep.output_dir = config.output_dir + "/exp4_failure_rate";

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
  sweep.output_dir = config.output_dir + "/exp5_loss_exposure";

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
  sweep.output_dir = config.output_dir + "/exp6_recovery_comm";

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
  SweepConfig sweep;
  sweep.variable_name = "aflto_enabled";
  sweep.sweep_values = {0, 1};
  sweep.output_dir = config.output_dir + "/exp7_aflto_ablation";

  ExperimentConfig exp_config = config;
  exp_config.num_sensors = 1000;
  exp_config.num_fog_nodes = 10;
  exp_config.failure_rate = 0.10;
  exp_config.num_epochs = ABLATION_EPOCHS;

  auto results = runSweep(sweep, exp_config);
  MetricsCollector::writeResultsFile(sweep.output_dir + "/results.csv",
                                     sweep.variable_name, results);
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

  std::cout << "[DES] Running all 7 experiments...\n\n";
  auto total_start = std::chrono::high_resolution_clock::now();

  for (int exp = 1; exp <= 7; ++exp) {
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

} // namespace plosha
