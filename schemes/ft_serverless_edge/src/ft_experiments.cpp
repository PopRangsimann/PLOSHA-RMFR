#include "ft_serverless_edge.hpp"
#include <iostream>
#include <numeric>
#include <algorithm>
#include <cmath>

// ── Constructor ──────────────────────────────────────────────────────────
FTServerlessEdgeSim::FTServerlessEdgeSim(const SimConfig& cfg)
    : cfg_(cfg), rng_(cfg.seed) {
    loadDataset();
}

SimResult FTServerlessEdgeSim::run() {
    switch (cfg_.experiment) {
        case 1: return runExp1_SensorScalability();
        case 2: return runExp2_FogScalability();
        case 3: return runExp3_WorkloadIntensity();
        case 4: return runExp4_FailureRate();
        case 5: return runExp5_LossExposure();
        case 6: return runExp6_RecoveryComm();
        default: return {cfg_.variable_value, 0, 0, 0};
    }
}

// ── Helper: run a batch of requests through the simulation ───────────────
static double runBatch(FTServerlessEdgeSim* sim, std::vector<DSPRequest>& reqs,
                       int num_cloudlets, double fail_override,
                       double* out_queue_util, double* out_recovery_freq,
                       double* out_completeness, double* out_availability,
                       double* out_recovery_latency, double* out_comm_kb) {
    double total_delay = 0.0;
    int total_count = 0;
    int failure_count = 0;
    int recovery_count = 0;
    double total_recovery_delay = 0.0;
    double total_comm_kb = 0.0;
    int completed = 0;

    int num_slots = std::max(1, (int)reqs.size() / 20);
    int per_slot = std::max(1, (int)reqs.size() / num_slots);

    for (int tau = 0; tau < num_slots; tau++) {
        int start = tau * per_slot;
        int end = std::min(start + per_slot, (int)reqs.size());

        for (int i = start; i < end; i++) {
            auto& req = reqs[i];
            // Place functions via Fwk
            sim->algorithmFwk(req);

            // Override failure if specified
            if (fail_override > 0) {
                req.had_failure = (sim->randDouble(0, 1) < fail_override);
            }

            // Simulate failover
            if (req.had_failure) {
                sim->simulateFailover(req);
                failure_count++;
            }

            // Compute delay
            double d = sim->computeTotalDelay(req);
            total_delay += d;
            total_count++;

            if (req.recovery_delay > 0) {
                recovery_count++;
                total_recovery_delay += req.recovery_delay;
            }

            // Communication overhead = state buffer transfers during recovery
            if (req.had_failure) {
                for (auto& fn : req.functions) {
                    if (!fn.standby_cloudlets.empty()) {
                        total_comm_kb += req.state_buffer_kb;
                    }
                }
            }

            if (d < INF_DELAY) completed++;

            // Proactive adjustment for subsequent slots
            if (tau < num_slots - 1 && i + per_slot < (int)reqs.size()) {
                double next_rate = reqs[std::min(i + per_slot, (int)reqs.size() - 1)].data_rate;
                sim->algorithmAdj(req, tau, next_rate);
            }
        }
    }

    double avg_delay = (total_count > 0) ? total_delay / total_count : 0;
    if (out_queue_util) *out_queue_util = (total_count > 0) ? (double)failure_count / total_count : 0;
    if (out_recovery_freq) *out_recovery_freq = recovery_count;
    if (out_completeness) *out_completeness = (total_count > 0) ? (double)completed / total_count : 0;
    if (out_availability) *out_availability = (total_count > 0) ? 1.0 - (double)failure_count / total_count : 1.0;
    if (out_recovery_latency) *out_recovery_latency = (recovery_count > 0) ? total_recovery_delay / recovery_count : 0;
    if (out_comm_kb) *out_comm_kb = total_comm_kb;

    return avg_delay;
}

