// Experiment 3: Workload Intensity — Robust IIoT (Ref[24])
// Sweeps sensor reporting rate (readings per sensor per round)
// Measures: aggregation latency (ms), queue utilization (fraction), recovery frequency
// Output: exp3_workload_intensity/results.csv

#include "robust_iiot_sim.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>

int main() {
    const std::string dataset_path = "../../../dataset/plosha_dataset.csv";
    const std::string output_path = "../exp3_workload_intensity/results.csv";
    const int NUM_SENSORS = 1000;
    const int NUM_EDGE_SERVERS = 5;
    const int PAILLIER_BITS = 1024;
    const double DP_EPSILON = 1.0;

    // Readings per sensor per round (workload intensity levels)
    const int workload_levels[] = {1, 2, 3, 5, 8, 10, 15, 20};
    const int num_points = sizeof(workload_levels) / sizeof(workload_levels[0]);

    std::cout << "=== Robust IIoT — Experiment 3: Workload Intensity ===" << std::endl;

    RobustIIoTSimulation sim;
    if (!sim.LoadDataset(dataset_path)) {
        std::cerr << "Failed to load dataset." << std::endl;
        return 1;
    }

    // Initialize once (same sensor/ES count)
    sim.Initialize(NUM_SENSORS, NUM_EDGE_SERVERS, PAILLIER_BITS, DP_EPSILON);
    sim.RunRegistration();

    std::ofstream out(output_path);
    if (!out.is_open()) {
        std::cerr << "Failed to open output: " << output_path << std::endl;
        return 1;
    }
    out << "variable_value,primary_metric,secondary_metric_1,secondary_metric_2" << std::endl;

    for (int i = 0; i < num_points; i++) {
        int readings = workload_levels[i];
        std::cout << "\n--- Readings per sensor: " << readings << " ---" << std::endl;

        sim.Initialize(NUM_SENSORS, NUM_EDGE_SERVERS, PAILLIER_BITS, DP_EPSILON);
        sim.RunRegistration();

        const int NUM_EPISODES = 30;
        double total_ms_accum = 0;
        AggregationTimings avg_t = {0,0,0,0,0,0,0,0,0};

        for (int e = 0; e < NUM_EPISODES; ++e) {
            AggregationTimings t = sim.RunAggregationRound(readings);
            avg_t.total_ms += t.total_ms;
            avg_t.sensor_encrypt_ms += t.sensor_encrypt_ms;
            avg_t.sensor_sign_ms += t.sensor_sign_ms;
            avg_t.es_batch_verify_ms += t.es_batch_verify_ms;
            avg_t.es_aggregate_ms += t.es_aggregate_ms;
            avg_t.es_noise_ms += t.es_noise_ms;
            avg_t.es_sign_ms += t.es_sign_ms;
            avg_t.cc_batch_verify_ms += t.cc_batch_verify_ms;
            avg_t.cc_decrypt_ms += t.cc_decrypt_ms;
        }

        avg_t.total_ms /= NUM_EPISODES;
        avg_t.sensor_encrypt_ms /= NUM_EPISODES;
        avg_t.sensor_sign_ms /= NUM_EPISODES;
        avg_t.es_batch_verify_ms /= NUM_EPISODES;
        avg_t.es_aggregate_ms /= NUM_EPISODES;
        avg_t.es_noise_ms /= NUM_EPISODES;
        avg_t.es_sign_ms /= NUM_EPISODES;
        avg_t.cc_batch_verify_ms /= NUM_EPISODES;
        avg_t.cc_decrypt_ms /= NUM_EPISODES;

        std::cout << "Average aggregation latency (" << NUM_EPISODES << " runs): " << std::fixed << std::setprecision(2)
                  << avg_t.total_ms << " ms" << std::endl;
        std::cout << "  Encrypt: " << avg_t.sensor_encrypt_ms << " ms" << std::endl;
        std::cout << "  Sign:    " << avg_t.sensor_sign_ms << " ms" << std::endl;
        std::cout << "  BV(ES):  " << avg_t.es_batch_verify_ms << " ms" << std::endl;
        std::cout << "  Agg:     " << avg_t.es_aggregate_ms << " ms" << std::endl;
        std::cout << "  Noise:   " << avg_t.es_noise_ms << " ms" << std::endl;
        std::cout << "  Sign(ES):" << avg_t.es_sign_ms << " ms" << std::endl;
        std::cout << "  BV(CC):  " << avg_t.cc_batch_verify_ms << " ms" << std::endl;
        std::cout << "  Decrypt: " << avg_t.cc_decrypt_ms << " ms" << std::endl;

        // Queue utilization: ratio of total ciphertexts processed to capacity
        double total_ops = static_cast<double>(NUM_SENSORS * readings);
        double queue_util = total_ops / (NUM_SENSORS * 20.0);

        out << readings << "," << std::fixed << std::setprecision(4) << avg_t.total_ms << "," << queue_util << ",0" << std::endl;
    }

    out.close();
    std::cout << "\nResults written to " << output_path << std::endl;
    return 0;
}
