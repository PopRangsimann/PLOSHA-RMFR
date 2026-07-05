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

        // Run aggregation with varying workload
        AggregationTimings t = sim.RunAggregationRound(readings);

        // Queue utilization: ratio of total ciphertexts processed to capacity
        // For Robust IIoT, "capacity" is all sensors * readings
        double total_ops = static_cast<double>(NUM_SENSORS * readings);
        double queue_util = total_ops / (NUM_SENSORS * 20.0); // normalize to max workload

        std::cout << "Total aggregation latency: " << std::fixed << std::setprecision(2)
                  << t.total_ms << " ms" << std::endl;
        std::cout << "Queue utilization: " << queue_util << std::endl;

        // Output: variable = readings_per_sensor, primary = latency_ms,
        //         secondary_1 = queue_utilization, secondary_2 = (empty, no recovery in this scheme)
        out << readings << "," << std::fixed << std::setprecision(4)
            << t.total_ms << "," << queue_util << "," << std::endl;
    }

    out.close();
    std::cout << "\nResults written to " << output_path << std::endl;
    return 0;
}
