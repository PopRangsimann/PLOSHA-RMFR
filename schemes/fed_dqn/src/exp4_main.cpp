// Experiment 4: Failure Rate — FedDQN (Ref[22])
// Sweeps fog-node failure rate from 2% to 20%
// Measures: recovery latency (ms), aggregation completeness, system availability
// Output: exp3_failure_rate/results.csv

#include "fed_dqn_sim.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>

int main() {
    const std::string dataset_path = "../../../dataset/plosha_dataset.csv";
    const std::string output_path = "../exp3_failure_rate/results.csv";
    const int NUM_FOG_NODES = 10;
    const int NUM_VMS_PER_NODE = 4;
    const int NUM_SENSORS = 2000;
    const int NUM_EPISODES = 10;

    // Failure rates to sweep (2% to 20%)
    const double failure_rates[] = {0.02, 0.04, 0.06, 0.08, 0.10, 0.12, 0.14, 0.16, 0.18, 0.20};
    const int num_points = sizeof(failure_rates) / sizeof(failure_rates[0]);

    std::cout << "=== FedDQN — Experiment 4: Failure Rate ===" << std::endl;

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
        double fail_rate = failure_rates[i];
        std::cout << "\n--- Failure Rate: " << fail_rate * 100 << "% ---" << std::endl;

        sim.Configure(NUM_FOG_NODES, NUM_VMS_PER_NODE, NUM_SENSORS, NUM_EPISODES, fail_rate);
        sim.SetHyperparameters();

        FedDQNMetrics m = sim.Run();

        // Recovery latency: aggregation_latency includes recovery overhead
        // Aggregation completeness: 1 - rejection_rate
        double agg_completeness = 1.0 - m.task_rejection_rate;
        // System availability: 1 - (failure_rate * impact)
        double sys_availability = 1.0 - m.task_rejection_rate;

        std::cout << "Recovery latency: " << std::fixed << std::setprecision(2)
                  << m.aggregation_latency_ms << " ms" << std::endl;
        std::cout << "Aggregation completeness: " << agg_completeness << std::endl;
        std::cout << "System availability: " << sys_availability << std::endl;
        std::cout << "Recoveries: " << m.recovery_count << std::endl;

        // Output: variable = failure_rate (fraction), primary = recovery_latency_ms,
        //         secondary_1 = aggregation_completeness, secondary_2 = system_availability
        out << std::fixed << std::setprecision(6) << fail_rate << ","
            << std::setprecision(4) << m.recovery_latency_ms << ","
            << agg_completeness << ","
            << sys_availability << std::endl;
    }

    out.close();
    std::cout << "\nResults written to " << output_path << std::endl;
    return 0;
}
