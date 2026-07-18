// Experiment 1: Sensor Scalability — Robust IIoT (Ref[24])
// Sweeps number of sensors from 500 to 5000
// Measures: aggregation latency (ms)
// Output: exp1_sensor_scalability/results.csv

#include "robust_iiot_sim.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>

int main() {
    const std::string dataset_path = "../../../dataset/plosha_dataset.csv";
    const std::string output_path = "../exp1_sensor_scalability/results.csv";
    const int NUM_EDGE_SERVERS = 5;
    const int PAILLIER_BITS = 1024;
    const double DP_EPSILON = 1.0;

    // Sensor counts to sweep
    const int sensor_counts[] = {500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000};
    const int num_points = sizeof(sensor_counts) / sizeof(sensor_counts[0]);

    std::cout << "=== Robust IIoT — Experiment 1: Sensor Scalability ===" << std::endl;

    RobustIIoTSimulation sim;
    if (!sim.LoadDataset(dataset_path)) {
        std::cerr << "Failed to load dataset." << std::endl;
        return 1;
    }

    std::ofstream out(output_path);
    if (!out.is_open()) {
        std::cerr << "Failed to open output: " << output_path << std::endl;
        return 1;
    }
    out << "variable_value,primary_metric,secondary_metric_1,secondary_metric_2" << std::endl;

    for (int i = 0; i < num_points; i++) {
        int n_sensors = sensor_counts[i];
        std::cout << "\n--- Sensors: " << n_sensors << " ---" << std::endl;

        sim.Initialize(n_sensors, NUM_EDGE_SERVERS, PAILLIER_BITS, DP_EPSILON);
        sim.RunRegistration();

        const int NUM_EPISODES = 30;
        double total_ms_accum = 0;
        AggregationTimings avg_t = {0,0,0,0,0,0,0,0,0};

        for (int e = 0; e < NUM_EPISODES; ++e) {
            AggregationTimings t = sim.RunAggregationRound(1);
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

        // Average
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

        out << n_sensors << "," << std::fixed << std::setprecision(4)
            << avg_t.total_ms << ",," << std::endl;
    }

    out.close();
    std::cout << "\nResults written to " << output_path << std::endl;
    return 0;
}
