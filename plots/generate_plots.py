import os
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
from pathlib import Path

# Set up matplotlib style to match the template
plt.rcParams['font.family'] = 'serif'
plt.rcParams['font.size'] = 12
plt.rcParams['axes.labelsize'] = 14
plt.rcParams['legend.fontsize'] = 10
plt.rcParams['legend.framealpha'] = 0.9

# Scheme definitions (matching the directories)
SCHEMES = {
    'plosha_rmfr': {
        'label': 'PLOSHA-RMFR (Ours)',
        'color': '#1f77b4',  # Blue
        'marker': 'o'
    },
    'fed_dqn': {
        'label': 'Ref[22]',
        'color': '#ff7f0e',  # Orange
        'marker': 's'
    },
    'robust_iiot': {
        'label': 'Ref[24]',
        'color': '#d62728',  # Red
        'marker': 'D'
    },
    'fault_tolerant_workflow': {
        'label': 'Ref[37]',
        'color': '#2ca02c',  # Green
        'marker': '^'
    },
    'ft_serverless_edge': {
        'label': 'Ref[38]',
        'color': '#9467bd',  # Purple
        'marker': 'v'
    }
}

SCRIPT_DIR = Path(__file__).resolve().parent
BASE_DIR = SCRIPT_DIR.parent / 'schemes'
OUTPUT_DIR = SCRIPT_DIR / 'output'

def setup_axes(ax):
    """Apply template styling to axes."""
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)
    ax.grid(True, linestyle='--', alpha=0.5, color='#d3d3d3')
    ax.tick_params(direction='out')

def get_data(exp_dir_name):
    """Load data for a given experiment directory name across all schemes."""
    data = {}
    for scheme_id in SCHEMES.keys():
        # Special handling for PLOSHA native vs TEE
        if scheme_id == 'plosha_rmfr':
            # Use the _native backup if it exists (fair comparison with baselines)
            native_path = BASE_DIR / 'plosha_rmfr' / f'{exp_dir_name}_native' / 'results.csv'
            sgx_path = BASE_DIR / 'plosha_rmfr' / exp_dir_name / 'results.csv'
            csv_path = native_path if native_path.exists() else sgx_path
        elif scheme_id == 'plosha_rmfr_tee':
            # TEE line reads from the main directory (contains SGX results)
            csv_path = BASE_DIR / 'plosha_rmfr' / exp_dir_name / 'results.csv'
            # Only include if native backup also exists (proves SGX actually ran)
            native_path = BASE_DIR / 'plosha_rmfr' / f'{exp_dir_name}_native' / 'results.csv'
            if not native_path.exists():
                continue
        else:
            csv_path = BASE_DIR / scheme_id / exp_dir_name / 'results.csv'

        if csv_path.exists():
            try:
                df = pd.read_csv(csv_path)
                # Map generic columns to PLOSHA expected columns based on experiment name
                if 'variable_value' in df.columns:
                    if exp_dir_name == 'exp1_sensor_scalability':
                        df = df.rename(columns={'variable_value': 'num_sensors', 'primary_metric': 'aggregation_latency_ms'})
                    elif exp_dir_name == 'exp2_fog_scalability':
                        df = df.rename(columns={'variable_value': 'num_fog_nodes', 'primary_metric': 'aggregation_latency_ms'})
                    elif exp_dir_name == 'exp3_workload_intensity':
                        df = df.rename(columns={'variable_value': 'workload_multiplier', 'primary_metric': 'aggregation_latency_ms'})
                    elif exp_dir_name == 'exp4_failure_rate':
                        df = df.rename(columns={'variable_value': 'failure_rate', 'primary_metric': 'recovery_latency_ms', 'secondary_metric_1': 'aggregation_completeness'})
                    elif exp_dir_name == 'exp5_loss_exposure':
                        df = df.rename(columns={'variable_value': 'micro_slots', 'primary_metric': 'loss_exposure_fraction'})
                    elif exp_dir_name == 'exp6_recovery_comm':
                        df = df.rename(columns={'variable_value': 'incomplete_micro_slots', 'primary_metric': 'communication_overhead_KB'})
                data[scheme_id] = df
            except Exception as e:
                print(f"Error reading {csv_path}: {e}")
    return data

