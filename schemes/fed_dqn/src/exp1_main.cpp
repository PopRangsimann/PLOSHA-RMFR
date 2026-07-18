// Experiment 1: Sensor Scalability — FedDQN (Ref[22])
// Sweeps number of sensors (tasks) from 500 to 5000
// Measures: aggregation latency (ms)
// Output: exp1_sensor_scalability/results.csv

#include "fed_dqn_sim.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>

int main() {
    const std::string dataset_path = "../../../dataset/plosha_dataset.csv";
    const std::string output_path = "../exp1_sensor_scalability/results.csv";
    const int NUM_FOG_NODES = 10;
    const int NUM_VMS_PER_NODE = 4;
    const int NUM_EPISODES = 30;

    const int sensor_counts[] = {500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000};
    const int num_points = sizeof(sensor_counts) / sizeof(sensor_counts[0]);

    std::cout << "=== FedDQN — Experiment 1: Sensor Scalability ===" << std::endl;

    FedDQNSimulation sim;
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

        sim.Configure(NUM_FOG_NODES, NUM_VMS_PER_NODE, n_sensors, NUM_EPISODES, 0.0);
        sim.SetHyperparameters();

        FedDQNMetrics m = sim.Run();

        std::cout << "Aggregation latency: " << std::fixed << std::setprecision(2)
                  << m.aggregation_latency_ms << " ms" << std::endl;
        std::cout << "Makespan: " << m.makespan_ms << " ms" << std::endl;
        std::cout << "Throughput: " << m.throughput << std::endl;
        std::cout << "SLA violations: " << m.sla_violation_rate * 100 << "%" << std::endl;
        std::cout << "Rejections: " << m.task_rejection_rate * 100 << "%" << std::endl;

        out << n_sensors << "," << std::fixed << std::setprecision(4)
            << m.aggregation_latency_ms << ",," << std::endl;
    }

    out.close();
    std::cout << "\nResults written to " << output_path << std::endl;
    return 0;
}
