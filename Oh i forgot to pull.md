# Oh I Forgot to Pull: What We Actually Fixed 🚀

This document summarizes the critical fixes that currently exist **ONLY on your local machine** (commit `eed2702`). Because your co-authors did not implement these, this work must be preserved and merged back into the repository.

### 1. The fedDQN "Real DNN" Implementation (Critical for Reviewer)
- **The Problem:** The reviewer explicitly criticized the baseline for not simulating a real DNN overhead. The Github version still uses a basic Q-Table `std::map` lookup, which takes microseconds and results in an unrealistically fast scheduling latency.
- **Our Fix:** We injected a real Matrix Multiplication Forward Pass into `fed_dqn/src/fed_dqn_sim.cpp` using a new header `dqn_network.hpp`. This correctly simulates the CPU cycles required for DNN inference, drastically changing the scheduling latency to realistically reflect the baseline's overhead.

### 2. Fair & Isolated Cloud Deployment Scripts
- **The Problem:** Running all experiments sequentially on one machine biases the results due to thermal throttling and resource starvation, which breaks fairness.
- **Our Fix:** We created a Python generator (`generate_cloud_scripts.py`) that compiled exactly 6 atomic bash scripts (`run_cloud_instance_1.sh` through `6.sh`). These guarantee perfectly isolated workloads that can be launched concurrently across your 6 cloud instances.

### 3. PLOSHA-RMFR Native Overwrite Protection
- **The Problem:** When running both Native and SGX variations of PLOSHA, the outputs overwrite each other in the `exp_X` folders, destroying data before the Python plotting script can read it.
- **Our Fix:** We added robust, space-safe Bash globbing to the cloud scripts. After Native PLOSHA finishes, the script atomically renames the folders to `_native` before moving on. This guarantees both sets of data are safely preserved for `generate_plots.py`.

### 4. Idempotent Cloud Compilation
- **The Problem:** Uploading partially built object files to the cloud can cause `make` to skip compiling, leading to mismatched binaries.
- **Our Fix:** We forced `make clean && make -j4` into every single cloud script. No matter what state the cloud repository is in, it will compile a pristine, deterministic benchmark binary.

### 5. PLOSHA-RMFR Double-Counting Latency
- **The Problem:** The `scheduling_latency_ms` was accidentally inheriting the heavy payload of the RMFR recovery protocol.
- **Our Fix:** We stripped the recovery logic out of the scheduling timer in `des_engine.cpp` so it strictly measures decision-making time. *(Note: Your co-authors did attempt to fix this one via a `std::chrono` wrapper in their commits).*

### 6. Baseline Workload Fairness (Same Task Configurations)
- **The Problem:** The different benchmark baselines were executing with slightly mismatched configurations (e.g., uneven sensor counts in Exp 9), leading to an unfair apples-to-oranges comparison of workload imbalance.
- **Our Fix:** We aligned the execution parameters across all baselines (e.g., hardcoding `--sensors 12600` for `ft_serverless_edge` Exp 9 and explicitly routing all baselines to the exact same `$DATASET_PATH`). This ensures every single algorithm is subjected to the exact same discrete event workload, which is a critical detail for a fair evaluation.

---
**Summary:** If you abandon this local commit, you lose the **Real DNN** simulation required to pass the journal review, and you lose the **6 Cloud Instance Scripts** required to execute the fair benchmark. 

*Let me know when you are ready to merge this safely with their new folder structure!*