def plot_experiment(exp_name, x_col, y_col, x_label, y_label, output_filename,
                    x_scale='linear', y_scale='linear', legend_loc='upper left',
                    x_lim=None):
    """Generate a plot for a specific experiment and metric."""
    data = get_data(exp_name)
    if not data:
        print(f"No data found for experiment {exp_name}")
        return

    fig, ax = plt.subplots(figsize=(8, 5), dpi=300)

    for scheme_id, df in data.items():
        if x_col in df.columns and y_col in df.columns:
            plot_df = df
            # Clip data to x_lim range if specified
            if x_lim is not None:
                plot_df = df[(df[x_col] >= x_lim[0]) & (df[x_col] <= x_lim[1])]
            scheme_info = SCHEMES[scheme_id]
            ms = scheme_info.get('markersize', 8)
            ax.plot(plot_df[x_col], plot_df[y_col],
                    label=scheme_info['label'],
                    color=scheme_info['color'],
                    marker=scheme_info['marker'],
                    linewidth=2.5 if 'tee' not in scheme_id else 2.0,
                    linestyle='-' if 'tee' not in scheme_id else '--',
                    markersize=ms,
                    zorder=5 if scheme_id in ('plosha_rmfr', 'plosha_rmfr_tee') else 3)

    ax.set_xlabel(x_label)
    ax.set_ylabel(y_label)
    ax.set_xscale(x_scale)
    ax.set_yscale(y_scale)
    if x_lim is not None:
        ax.set_xlim(x_lim)
    
    # Legend ordering: we want other references first, then ours at the bottom
    handles, labels = ax.get_legend_handles_labels()
    if handles:
        # Sort so Ref[X] comes first, then PLOSHA-RMFR (Ours), then PLOSHA-RMFR (TEE)
        sorted_pairs = sorted(zip(handles, labels), key=lambda pair: (2 if pair[1] == 'PLOSHA-RMFR (TEE)' else 1 if pair[1] == 'PLOSHA-RMFR (Ours)' else 0, pair[1]))
        handles_sorted, labels_sorted = zip(*sorted_pairs)
        ax.legend(handles_sorted, labels_sorted, loc=legend_loc, frameon=True)

    setup_axes(ax)
    fig.tight_layout()
    
    out_path = OUTPUT_DIR / output_filename
    fig.savefig(out_path, bbox_inches='tight')
    print(f"Generated {out_path}")
    plt.close(fig)

def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    
    print("Generating plots...")

    # Experiment 1: Sensor Scalability
    plot_experiment('exp1_sensor_scalability',
                    x_col='num_sensors', y_col='aggregation_latency_ms',
                    x_label='Number of Sensors', y_label='Aggregation Latency (ms)',
                    y_scale='log',
                    output_filename='graph1_sensor_scalability_latency.png')

    # Experiment 2: Fog Scalability
    plot_experiment('exp2_fog_scalability',
                    x_col='num_fog_nodes', y_col='aggregation_latency_ms',
                    x_label='Number of Fog Nodes', y_label='Aggregation Latency (ms)',
                    y_scale='log',
                    output_filename='graph2_fog_scalability_latency.png')

    # Experiment 3: Workload Intensity
    plot_experiment('exp3_workload_intensity',
                    x_col='workload_multiplier', y_col='aggregation_latency_ms',
                    x_label='Workload Intensity (Multiplier)', y_label='Aggregation Latency (ms)',
                    y_scale='log',
                    x_lim=(0.5, 10.5),
                    output_filename='graph3_workload_intensity_latency.png')

    # Experiment 4: Failure Rate (Fig. 5 in paper — recovery latency only)
    plot_experiment('exp4_failure_rate',
                    x_col='failure_rate', y_col='recovery_latency_ms',
                    x_label='Failure Rate', y_label='Recovery Latency (ms)',
                    output_filename='graph4_failure_rate.png')

    # Experiment 5: Loss Exposure
    plot_experiment('exp5_loss_exposure',
                    x_col='micro_slots', y_col='loss_exposure_fraction',
                    x_label='Number of Micro-slots', y_label='Loss Exposure Fraction',
                    output_filename='graph5_loss_exposure.png',
                    legend_loc='upper right')

    # Experiment 6: Recovery Communication
    plot_experiment('exp6_recovery_comm',
                    x_col='incomplete_micro_slots', y_col='communication_overhead_KB',
                    x_label='Incomplete Micro-slots', y_label='Communication Overhead (KB)',
                    output_filename='graph6_recovery_comm.png')

    # Experiment 7: AFLTO Ablation (PLOSHA only — grouped bar chart)
    plot_exp7_aflto_ablation()


