#include "ft_engine.hpp"
#include <iostream>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace ftworkflow {

FTEngine::FTEngine() {}

void FTEngine::initializeSystem(const ExperimentConfig& config,
                                std::vector<Sensor>& sensors,
                                std::vector<FogNode>& fog_nodes,
                                std::vector<uint8_t>& fog_aes_key) {
    sensors.clear();
    fog_nodes.clear();
    fog_aes_key = crypto_.generateAESKey();

    int queue_cap = std::max(1, config.num_sensors / config.num_fog_nodes);
    for (int f = 0; f < config.num_fog_nodes; ++f) {
        fog_nodes.emplace_back(f, queue_cap);
    }
    for (int s = 0; s < config.num_sensors; ++s) {
        int fog_id = s % config.num_fog_nodes;
        sensors.emplace_back(s, fog_id, fog_aes_key);
        fog_nodes[fog_id].assignSensor(s);
    }
}

EpochMetrics FTEngine::runEpoch(const ExperimentConfig& config,
                                std::vector<Sensor>& sensors,
                                std::vector<FogNode>& fog_nodes,
                                const std::vector<uint8_t>& fog_aes_key,
                                const std::vector<std::vector<SensorReading>>& epoch_data,
                                int epoch_index,
                                std::mt19937& rng) {
    EpochMetrics metrics;
    int num_fog = config.num_fog_nodes;
    int actual_epoch = epoch_index % static_cast<int>(epoch_data.size());
    const auto& readings = epoch_data[actual_epoch];

    // Submit encrypted readings
    for (const auto& reading : readings) {
        if (reading.sensor_id >= static_cast<int>(sensors.size())) continue;
        auto& sensor = sensors[reading.sensor_id];
        if (!sensor.isActive()) continue;
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
    for (auto& fn : fog_nodes) fn.setFailed(false);
    if (config.failure_rate > 0.0) {
        int num_failures = static_cast<int>(std::ceil(config.failure_rate * num_fog));
        num_failures = std::min(num_failures, num_fog - 1);
        std::vector<int> fog_indices(num_fog);
        std::iota(fog_indices.begin(), fog_indices.end(), 0);
        std::shuffle(fog_indices.begin(), fog_indices.end(), rng);
        for (int i = 0; i < num_failures; ++i) {
            fog_nodes[fog_indices[i]].setFailed(true);
        }
    }

    // Performance Fluctuation update
    std::uniform_real_distribution<double> perf_dist(PERF_COEFF_MIN, PERF_COEFF_MAX);
    for (auto& fn : fog_nodes) {
        double new_perf = perf_dist(rng);
        double smoothed = PERF_EWMA_ALPHA * new_perf + (1.0 - PERF_EWMA_ALPHA) * fn.perfCoefficient();
        fn.updatePerfCoefficient(smoothed);
    }

    double total_agg_latency = 0.0;
    double total_completeness = 0.0;
    double total_queue_util = 0.0;
    int successful_fogs = 0;
    int recovery_count = 0;
    double total_recovery_latency = 0.0;
    double total_comm_overhead = 0.0;
    double total_loss_exposure = 0.0;

    for (int f = 0; f < num_fog; ++f) {
        bool node_failed = fog_nodes[f].isFailed();
        auto queued_readings = fog_nodes[f].drainQueue();
        int expected = static_cast<int>(fog_nodes[f].assignedSensors().size()) * config.workload_multiplier;

        auto state = fog_nodes[f].getState();
        total_queue_util += state.queue_load;

        if (queued_readings.empty()) continue;

        // Estimate Task Duration
        double T_task_est = (queued_readings.size() * beta_t_calibrated_ * 1000.0) / state.perf_coeff;
        bool use_resubmission = (T_task_est < TASK_DURATION_THRESHOLD_MS);

        auto start = std::chrono::high_resolution_clock::now();

        // AES-only aggregation: decrypt, sum in plaintext, re-encrypt
        // Ref[37] is a scheduling paper — it does NOT use homomorphic encryption
        if (!node_failed) {
            uint64_t plaintext_sum = 0;
            for (const auto& reading : queued_readings) {
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
            auto agg_ct = crypto_.aesEncrypt(fog_aes_key, sum_bytes, sizeof(uint64_t));
            
            auto end = std::chrono::high_resolution_clock::now();
            double latency_ms = std::chrono::duration<double, std::milli>(end - start).count();
            
            // Adjust for performance coefficient
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
                for (const auto& reading : queued_readings) {
                    auto pt = crypto_.aesDecrypt(fog_aes_key, reading.aes_ct);
                    uint32_t val = 0;
                    if (pt.size() >= sizeof(uint32_t))
                        memcpy(&val, pt.data(), sizeof(uint32_t));
                    plaintext_sum += val;
                }
                uint8_t sum_bytes[sizeof(uint64_t)];
                memcpy(sum_bytes, &plaintext_sum, sizeof(uint64_t));
                auto agg_ct = crypto_.aesEncrypt(fog_aes_key, sum_bytes, sizeof(uint64_t));
            } else {
                // Replication Recovery
                comm_bytes += queued_readings.size() * 128 * REPLICATION_COMM_RATIO;
                rec_latency += T_task_est;
                uint64_t plaintext_sum = 0;
                for (const auto& reading : queued_readings) {
                    auto pt = crypto_.aesDecrypt(fog_aes_key, reading.aes_ct);
                    uint32_t val = 0;
                    if (pt.size() >= sizeof(uint32_t))
                        memcpy(&val, pt.data(), sizeof(uint32_t));
                    plaintext_sum += val;
                }
                uint8_t sum_bytes[sizeof(uint64_t)];
                memcpy(sum_bytes, &plaintext_sum, sizeof(uint64_t));
                auto agg_ct = crypto_.aesEncrypt(fog_aes_key, sum_bytes, sizeof(uint64_t));
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
    metrics.recovery_latency_ms = (recovery_count > 0) ? total_recovery_latency / recovery_count : 0.0;
    metrics.aggregation_completeness = total_completeness / active_fogs;
    metrics.loss_exposure_fraction = total_loss_exposure / active_fogs;
    metrics.communication_overhead_KB = total_comm_overhead;
    metrics.system_availability = static_cast<double>(successful_fogs) / num_fog;
    metrics.queue_utilization = total_queue_util / num_fog;
    metrics.recovery_frequency = recovery_count;

    return metrics;
}

std::vector<SweepPointResult> FTEngine::runSweep(const SweepConfig& sweep, ExperimentConfig config) {
    std::vector<SweepPointResult> results;
    for (double val : sweep.sweep_values) {
        std::cout << "  [Sweep] " << sweep.variable_name << " = " << val << "...\n";
        metrics_.reset();

        if (sweep.variable_name == "num_sensors") config.num_sensors = static_cast<int>(val);
        else if (sweep.variable_name == "num_fog_nodes") config.num_fog_nodes = static_cast<int>(val);
        else if (sweep.variable_name == "workload_multiplier") config.workload_multiplier = static_cast<int>(val);
        else if (sweep.variable_name == "failure_rate") config.failure_rate = val;
        else if (sweep.variable_name == "incomplete_micro_slots") {
            // More incomplete slots → proportionally more fog nodes need recovery
            // Scale failure rate: 1 slot → 10%, 10 slots → 100%
            config.failure_rate = val * 0.10;
        }
        else if (sweep.variable_name == "micro_slots") config.forced_micro_slots = static_cast<int>(val);
        
        std::vector<Sensor> sensors;
        std::vector<FogNode> fog_nodes;
        std::vector<uint8_t> fog_aes_key;
        initializeSystem(config, sensors, fog_nodes, fog_aes_key);

        dataset_.load(config.dataset_path, config.num_sensors, config.num_fog_nodes);
        auto epoch_data = dataset_.groupByEpoch(config.num_sensors, config.workload_multiplier);

        std::mt19937 rng(42);
        for (int e = 0; e < config.num_epochs; ++e) {
            auto epoch_metrics = runEpoch(config, sensors, fog_nodes, fog_aes_key, epoch_data, e, rng);
            metrics_.recordEpoch(epoch_metrics);
        }
        results.push_back(metrics_.computeAverages(val));
    }
    return results;
}

void FTEngine::runExp1_SensorScalability(const ExperimentConfig& config) {
    std::cout << "\n=== Experiment 1: Sensor Scalability ===\n";
    SweepConfig sweep{"num_sensors", {500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000}, config.output_dir + "/exp1_sensor_scalability"};
    ExperimentConfig exp_config = config;
    exp_config.num_fog_nodes = 10;
    exp_config.failure_rate = 0.0;
    auto results = runSweep(sweep, exp_config);
    MetricsCollector::writeResultsFile(sweep.output_dir + "/results.csv", sweep.variable_name, results);
}

void FTEngine::runExp2_FogScalability(const ExperimentConfig& config) {
    std::cout << "\n=== Experiment 2: Fog Node Scalability ===\n";
    SweepConfig sweep{"num_fog_nodes", {5, 10, 15, 20, 25, 30, 35, 40, 45, 50}, config.output_dir + "/exp2_fog_scalability"};
    ExperimentConfig exp_config = config;
    exp_config.num_sensors = 2000;
    exp_config.failure_rate = 0.0;
    auto results = runSweep(sweep, exp_config);
    MetricsCollector::writeResultsFile(sweep.output_dir + "/results.csv", sweep.variable_name, results);
}

void FTEngine::runExp3_WorkloadIntensity(const ExperimentConfig& config) {
    std::cout << "\n=== Experiment 3: Workload Intensity ===\n";
    SweepConfig sweep{"workload_multiplier", {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, config.output_dir + "/exp3_workload_intensity"};
    ExperimentConfig exp_config = config;
    exp_config.num_sensors = 1000;
    exp_config.num_fog_nodes = 10;
    exp_config.failure_rate = 0.05;
    auto results = runSweep(sweep, exp_config);
    MetricsCollector::writeResultsFile(sweep.output_dir + "/results.csv", sweep.variable_name, results);
}

void FTEngine::runExp4_FailureRate(const ExperimentConfig& config) {
    std::cout << "\n=== Experiment 4: Failure Rate ===\n";
    SweepConfig sweep{"failure_rate", {0.02, 0.04, 0.06, 0.08, 0.10, 0.12, 0.14, 0.16, 0.18, 0.20}, config.output_dir + "/exp4_failure_rate"};
    ExperimentConfig exp_config = config;
    exp_config.num_sensors = 1000;
    exp_config.num_fog_nodes = 10;
    auto results = runSweep(sweep, exp_config);
    MetricsCollector::writeResultsFile(sweep.output_dir + "/results.csv", sweep.variable_name, results);
}

void FTEngine::runExp5_LossExposure(const ExperimentConfig& config) {
    std::cout << "\n=== Experiment 5: Aggregation-Loss Exposure ===\n";
    SweepConfig sweep{"micro_slots", {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 16, 18, 20}, config.output_dir + "/exp5_loss_exposure"};
    ExperimentConfig exp_config = config;
    exp_config.num_sensors = 1000;
    exp_config.num_fog_nodes = 10;
    exp_config.failure_rate = 0.10;
    auto results = runSweep(sweep, exp_config);
    MetricsCollector::writeResultsFile(sweep.output_dir + "/results.csv", sweep.variable_name, results);
}

void FTEngine::runExp6_RecoveryComm(const ExperimentConfig& config) {
    std::cout << "\n=== Experiment 6: Recovery Communication Overhead ===\n";
    SweepConfig sweep{"incomplete_micro_slots", {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, config.output_dir + "/exp6_recovery_comm"};
    ExperimentConfig exp_config = config;
    exp_config.num_sensors = 1000;
    exp_config.num_fog_nodes = 10;
    exp_config.failure_rate = 0.10;
    auto results = runSweep(sweep, exp_config);
    MetricsCollector::writeResultsFile(sweep.output_dir + "/results.csv", sweep.variable_name, results);
}

void FTEngine::runExperiment(const ExperimentConfig& config) {
    std::cout << "[FTWorkflow-Engine] Generating cryptographic keys...\n";
    // Ref[37] uses AES-only (no Paillier homomorphic encryption)
    crypto_.generateECDSAKeys();
    beta_t_calibrated_ = crypto_.calibrateBetaT(100);

    switch (config.experiment_id) {
        case 1: runExp1_SensorScalability(config); break;
        case 2: runExp2_FogScalability(config); break;
        case 3: runExp3_WorkloadIntensity(config); break;
        case 4: runExp4_FailureRate(config); break;
        case 5: runExp5_LossExposure(config); break;
        case 6: runExp6_RecoveryComm(config); break;
        default: std::cerr << "Unknown experiment ID\n";
    }
}

void FTEngine::runAll(const ExperimentConfig& base_config) {
    std::cout << "[FTWorkflow-Engine] Generating cryptographic keys...\n";
    // Ref[37] uses AES-only (no Paillier homomorphic encryption)
    crypto_.generateECDSAKeys();
    beta_t_calibrated_ = crypto_.calibrateBetaT(100);

    std::cout << "[FTWorkflow-Engine] Running all experiments...\n";
    for (int exp = 1; exp <= 6; ++exp) {
        ExperimentConfig config = base_config;
        config.experiment_id = exp;
        switch (exp) {
            case 1: runExp1_SensorScalability(config); break;
            case 2: runExp2_FogScalability(config); break;
            case 3: runExp3_WorkloadIntensity(config); break;
            case 4: runExp4_FailureRate(config); break;
            case 5: runExp5_LossExposure(config); break;
            case 6: runExp6_RecoveryComm(config); break;
        }
    }
}

} // namespace ftworkflow
