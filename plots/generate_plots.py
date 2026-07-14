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
                    if exp_dir_name == 'exp3_failure_rate':
                        df = df.rename(columns={'variable_value': 'failure_rate', 'primary_metric': 'recovery_latency_ms', 'secondary_metric_1': 'aggregation_completeness'})
                    elif exp_dir_name == 'exp4_loss_exposure':
                        df = df.rename(columns={'variable_value': 'micro_slots', 'primary_metric': 'loss_exposure_fraction'})
                    elif exp_dir_name == 'exp5_recovery_comm':
                        df = df.rename(columns={'variable_value': 'incomplete_micro_slots', 'primary_metric': 'communication_overhead_KB'})
                data[scheme_id] = df
            except Exception as e:
                print(f"Error reading {csv_path}: {e}")
    return data

def plot_experiment(exp_name, x_col, y_col, x_label, y_label, output_filename,
                    x_scale='linear', y_scale='linear', legend_loc='upper left',
                    x_lim=None, exclude_schemes=None):
    """Generate a plot for a specific experiment and metric."""
    if exclude_schemes is None:
        exclude_schemes = []

    data = get_data(exp_name)
    if not data:
        print(f"No data found for experiment {exp_name}")
        return

    fig, ax = plt.subplots(figsize=(8, 5), dpi=300)

    for scheme_id, df in data.items():
        if scheme_id in exclude_schemes:
            continue
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

    # Graph 1: Ablation of PLOSHA Aggregation Architecture
    plot_exp1_ablation_aggregation()

    # Graph 2: Scheduling Efficiency
    plot_exp2_scheduling_efficiency()

    # Graph 3: Failure Rate
    plot_experiment('exp3_failure_rate',
                    x_col='failure_rate', y_col='recovery_latency_ms',
                    x_label='Failure Rate', y_label='Recovery Latency (ms)',
                    output_filename='graph3_failure_rate.png')

    # Graph 4: Loss Exposure
    plot_experiment('exp4_loss_exposure',
                    x_col='micro_slots', y_col='loss_exposure_fraction',
                    x_label='Number of Micro-slots', y_label='Loss Exposure Fraction',
                    output_filename='graph4_loss_exposure.png',
                    legend_loc='upper right',
                    exclude_schemes=['ft_serverless_edge'])

    # Graph 5: Recovery Communication
    plot_experiment('exp5_recovery_comm',
                    x_col='incomplete_micro_slots', y_col='communication_overhead_KB',
                    x_label='Incomplete Micro-slots', y_label='Communication Overhead (KB)',
                    output_filename='graph5_recovery_comm.png')

    # Graph 6: AFLTO Ablation
    plot_exp6_aflto_ablation()


def plot_exp6_aflto_ablation():
    """Generate a grouped bar chart for the AFLTO ablation study.

    This is PLOSHA-only: compares aggregation completeness and system
    availability with AFLTO enabled vs. disabled.
    """
    csv_path = BASE_DIR / 'plosha_rmfr' / 'exp6_aflto_ablation' / 'results.csv'
    if not csv_path.exists():
        print(f"No data found for experiment exp6_aflto_ablation")
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
        print("exp6: cannot find AFLTO toggle column")
        return

    # Map metrics — handle both PLOSHA-native and generic CSV formats
    comp_col = 'aggregation_completeness' if 'aggregation_completeness' in df.columns else 'secondary_metric_1'
    avail_col = 'system_availability' if 'system_availability' in df.columns else 'secondary_metric_2'

    if comp_col not in df.columns or avail_col not in df.columns:
        print("exp6: missing completeness or availability columns")
        return

    # Extract values for disabled (0) and enabled (1)
    row_off = df[df[toggle_col] == 0.0].iloc[0] if len(df[df[toggle_col] == 0.0]) > 0 else None
    row_on  = df[df[toggle_col] == 1.0].iloc[0] if len(df[df[toggle_col] == 1.0]) > 0 else None

    if row_off is None or row_on is None:
        print("exp6: need both AFLTO=0 and AFLTO=1 rows")
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

    out_path = OUTPUT_DIR / 'graph6_aflto_ablation.png'
    fig.savefig(out_path, bbox_inches='tight')
    print(f"Generated {out_path}")
    plt.close(fig)


