// Experiment 3: Workload Intensity — FedDQN (Ref[22])
// Sweeps sensor reporting rate (effective task multiplier)
// Measures: aggregation latency (ms), queue utilization, recovery frequency
// Output: exp3_workload_intensity/results.csv

#include "fed_dqn_sim.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>

int main() {
    const std::string dataset_path = "../../../dataset/plosha_dataset.csv";
    const std::string output_path = "../exp3_workload_intensity/results.csv";
    const int NUM_FOG_NODES = 10;
    const int NUM_VMS_PER_NODE = 4;
    const int NUM_EPISODES = 5;

    // Workload intensity: multiply effective tasks
    // Base = 1000 sensors, multiplied by intensity factor
    const int base_sensors = 1000;
    const double intensity_levels[] = {0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0};
    const int num_points = sizeof(intensity_levels) / sizeof(intensity_levels[0]);

    std::cout << "=== FedDQN — Experiment 3: Workload Intensity ===" << std::endl;

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
        double intensity = intensity_levels[i];
        int effective_tasks = static_cast<int>(base_sensors * intensity);

        std::cout << "\n--- Workload Intensity: " << intensity
                  << "x (" << effective_tasks << " tasks) ---" << std::endl;

        sim.Configure(NUM_FOG_NODES, NUM_VMS_PER_NODE, effective_tasks, NUM_EPISODES, 0.0);
        sim.SetHyperparameters();

        FedDQNMetrics m = sim.Run();

        std::cout << "Aggregation latency: " << std::fixed << std::setprecision(2)
                  << m.aggregation_latency_ms << " ms" << std::endl;
        std::cout << "Queue utilization: " << m.avg_queue_utilization << std::endl;

        // Output: variable = intensity, primary = latency,
        //         secondary_1 = queue_util, secondary_2 = recovery_count
        out << std::fixed << std::setprecision(1) << intensity << ","
            << std::setprecision(4) << m.aggregation_latency_ms << ","
            << m.avg_queue_utilization << ","
            << m.recovery_count << std::endl;
    }

    out.close();
    std::cout << "\nResults written to " << output_path << std::endl;
    return 0;
}
