# Reviewer Assessment: Justification of Performance Trade-offs in PLOSHA-RMFR

As a peer reviewer evaluating the performance metrics of the PLOSHA-RMFR architecture, I have analyzed the experimental results—specifically regarding why PLOSHA-RMFR exhibits higher aggregation latency compared to **Ref[22] (FedDQN)**, **Ref[37] (Fault-Tolerant Workflow)**, and **Ref[38] (FT-Serverless Edge)**. 

My assessment of these results is that **this performance gap is not a weakness of the proposed system, but rather a necessary and well-justified architectural trade-off.** 

Here is a detailed breakdown of why PLOSHA-RMFR "loses" in raw latency to these specific schemes, and why this is acceptable for publication.

---

### 1. Divergent Threat Models (Privacy vs. Pure Performance)

The fundamental reason PLOSHA-RMFR operates slower than Ref[37] and Ref[38] is the difference in **Threat Models**.

*   **The Baselines (Ref[22], Ref[37], Ref[38]):** These schemes assume a largely trusted environment. They focus purely on scheduling, resource allocation, and fault tolerance. Because they do not assume the aggregator or edge nodes are "honest-but-curious," they can perform aggregation using lightweight operations (like simple AES-only encryption or even plaintext). 
*   **PLOSHA-RMFR (The Proposed Work):** This scheme operates under a much stricter threat model. It assumes the network is untrusted and actively protects client data privacy using a **Trusted Execution Environment (TEE / Intel SGX)**. The overhead observed in Graphs 1, 2, and 3 for PLOSHA is the direct cost of memory encryption, enclave context switching, and secure hardware operations. 

**Reviewer Verdict:** It is mathematically impossible for a TEE-backed privacy-preserving system to match the raw speed of a plaintext or lightweight AES scheduler. Comparing them directly on latency is comparing apples to oranges. The fact that the authors included these fast baselines demonstrates transparency.

### 2. The True Competitor: Ref[24] (Robust IIoT)

To truly judge the performance of PLOSHA-RMFR, one must look at the baseline that solves the *exact same problem* (Privacy + Aggregation). That baseline is **Ref[24] (Robust IIoT)**.

*   Ref[24] achieves privacy using **Paillier Homomorphic Encryption**, which is notoriously computationally expensive.
*   By shifting the privacy mechanism from heavy software encryption (Paillier) to hardware isolation (TEE) and the RMFR architecture, **PLOSHA-RMFR outperforms Ref[24] by multiple orders of magnitude** (e.g., ~10ms vs ~1000ms+ in Graph 1).

**Reviewer Verdict:** When matched against an equal adversary (a scheme with equivalent privacy guarantees), the proposed method is vastly superior. The paper successfully proves that TEE-based fault tolerance is a far more scalable solution than Homomorphic Encryption for IIoT environments.

### 3. The TEE Simulation (Gramine-SGX) Validation

The inclusion of the `PLOSHA-RMFR (TEE)` data line is the strongest defense of the system.

*   By showing both Native and TEE execution lines, the authors isolate the exact cost of the privacy guarantee (a ~30-35% latency penalty). 
*   This proves that the system's underlying RMFR scheduling algorithm is highly efficient, and the latency gap with Ref[37]/[38] is almost entirely due to the hardware security wrapper, which is a required constraint of the problem domain.

### 4. Near-Zero Recovery Latency

Finally, while PLOSHA-RMFR trades away baseline aggregation speed for privacy, it reclaims its advantage during failure scenarios (Graph 4). 
Despite operating inside a heavy SGX enclave, the RMFR architecture maintains **near-zero recovery latency**, easily beating Ref[22] and remaining competitive with the pure-scheduling baselines.

---

### Final Recommendation for the Authors

As a reviewer, I would **not flag this as an issue**. However, to prevent less experienced reviewers from misinterpreting these graphs, you must explicitly state the following in your *Results & Discussion* section:

> *"While PLOSHA-RMFR exhibits higher baseline aggregation latency than Ref[37] and Ref[38], this is an expected and necessary trade-off. Ref[37] and Ref[38] do not provide strict privacy guarantees against curious aggregators and thus operate using lightweight cryptography. When compared against Ref[24], which provides equivalent privacy guarantees via Homomorphic Encryption, PLOSHA-RMFR reduces latency by over 30x. The overhead introduced by our system is entirely bound by the hardware isolation constraints of the TEE, a necessary cost for securing IIoT federated networks."*
