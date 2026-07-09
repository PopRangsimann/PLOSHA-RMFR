// Experiment 2: Fog Node Scalability — FedDQN (Ref[22])
// Sweeps number of fog nodes from 5 to 50
// Measures: aggregation latency (ms)
// Output: exp2_fog_scalability/results.csv

#include "fed_dqn_sim.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>

int main() {
    const std::string dataset_path = "../../../dataset/plosha_dataset.csv";
    const std::string output_path = "../exp2_fog_scalability/results.csv";
    const int NUM_SENSORS = 2000;
    const int NUM_VMS_PER_NODE = 4;
    const int NUM_EPISODES = 5;

    const int fog_node_counts[] = {5, 10, 15, 20, 25, 30, 35, 40, 45, 50};
    const int num_points = sizeof(fog_node_counts) / sizeof(fog_node_counts[0]);

    std::cout << "=== FedDQN — Experiment 2: Fog Node Scalability ===" << std::endl;

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
        int n_fog = fog_node_counts[i];
        std::cout << "\n--- Fog Nodes: " << n_fog << " ---" << std::endl;

        sim.Configure(n_fog, NUM_VMS_PER_NODE, NUM_SENSORS, NUM_EPISODES, 0.0);
        sim.SetHyperparameters();

        FedDQNMetrics m = sim.Run();

        std::cout << "Aggregation latency: " << std::fixed << std::setprecision(2)
                  << m.aggregation_latency_ms << " ms" << std::endl;
        std::cout << "Makespan: " << m.makespan_ms << " ms" << std::endl;

        out << n_fog << "," << std::fixed << std::setprecision(4)
            << m.aggregation_latency_ms << ",," << std::endl;
    }

    out.close();
    std::cout << "\nResults written to " << output_path << std::endl;
    return 0;
}
