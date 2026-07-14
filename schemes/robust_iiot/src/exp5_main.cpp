// Experiment 5: Loss Exposure — Robust IIoT (Ref[24])
// Sweeps number of micro-slots from 1 to 20
// Measures: loss exposure fraction (fraction of data lost when one slot fails)
// Output: exp4_loss_exposure/results.csv
//
// NOTE: Robust IIoT does NOT have a micro-slot architecture.
// It performs monolithic aggregation at the ES level.
// This experiment quantifies how much data is lost when a failure occurs
// during aggregation, compared to PLOSHA's micro-slot isolation.
// With monolithic aggregation (1 slot), any failure loses ALL data.
// We subdivide into virtual "micro-slots" to show what WOULD happen
// if the scheme supported partial aggregation.

#include "robust_iiot_sim.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>
#include <chrono>

int main() {
    const std::string dataset_path = "../../../dataset/plosha_dataset.csv";
    const std::string output_path = "../exp4_loss_exposure/results.csv";
    const int NUM_SENSORS = 1000;
    const int NUM_EDGE_SERVERS = 5;
    const int PAILLIER_BITS = 1024;
    const double DP_EPSILON = 1.0;
    const int FAILED_SLOTS = 1; // One micro-slot fails

    // Micro-slot counts to sweep
    const int micro_slot_counts[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 16, 18, 20};
    const int num_points = sizeof(micro_slot_counts) / sizeof(micro_slot_counts[0]);

    std::cout << "=== Robust IIoT — Experiment 5: Loss Exposure ===" << std::endl;

    RobustIIoTSimulation sim;
    if (!sim.LoadDataset(dataset_path)) {
        std::cerr << "Failed to load dataset." << std::endl;
        return 1;
    }

    sim.Initialize(NUM_SENSORS, NUM_EDGE_SERVERS, PAILLIER_BITS, DP_EPSILON);

    std::ofstream out(output_path);
    if (!out.is_open()) {
        std::cerr << "Failed to open output: " << output_path << std::endl;
        return 1;
    }
    out << "variable_value,primary_metric,secondary_metric_1,secondary_metric_2" << std::endl;

    for (int i = 0; i < num_points; i++) {
        int m_slots = micro_slot_counts[i];

        // Measure real crypto overhead for this configuration
        // (to ensure timing is authentic, run actual aggregation)
        auto t_start = std::chrono::high_resolution_clock::now();

        double loss_fraction = sim.RunLossExposure(m_slots, FAILED_SLOTS);

        auto t_end = std::chrono::high_resolution_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

        std::cout << "Micro-slots: " << m_slots
                  << " | Loss fraction: " << std::fixed << std::setprecision(4) << loss_fraction
                  << " | Time: " << elapsed_ms << " ms" << std::endl;

        // Output: variable = micro_slots, primary = loss_exposure_fraction
        out << m_slots << "," << std::fixed << std::setprecision(6)
            << loss_fraction << ",," << std::endl;
    }

    out.close();
    std::cout << "\nResults written to " << output_path << std::endl;
    return 0;
}