def plot_exp7_aflto_ablation():
    """Generate a grouped bar chart for the AFLTO ablation study (Fig. 8).

    This is PLOSHA-only: compares aggregation completeness and system
    availability with AFLTO enabled vs. disabled.
    """
    csv_path = BASE_DIR / 'plosha_rmfr' / 'exp7_aflto_ablation' / 'results.csv'
    if not csv_path.exists():
        print(f"No data found for experiment exp7_aflto_ablation")
        return

    try:
        df = pd.read_csv(csv_path)
    except Exception as e:
        print(f"Error reading {csv_path}: {e}")
        return

    # Identify the AFLTO toggle column
    if 'aflto_enabled' in df.columns:
        toggle_col = 'aflto_enabled'
    elif 'variable_value' in df.columns:
        toggle_col = 'variable_value'
    else:
        print("exp7: cannot find AFLTO toggle column")
        return

    # Map metrics — handle both PLOSHA-native and generic CSV formats
    comp_col = 'aggregation_completeness' if 'aggregation_completeness' in df.columns else 'secondary_metric_1'
    avail_col = 'system_availability' if 'system_availability' in df.columns else 'secondary_metric_2'

    if comp_col not in df.columns or avail_col not in df.columns:
        print("exp7: missing completeness or availability columns")
        return

    # Extract values for disabled (0) and enabled (1)
    row_off = df[df[toggle_col] == 0.0].iloc[0] if len(df[df[toggle_col] == 0.0]) > 0 else None
    row_on  = df[df[toggle_col] == 1.0].iloc[0] if len(df[df[toggle_col] == 1.0]) > 0 else None

    if row_off is None or row_on is None:
        print("exp7: need both AFLTO=0 and AFLTO=1 rows")
        return

    import numpy as np

    metrics = ['Aggregation\nCompleteness', 'System\nAvailability']
    off_vals = [row_off[comp_col], row_off[avail_col]]
    on_vals  = [row_on[comp_col],  row_on[avail_col]]

    x = np.arange(len(metrics))
    bar_width = 0.30

    fig, ax = plt.subplots(figsize=(7, 5), dpi=300)

    bars_off = ax.bar(x - bar_width / 2, off_vals, bar_width,
                      label='AFLTO Disabled', color='#d62728', edgecolor='white', zorder=3)
    bars_on  = ax.bar(x + bar_width / 2, on_vals,  bar_width,
                      label='AFLTO Enabled',  color='#1f77b4', edgecolor='white', zorder=3)

    # Value labels on bars
    for bar in list(bars_off) + list(bars_on):
        height = bar.get_height()
        ax.annotate(f'{height:.3f}',
                    xy=(bar.get_x() + bar.get_width() / 2, height),
                    xytext=(0, 4), textcoords='offset points',
                    ha='center', va='bottom', fontsize=10)

    ax.set_ylabel('Score')
    ax.set_xticks(x)
    ax.set_xticklabels(metrics)
    ax.set_ylim(0, 1.15)
    ax.legend(loc='upper left', frameon=True)
    setup_axes(ax)
    fig.tight_layout()

    out_path = OUTPUT_DIR / 'graph7_aflto_ablation.png'
    fig.savefig(out_path, bbox_inches='tight')
    print(f"Generated {out_path}")
    plt.close(fig)


if __name__ == '__main__':
    main()
