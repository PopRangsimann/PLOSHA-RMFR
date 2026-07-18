#include "ft_engine.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>

namespace ftworkflow {

FTEngine::FTEngine() {}

void FTEngine::initializeSystem(const ExperimentConfig &config,
                                std::vector<Sensor> &sensors,
                                std::vector<FogNode> &fog_nodes,
                                std::vector<uint8_t> &fog_aes_key) {
  sensors.clear();
  fog_nodes.clear();
  fog_aes_key = crypto_.generateAESKey();

  int queue_cap = std::max(1, config.num_sensors / config.num_fog_nodes);
  for (int f = 0; f < config.num_fog_nodes; ++f) {
    fog_nodes.emplace_back(f, queue_cap);
  }
  for (int s = 0; s < config.num_sensors; ++s) {
    int fog_id;
    // Introduce a hotspot for realistic evaluation: 25% of sensors go to Fog
    // Node 0
    if (s < config.num_sensors * 0.25) {
      fog_id = 0;
    } else {
      fog_id = 1 + (s % std::max(1, config.num_fog_nodes - 1));
    }
    sensors.emplace_back(s, fog_id, fog_aes_key);
    fog_nodes[fog_id].assignSensor(s);
  }
}

EpochMetrics
FTEngine::runEpoch(const ExperimentConfig &config, std::vector<Sensor> &sensors,
                   std::vector<FogNode> &fog_nodes,
                   const std::vector<uint8_t> &fog_aes_key,
                   const std::vector<std::vector<SensorReading>> &epoch_data,
                   int epoch_index, std::mt19937 &rng) {
  EpochMetrics metrics;
  int num_fog = config.num_fog_nodes;
  int actual_epoch = epoch_index % static_cast<int>(epoch_data.size());
  const auto &readings = epoch_data[actual_epoch];

  // Submit encrypted readings
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

  // Failure Injection
  for (auto &fn : fog_nodes)
    fn.setFailed(false);
  if (config.failure_rate > 0.0) {
    int num_failures =
        static_cast<int>(std::ceil(config.failure_rate * num_fog));
    num_failures = std::min(num_failures, num_fog - 1);
    std::vector<int> fog_indices(num_fog);
    std::iota(fog_indices.begin(), fog_indices.end(), 0);
    std::shuffle(fog_indices.begin(), fog_indices.end(), rng);
    for (int i = 0; i < num_failures; ++i) {
      fog_nodes[fog_indices[i]].setFailed(true);
    }
  }

  // Performance Fluctuation update
  auto sched_start = std::chrono::high_resolution_clock::now();
  std::uniform_real_distribution<double> perf_dist(PERF_COEFF_MIN,
                                                   PERF_COEFF_MAX);
  for (auto &fn : fog_nodes) {
    double new_perf = perf_dist(rng);
    double smoothed = PERF_EWMA_ALPHA * new_perf +
                      (1.0 - PERF_EWMA_ALPHA) * fn.perfCoefficient();
    fn.updatePerfCoefficient(smoothed);
  }
  auto sched_end = std::chrono::high_resolution_clock::now();
  metrics.scheduling_latency_ms =
      std::chrono::duration<double, std::milli>(sched_end - sched_start)
          .count();

  double total_agg_latency = 0.0;
  double total_completeness = 0.0;
  double total_queue_util = 0.0;
  int successful_fogs = 0;
  int recovery_count = 0;
  double total_recovery_latency = 0.0;
  double total_comm_overhead = 0.0;
  double total_loss_exposure = 0.0;

  std::vector<int> pre_drain_counts(num_fog, 0);
  for (int f = 0; f < num_fog; ++f) {
    bool node_failed = fog_nodes[f].isFailed();
    pre_drain_counts[f] = fog_nodes[f].currentQueueSize();
    auto state =
        fog_nodes[f]
            .getState(); // Capture state BEFORE drain (for other metrics)
    total_queue_util += state.queue_load;
    auto queued_readings = fog_nodes[f].drainQueue();

    if (queued_readings.empty())
      continue;

    // Estimate Task Duration
    double T_task_est = (queued_readings.size() * beta_t_calibrated_ * 1000.0) /
                        state.perf_coeff;
    bool use_resubmission = (T_task_est < TASK_DURATION_THRESHOLD_MS);

    auto start = std::chrono::high_resolution_clock::now();

    // AES-only aggregation: decrypt, sum in plaintext, re-encrypt
    // Ref[37] is a scheduling paper — it does NOT use homomorphic encryption
    if (!node_failed) {
      uint64_t plaintext_sum = 0;
      for (const auto &reading : queued_readings) {
        // Decrypt AES ciphertext to get plaintext value
        auto pt = crypto_.aesDecrypt(fog_aes_key, reading.aes_ct);
        uint32_t val = 0;
        if (pt.size() >= sizeof(uint32_t))
          memcpy(&val, pt.data(), sizeof(uint32_t));
        plaintext_sum += val;
      }
      // Re-encrypt the aggregate
      uint8_t sum_bytes[sizeof(uint64_t)];
      memcpy(sum_bytes, &plaintext_sum, sizeof(uint64_t));
      auto agg_ct =
          crypto_.aesEncrypt(fog_aes_key, sum_bytes, sizeof(uint64_t));

      auto end = std::chrono::high_resolution_clock::now();
      double latency_ms =
          std::chrono::duration<double, std::milli>(end - start).count();

      latency_ms /= state.perf_coeff;

      total_agg_latency += latency_ms;
      total_completeness += 1.0;
      successful_fogs++;
      total_loss_exposure += 0.0; // Non-failed: no data loss
    } else {
      // Node failed during execution
      recovery_count++;
      double rec_latency = 0.0;
      double comm_bytes = 0.0;

      if (use_resubmission) {
        // Resubmission Recovery
        comm_bytes += queued_readings.size() * 128 * REPLICATION_COMM_RATIO;
        rec_latency += T_task_est;

        // Re-execute with AES-only aggregation
        uint64_t plaintext_sum = 0;
        for (const auto &reading : queued_readings) {
          auto pt = crypto_.aesDecrypt(fog_aes_key, reading.aes_ct);
          uint32_t val = 0;
          if (pt.size() >= sizeof(uint32_t))
            memcpy(&val, pt.data(), sizeof(uint32_t));
          plaintext_sum += val;
        }
        uint8_t sum_bytes[sizeof(uint64_t)];
        memcpy(sum_bytes, &plaintext_sum, sizeof(uint64_t));
        auto agg_ct =
            crypto_.aesEncrypt(fog_aes_key, sum_bytes, sizeof(uint64_t));
      } else {
        // Replication Recovery
        comm_bytes += queued_readings.size() * 128 * REPLICATION_COMM_RATIO;
        rec_latency += T_task_est;
        uint64_t plaintext_sum = 0;
        for (const auto &reading : queued_readings) {
          auto pt = crypto_.aesDecrypt(fog_aes_key, reading.aes_ct);
          uint32_t val = 0;
          if (pt.size() >= sizeof(uint32_t))
            memcpy(&val, pt.data(), sizeof(uint32_t));
          plaintext_sum += val;
        }
        uint8_t sum_bytes[sizeof(uint64_t)];
        memcpy(sum_bytes, &plaintext_sum, sizeof(uint64_t));
        auto agg_ct =
            crypto_.aesEncrypt(fog_aes_key, sum_bytes, sizeof(uint64_t));
      }

      total_recovery_latency += rec_latency;
      total_comm_overhead += comm_bytes / 1024.0;
      total_agg_latency += T_task_est; // Original partial attempt

      // Assuming successful recovery
      total_completeness += 1.0;
      successful_fogs++;
      total_loss_exposure += 1.0; // No micro-slots
    }
  }

  double active_fogs = std::max(1.0, static_cast<double>(num_fog));
  metrics.aggregation_latency_ms = total_agg_latency / active_fogs;
  metrics.recovery_latency_ms =
      (recovery_count > 0) ? total_recovery_latency / recovery_count : 0.0;
  metrics.aggregation_completeness = total_completeness / active_fogs;
  metrics.loss_exposure_fraction = total_loss_exposure / active_fogs;
  metrics.communication_overhead_KB = total_comm_overhead;
  metrics.system_availability = static_cast<double>(successful_fogs) / num_fog;
  metrics.queue_utilization = total_queue_util / num_fog;
  metrics.recovery_frequency = recovery_count;

  // Compute workload imbalance using total_load (standardized across all
  // baselines)
  {
    double total_w = 0.0;
    for (int f = 0; f < num_fog; ++f) {
      total_w += static_cast<double>(pre_drain_counts[f]);
    }
    double w_bar = 1.0 / num_fog;
    double var_sum = 0.0;
    for (int f = 0; f < num_fog; ++f) {
      double w_i = (total_w > 0)
                       ? (static_cast<double>(pre_drain_counts[f]) / total_w)
                       : 0.0;
      double diff = w_i - w_bar;
      var_sum += diff * diff;
    }
    metrics.workload_imbalance = std::sqrt(var_sum / num_fog);
  }

  return metrics;
}

std::vector<SweepPointResult> FTEngine::runSweep(const SweepConfig &sweep,
                                                 ExperimentConfig config) {
  std::vector<SweepPointResult> results;
  for (double val : sweep.sweep_values) {
    std::cout << "  [Sweep] " << sweep.variable_name << " = " << val << "...\n";
    metrics_.reset();

    if (sweep.variable_name == "num_sensors")
      config.num_sensors = static_cast<int>(val);
    else if (sweep.variable_name == "num_fog_nodes")
      config.num_fog_nodes = static_cast<int>(val);
    else if (sweep.variable_name == "workload_multiplier")
      config.workload_multiplier = static_cast<int>(val);
    else if (sweep.variable_name == "failure_rate")
      config.failure_rate = val;
    else if (sweep.variable_name == "incomplete_micro_slots") {
      // More incomplete slots → proportionally more fog nodes need recovery
      // Scale failure rate: 1 slot → 10%, 10 slots → 100%
      config.failure_rate = val * 0.10;
    } else if (sweep.variable_name == "micro_slots")
      config.forced_micro_slots = static_cast<int>(val);

    std::vector<Sensor> sensors;
    std::vector<FogNode> fog_nodes;
    std::vector<uint8_t> fog_aes_key;
    initializeSystem(config, sensors, fog_nodes, fog_aes_key);

    dataset_.load(config.dataset_path, config.num_sensors,
                  config.num_fog_nodes);
    auto epoch_data =
        dataset_.groupByEpoch(config.num_sensors, config.workload_multiplier);

    std::mt19937 rng(42);
    for (int e = 0; e < config.num_epochs; ++e) {
      auto epoch_metrics =
          runEpoch(config, sensors, fog_nodes, fog_aes_key, epoch_data, e, rng);
      metrics_.recordEpoch(epoch_metrics);
    }
    results.push_back(metrics_.computeAverages(val));
  }
  return results;
}

void FTEngine::runExp1_SensorScalability(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 1: Sensor Scalability ===\n";
  SweepConfig sweep{"num_sensors",
                    {500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000},
                    config.output_dir + "/exp1_sensor_scalability"};
  ExperimentConfig exp_config = config;
  exp_config.num_fog_nodes = 10;
  exp_config.failure_rate = 0.0;
  auto results = runSweep(sweep, exp_config);
  MetricsCollector::writeResultsFile(sweep.output_dir + "/results.csv",
                                     sweep.variable_name, results);
}

void FTEngine::runExp2_FogScalability(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 2: Fog Node Scalability ===\n";
  SweepConfig sweep{"num_fog_nodes",
                    {5, 10, 15, 20, 25, 30, 35, 40, 45, 50},
                    config.output_dir + "/exp2_fog_scalability"};
  ExperimentConfig exp_config = config;
  exp_config.num_sensors = 2000;
  exp_config.failure_rate = 0.0;
  auto results = runSweep(sweep, exp_config);
  MetricsCollector::writeResultsFile(sweep.output_dir + "/results.csv",
                                     sweep.variable_name, results);
}

void FTEngine::runExp3_WorkloadIntensity(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 3: Workload Intensity ===\n";
  SweepConfig sweep{"workload_multiplier",
                    {1, 2, 3, 4, 5, 6, 7, 8, 9, 10},
                    config.output_dir + "/exp3_workload_intensity"};
  ExperimentConfig exp_config = config;
  exp_config.num_sensors = 1000;
  exp_config.num_fog_nodes = 10;
  exp_config.failure_rate = 0.05;
  auto results = runSweep(sweep, exp_config);
  MetricsCollector::writeResultsFile(sweep.output_dir + "/results.csv",
                                     sweep.variable_name, results);
}

void FTEngine::runExp4_FailureRate(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 4: Failure Rate ===\n";
  SweepConfig sweep{
      "failure_rate",
      {0.02, 0.04, 0.06, 0.08, 0.10, 0.12, 0.14, 0.16, 0.18, 0.20},
      config.output_dir + "/exp3_failure_rate"};
  ExperimentConfig exp_config = config;
  exp_config.num_sensors = 1000;
  exp_config.num_fog_nodes = 10;
  auto results = runSweep(sweep, exp_config);
  MetricsCollector::writeResultsFile(sweep.output_dir + "/results.csv",
                                     sweep.variable_name, results);
}

void FTEngine::runExp5_LossExposure(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 5: Aggregation-Loss Exposure ===\n";
  SweepConfig sweep{"micro_slots",
                    {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 16, 18, 20},
                    config.output_dir + "/exp4_loss_exposure"};
  ExperimentConfig exp_config = config;
  exp_config.num_sensors = 1000;
  exp_config.num_fog_nodes = 10;
  exp_config.failure_rate = 0.10;
  auto results = runSweep(sweep, exp_config);
  MetricsCollector::writeResultsFile(sweep.output_dir + "/results.csv",
                                     sweep.variable_name, results);
}

void FTEngine::runExp6_RecoveryComm(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 6: Recovery Communication Overhead ===\n";
  SweepConfig sweep{"incomplete_micro_slots",
                    {1, 2, 3, 4, 5, 6, 7, 8, 9, 10},
                    config.output_dir + "/exp5_recovery_comm"};
  ExperimentConfig exp_config = config;
  exp_config.num_sensors = 1000;
  exp_config.num_fog_nodes = 10;
  exp_config.failure_rate = 0.10;
  auto results = runSweep(sweep, exp_config);
  MetricsCollector::writeResultsFile(sweep.output_dir + "/results.csv",
                                     sweep.variable_name, results);
}

void FTEngine::runExp9_SchedulingEfficiency(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 9: Scheduling Efficiency ===\n";
  std::vector<double> fog_sweep = {5, 10, 15, 20, 25, 30, 35, 40, 45, 50};
  std::string output_dir = config.output_dir + "/exp2_scheduling_efficiency";

  std::vector<SweepPointResult> results;

  for (double num_fog_nodes : fog_sweep) {
    std::cout << "  [Sweep] num_fog_nodes = " << num_fog_nodes << "...\n";
    metrics_.reset();
    ExperimentConfig exp_config = config;
    exp_config.num_fog_nodes = static_cast<int>(num_fog_nodes);
    exp_config.num_sensors = exp_config.num_fog_nodes * 100;
    exp_config.failure_rate = 0.0;
    exp_config.num_epochs = 30;
    
    dataset_.load(exp_config.dataset_path, exp_config.num_sensors,
                  exp_config.num_fog_nodes);

    double total_convergence_epochs = 0;

    for (int iter = 0; iter < 30; ++iter) {
        std::vector<Sensor> sensors;
        std::vector<FogNode> fog_nodes;
        std::vector<uint8_t> fog_aes_key;
        initializeSystem(exp_config, sensors, fog_nodes, fog_aes_key);
        
        std::mt19937 rng(42 + iter);
        int convergence_epoch = -1;
        
        for (int e = 0; e < exp_config.num_epochs; ++e) {
            double burst_multiplier = exp_config.workload_multiplier;
            if (e >= 12) {
                burst_multiplier *= 1.5; // 50% burst
            }
            if (e >= 21) {
                // 20% Node Degradation
                int num_degraded = std::max(1, static_cast<int>(exp_config.num_fog_nodes * 0.20));
                for (int i = 0; i < num_degraded; ++i) {
                    fog_nodes[i].updateProcessingLatency(200.0); // Artificially high latency
                }
            }
            auto epoch_data = dataset_.groupByEpoch(exp_config.num_sensors, burst_multiplier);
            
            auto epoch_metrics = runEpoch(exp_config, sensors, fog_nodes, fog_aes_key,
                                           epoch_data, e, rng);
            
            epoch_metrics.scheduling_comm_kb = (exp_config.num_fog_nodes * 32.0) / 1024.0;
            metrics_.recordEpoch(epoch_metrics);
            
            if (e >= 12 && convergence_epoch == -1 && epoch_metrics.workload_imbalance < 0.1) {
                convergence_epoch = e - 12;
            }
        }
        if (convergence_epoch == -1) convergence_epoch = exp_config.num_epochs - 12;
        total_convergence_epochs += convergence_epoch;
    }

    auto avg = metrics_.computeAverages(num_fog_nodes);
    avg.convergence_time_epochs = total_convergence_epochs / 30.0;
    results.push_back(avg);
  }

  auto slash = output_dir.rfind('/');
  if (slash != std::string::npos) {
    std::string dir = output_dir.substr(0, slash);
    std::string cmd = "mkdir -p " + dir;
    (void)system(cmd.c_str());
  }
  std::ofstream file(output_dir + "/results.csv");
  if (file.is_open()) {
    file << "num_fog_nodes,scheduling_latency_ms,workload_imbalance,scheduling_"
            "comm_kb,convergence_time_epochs\n";
    for (const auto &r : results) {
      file << std::fixed << std::setprecision(6) << r.variable_value << ","
           << r.avg_scheduling_latency << "," << r.avg_workload_imbalance << ","
           << r.avg_scheduling_comm_kb << "," << r.convergence_time_epochs
           << "\n";
    }
    file.close();
    std::cout << "[FTWorkflow] Wrote " << results.size()
              << " scheduling rows\n";
  }
}

void FTEngine::runExperiment(const ExperimentConfig &config) {
  std::cout << "[FTWorkflow-Engine] Generating cryptographic keys...\n";
  // Ref[37] uses AES-only (no Paillier homomorphic encryption)
  crypto_.generateECDSAKeys();
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
  case 9:
    runExp9_SchedulingEfficiency(config);
    break;
  default:
    std::cerr << "Unknown experiment ID\n";
  }
}

void FTEngine::runAll(const ExperimentConfig &base_config) {
  std::cout << "[FTWorkflow-Engine] Generating cryptographic keys...\n";
  // Ref[37] uses AES-only (no Paillier homomorphic encryption)
  crypto_.generateECDSAKeys();
  beta_t_calibrated_ = crypto_.calibrateBetaT(100);

  std::cout << "[FTWorkflow-Engine] Running all experiments...\n";
  // Run experiments 1-6 and 9
  int exp_ids[] = {1, 2, 3, 4, 5, 6, 9};
  for (int exp : exp_ids) {
    ExperimentConfig config = base_config;
    config.experiment_id = exp;
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
    case 9:
      runExp9_SchedulingEfficiency(config);
      break;
    }
  }
}

} // namespace ftworkflow