def plot_exp1_ablation_aggregation():
    """Generate a grouped bar chart for the PLOSHA aggregation ablation study.

    Compares Flat-Epoch, Fixed-Slot, Adaptive-Slot, and Full PLOSHA
    across aggregation latency.
    """
    csv_path = BASE_DIR / 'plosha_rmfr' / 'exp1_ablation_aggregation' / 'results.csv'
    if not csv_path.exists():
        print(f"No data found for experiment exp1_ablation_aggregation")
        return

    try:
        df = pd.read_csv(csv_path)
    except Exception as e:
        print(f"Error reading {csv_path}: {e}")
        return

    # Ablation variant definitions (ordered so Full PLOSHA is last / visually prominent)
    VARIANTS = {
        'flat_epoch':    {'label': 'Flat-Epoch',    'color': '#d62728', 'marker': 'X'},
        'fixed_slot':    {'label': 'Fixed-Slot',    'color': '#ff7f0e', 'marker': 's'},
        'adaptive_slot': {'label': 'Adaptive-Slot', 'color': '#2ca02c', 'marker': '^'},
        'full_plosha':   {'label': 'Full PLOSHA',   'color': '#1f77b4', 'marker': 'o'},
    }

    import numpy as np

    fig, ax = plt.subplots(figsize=(8, 5), dpi=300)

    bar_sensors = [1000, 2000, 3000, 4000, 5000]
    variant_ids = list(VARIANTS.keys())
    n_variants = len(variant_ids)
    bar_width = 0.18
    x_positions = np.arange(len(bar_sensors))

    y_col = 'aggregation_latency_ms'
    y_label = 'Aggregation Latency (ms)'

    for i, variant_id in enumerate(variant_ids):
        info = VARIANTS[variant_id]
        sub = df[df['variant'] == variant_id]
        vals = []
        for s in bar_sensors:
            row = sub[sub['num_sensors'] == s]
            vals.append(row.iloc[0][y_col] if not row.empty else 0)
        offset = (i - (n_variants - 1) / 2) * bar_width
        ax.bar(x_positions + offset, vals, bar_width,
               label=info['label'], color=info['color'], zorder=3)
    ax.set_xticks(x_positions)
    ax.set_xticklabels([str(s) for s in bar_sensors])
    ax.set_xlabel('Number of Sensors')
    ax.set_ylabel(y_label)
    ax.legend(loc='upper left', frameon=True)
    setup_axes(ax)
    fig.tight_layout()

    out_path = OUTPUT_DIR / 'graph1_ablation_aggregation.png'
    fig.savefig(out_path, bbox_inches='tight')
    print(f"Generated {out_path}")
    plt.close(fig)


def plot_exp2_scheduling_efficiency():
    """Generate a line plot for scheduling efficiency.

    Compares PLOSHA-RMFR with FedDQN, FT-Workflow, and FT-Serverless-Edge
    on scheduling latency.
    """
    # Schemes to compare for this experiment
    exp2_schemes = {
        'plosha_rmfr':              SCHEMES['plosha_rmfr'],
        'fed_dqn':                  SCHEMES['fed_dqn'],
        'fault_tolerant_workflow':  SCHEMES['fault_tolerant_workflow'],
        'ft_serverless_edge':       SCHEMES['ft_serverless_edge'],
    }

    # Load data for each scheme
    data = {}
    for scheme_id, info in exp2_schemes.items():
        csv_path = BASE_DIR / scheme_id / 'exp2_scheduling_efficiency' / 'results.csv'
        if csv_path.exists():
            try:
                df = pd.read_csv(csv_path)
                # Normalise column names from generic format
                if 'variable_value' in df.columns:
                    df = df.rename(columns={
                        'variable_value': 'num_fog_nodes',
                        'primary_metric': 'scheduling_latency_ms',
                        'secondary_metric_1': 'workload_imbalance',
                    })
                data[scheme_id] = df
            except Exception as e:
                print(f"Error reading {csv_path}: {e}")

    if not data:
        print("No data found for experiment exp2_scheduling_efficiency")
        return

    fig, ax = plt.subplots(figsize=(8, 5), dpi=300)

    y_col = 'scheduling_latency_ms'
    y_label = 'Scheduling Latency (ms)'

    for scheme_id, df in data.items():
        if y_col not in df.columns:
            continue
        info = exp2_schemes[scheme_id]
        ax.plot(df['num_fog_nodes'], df[y_col],
                label=info['label'], color=info['color'],
                marker=info['marker'], linewidth=2.5, markersize=8,
                zorder=5 if scheme_id == 'plosha_rmfr' else 3)
    ax.set_xlabel('Number of Fog Nodes')
    ax.set_ylabel(y_label)
    ax.set_yscale('log')
    setup_axes(ax)

    # Legend — Refs first, then Ours
    handles, labels = ax.get_legend_handles_labels()
    sorted_pairs = sorted(zip(handles, labels),
                          key=lambda p: (1 if 'Ours' in p[1] else 0, p[1]))
    handles_sorted, labels_sorted = zip(*sorted_pairs)
    ax.legend(handles_sorted, labels_sorted, loc='upper left', frameon=True)
    fig.tight_layout()

    out_path = OUTPUT_DIR / 'graph2_scheduling_efficiency.png'
    fig.savefig(out_path, bbox_inches='tight')
    print(f"Generated {out_path}")
    plt.close(fig)


if __name__ == '__main__':
    main()
