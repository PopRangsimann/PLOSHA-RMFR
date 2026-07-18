// Experiment 9: Scheduling Efficiency — FedDQN (Ref[22])
// Sweeps number of fog nodes from 5 to 50
// Measures: scheduling_latency_ms, workload_imbalance
// Output: exp2_scheduling_efficiency/results.csv

#include "fed_dqn_sim.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cmath>

int main() {
    const std::string dataset_path = "../../../dataset/plosha_dataset.csv";
    const std::string output_path = "../exp2_scheduling_efficiency/results.csv";
    const int NUM_SENSORS = 12600;
    const int NUM_VMS_PER_NODE = 4;
    const int NUM_EPISODES = 30;

    const int fog_node_counts[] = {5, 10, 15, 20, 25, 30, 35, 40, 45, 50};
    const int num_points = sizeof(fog_node_counts) / sizeof(fog_node_counts[0]);

    std::cout << "=== FedDQN — Experiment 9: Scheduling Efficiency ===" << std::endl;

    FedDQNSimulation sim;
    if (!sim.LoadDataset(dataset_path)) {
        std::cerr << "Failed to load dataset." << std::endl;
        return 1;
    }

    // Create output directory
    system("mkdir -p ../exp2_scheduling_efficiency");

    std::ofstream out(output_path);
    if (!out.is_open()) {
        std::cerr << "Failed to open output: " << output_path << std::endl;
        return 1;
    }
    out << "num_fog_nodes,scheduling_latency_ms,workload_imbalance,scheduling_comm_kb,convergence_time_epochs" << std::endl;

    for (int i = 0; i < num_points; i++) {
        int n_fog = fog_node_counts[i];
        int num_epochs = (n_fog >= 100) ? 10 : NUM_EPISODES;
        int num_sensors = n_fog * 100;
        std::cout << "\n--- Fog Nodes: " << n_fog << " ---" << std::endl;

        sim.Configure(n_fog, NUM_VMS_PER_NODE, num_sensors, num_epochs, 0.0);
        sim.SetHyperparameters();

        FedDQNMetrics m = sim.Run();
        
        // FedDQN requires synchronizing DQN weights (approx 100KB per node per episode)
        double comm_kb = n_fog * 100.0;
        // R11 FIX: previously a hardcoded 5.0 literal ("RL typically takes
        // ~4-6 episodes to converge"), never computed from simulation
        // state. Now read from the real per-episode measurement in Run().
        double convergence_epochs = m.convergence_time_epochs;

        std::cout << "Scheduling latency: " << std::fixed << std::setprecision(6)
                  << m.scheduling_latency_ms << " ms" << std::endl;
        std::cout << "Workload imbalance: " << m.workload_imbalance << std::endl;

        out << n_fog << "," << std::fixed << std::setprecision(6)
            << m.scheduling_latency_ms << ","
            << m.workload_imbalance << ","
            << comm_kb << ","
            << convergence_epochs << std::endl;
    }

    out.close();
    std::cout << "\nResults written to " << output_path << std::endl;
    return 0;
}