// ── Exp1: Sensor Scalability ─────────────────────────────────────────────
SimResult FTServerlessEdgeSim::runExp1_SensorScalability() {
    int n_sensors = (int)cfg_.variable_value;
    int n_cl = cfg_.num_cloudlets > 0 ? cfg_.num_cloudlets : 10;
    rng_.seed(cfg_.seed);
    buildSECNetwork(n_cl);

    std::vector<DSPRequest> reqs;
    for (int i = 0; i < n_sensors; i++) {
        auto& row = dataset_[i % dataset_.size()];
        reqs.push_back(createRequest(i, i % 100, row.fog_node_id,
            row.bytes_transferred, row.packet_size, row.is_failure));
    }
    double avg = runBatch(this, reqs, n_cl, -1, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    return {cfg_.variable_value, avg, 0, 0};
}

// ── Exp2: Fog Scalability ────────────────────────────────────────────────
SimResult FTServerlessEdgeSim::runExp2_FogScalability() {
    int n_cl = (int)cfg_.variable_value;
    int n_sensors = cfg_.num_sensors > 0 ? cfg_.num_sensors : 1000;
    rng_.seed(cfg_.seed);
    buildSECNetwork(n_cl);

    std::vector<DSPRequest> reqs;
    for (int i = 0; i < n_sensors; i++) {
        auto& row = dataset_[i % dataset_.size()];
        reqs.push_back(createRequest(i, i % 100, row.fog_node_id,
            row.bytes_transferred, row.packet_size, row.is_failure));
    }
    double avg = runBatch(this, reqs, n_cl, -1, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    return {cfg_.variable_value, avg, 0, 0};
}

// ── Exp3: Workload Intensity ─────────────────────────────────────────────
SimResult FTServerlessEdgeSim::runExp3_WorkloadIntensity() {
    double rate_mult = cfg_.variable_value; // reporting rate multiplier
    int n_cl = cfg_.num_cloudlets > 0 ? cfg_.num_cloudlets : 10;
    int n_sensors = cfg_.num_sensors > 0 ? cfg_.num_sensors : 1000;
    rng_.seed(cfg_.seed);
    buildSECNetwork(n_cl);

    // Scale the number of requests by the workload multiplier.
    // Higher workload intensity = more sensor reports per aggregation epoch.
    int effective_sensors = static_cast<int>(n_sensors * rate_mult);

    std::vector<DSPRequest> reqs;
    for (int i = 0; i < effective_sensors; i++) {
        auto& row = dataset_[i % dataset_.size()];
        // Also scale data rate with workload to model heavier per-request processing
        double scaled_rate = row.bytes_transferred * (rate_mult / 1.0);
        reqs.push_back(createRequest(i, i % 100, row.fog_node_id,
            scaled_rate, row.packet_size, row.is_failure));
    }
    double queue_util = 0, recovery_freq = 0;
    double avg = runBatch(this, reqs, n_cl, -1, &queue_util, &recovery_freq,
                          nullptr, nullptr, nullptr, nullptr);
    return {cfg_.variable_value, avg, queue_util, recovery_freq};
}

// ── Exp4: Failure Rate ───────────────────────────────────────────────────
SimResult FTServerlessEdgeSim::runExp4_FailureRate() {
    double fail_rate = cfg_.variable_value; // Already a fraction (0.02-0.20)
    int n_cl = cfg_.num_cloudlets > 0 ? cfg_.num_cloudlets : 10;
    int n_sensors = cfg_.num_sensors > 0 ? cfg_.num_sensors : 1000;
    rng_.seed(cfg_.seed);
    buildSECNetwork(n_cl);

    // Override function failure probabilities
    std::vector<DSPRequest> reqs;
    for (int i = 0; i < n_sensors; i++) {
        auto& row = dataset_[i % dataset_.size()];
        auto req = createRequest(i, i % 100, row.fog_node_id,
            row.bytes_transferred, row.packet_size, false);
        for (auto& fn : req.functions)
            fn.failure_prob = fail_rate;
        reqs.push_back(std::move(req));
    }
    double completeness = 0, availability = 0, rec_lat = 0;
    runBatch(this, reqs, n_cl, fail_rate, nullptr, nullptr,
             &completeness, &availability, &rec_lat, nullptr);
    return {cfg_.variable_value, rec_lat, completeness, availability};
}

// ── Exp5: Loss Exposure ──────────────────────────────────────────────────
SimResult FTServerlessEdgeSim::runExp5_LossExposure() {
    int micro_slots = (int)cfg_.variable_value;
    int n_cl = cfg_.num_cloudlets > 0 ? cfg_.num_cloudlets : 10;
    int n_sensors = cfg_.num_sensors > 0 ? cfg_.num_sensors : 1000;
    rng_.seed(cfg_.seed);
    buildSECNetwork(n_cl);

    std::vector<DSPRequest> reqs;
    for (int i = 0; i < n_sensors; i++) {
        auto& row = dataset_[i % dataset_.size()];
        reqs.push_back(createRequest(i, i % 100, row.fog_node_id,
            row.bytes_transferred, row.packet_size, row.is_failure));
    }

    // Place all requests
    for (auto& req : reqs) algorithmFwk(req);

    // Loss exposure: fraction of DAG execution unprotected between adjustments
    // With K micro-slots, adjustment happens every 1/K of the execution window
    // Loss exposure = average unprotected fraction across requests
    double total_loss = 0.0;
    for (auto& req : reqs) {
        int nf = (int)req.functions.size();
        int unprotected = 0;
        for (auto& fn : req.functions) {
            // A function is "exposed" if it has no standby OR standby is stale
            // With more micro-slots, standbys are refreshed more frequently
            double protection_prob = 1.0 - std::pow(0.5, micro_slots);
            if (fn.standby_cloudlets.empty() || randDouble(0, 1) > protection_prob)
                unprotected++;
        }
        total_loss += (double)unprotected / nf;
    }
    double avg_loss = total_loss / reqs.size();
    return {cfg_.variable_value, avg_loss, 0, 0};
}

// ── Exp6: Recovery Communication ─────────────────────────────────────────
SimResult FTServerlessEdgeSim::runExp6_RecoveryComm() {
    int incomplete_slots = (int)cfg_.variable_value;
    int n_cl = cfg_.num_cloudlets > 0 ? cfg_.num_cloudlets : 10;
    int n_sensors = cfg_.num_sensors > 0 ? cfg_.num_sensors : 1000;
    rng_.seed(cfg_.seed);
    buildSECNetwork(n_cl);

    std::vector<DSPRequest> reqs;
    for (int i = 0; i < n_sensors; i++) {
        auto& row = dataset_[i % dataset_.size()];
        reqs.push_back(createRequest(i, i % 100, row.fog_node_id,
            row.bytes_transferred, row.packet_size, true)); // force failure
    }

    // Place all requests
    for (auto& req : reqs) algorithmFwk(req);

    // Communication overhead = state buffer transfers during recovery
    // More incomplete slots → more state that needs to be re-transferred
    double total_comm_kb = 0.0;
    for (auto& req : reqs) {
        for (auto& fn : req.functions) {
            if (fn.standby_cloudlets.empty()) continue;
            // Each incomplete slot requires transferring accumulated state
            double state_per_slot = req.state_buffer_kb;
            // Total = incomplete_slots * state_buffer * num_affected_functions
            total_comm_kb += incomplete_slots * state_per_slot;
        }
    }
    // Average per request
    double avg_comm_kb = total_comm_kb / reqs.size();
    return {cfg_.variable_value, avg_comm_kb, 0, 0};
}
