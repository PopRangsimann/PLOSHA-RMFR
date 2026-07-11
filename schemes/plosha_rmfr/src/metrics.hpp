#ifndef PLOSHA_METRICS_HPP
#define PLOSHA_METRICS_HPP

#include <string>
#include <vector>
#include <fstream>

namespace plosha {

// One row of metrics collected per epoch
struct EpochMetrics {
    double aggregation_latency_ms = 0.0;
    double recovery_latency_ms = 0.0;
    double aggregation_completeness = 0.0;
    double loss_exposure_fraction = 0.0;
    double communication_overhead_KB = 0.0;
    double system_availability = 0.0;
    double queue_utilization = 0.0;
    int recovery_frequency = 0;
    double scheduling_latency_ms = 0.0;
    double workload_imbalance = 0.0;
    double processing_overhead_ms = 0.0;
};

// Averaged metrics written as one CSV row per sweep point
struct SweepPointResult {
    double variable_value = 0.0;
    double avg_aggregation_latency = 0.0;
    double avg_recovery_latency = 0.0;
    double avg_aggregation_completeness = 0.0;
    double avg_loss_exposure = 0.0;
    double avg_communication_overhead = 0.0;
    double avg_system_availability = 0.0;
    double avg_queue_utilization = 0.0;
    double avg_recovery_frequency = 0.0;
    double avg_scheduling_latency = 0.0;
    double avg_workload_imbalance = 0.0;
    double avg_processing_overhead = 0.0;
};

class MetricsCollector {
public:
    // Record one epoch's metrics
    void recordEpoch(const EpochMetrics& m);

    // Compute averages across all recorded epochs
    SweepPointResult computeAverages(double variable_value) const;

    // Reset for next sweep point
    void reset();

    // Write results.csv header
    static void writeCSVHeader(std::ofstream& file, const std::string& variable_name);

    // Write one row to results.csv
    static void writeCSVRow(std::ofstream& file, const SweepPointResult& result);

    // Convenience: write full results file for an experiment
    static void writeResultsFile(const std::string& filepath,
                                  const std::string& variable_name,
                                  const std::vector<SweepPointResult>& results);

    // Write ablation results (variant column format)
    struct AblationRow {
        std::string variant;
        double num_sensors;
        double aggregation_latency_ms;
        double processing_overhead_ms;
        double loss_exposure_fraction;
    };
    static void writeAblationResultsFile(const std::string& filepath,
                                          const std::vector<AblationRow>& rows);

    // Write scheduling results (2-metric format)
    static void writeSchedulingResultsFile(const std::string& filepath,
                                            const std::string& variable_name,
                                            const std::vector<SweepPointResult>& results);

private:
    std::vector<EpochMetrics> epoch_records_;
};

} // namespace plosha

#endif // PLOSHA_METRICS_HPP
