# **PLOSHA-RMFR: How to Measure Every Input**

*Phase-by-phase measurement guide for simulation implementation*

**Gramine-SGX Edition — TEE path migrated from QEMU OP-TEE**

Somchart Fugkeaw et al., SIIT, Thammasat University

| Revision note Every QEMU OP-TEE reference in the original plan has been replaced with Gramine-SGX. Gramine runs an unmodified CPython interpreter directly inside an SGX enclave, so the trusted execution path no longer needs a separately compiled native Trusted Application (TA). The same tee\_sign() / tee\_verify() Python module now runs both outside the enclave (for local testing) and inside the enclave under ‘gramine-sgx python3 tee\_sign.py’ (for the real measurements), so the codebase stays 100% Python end to end. Section 5.4 below documents the new TEE path and its Python implementation; all other phases were already Python and are unchanged except for the QEMU-host references noted inline. |
| :---- |

# **1\. Overview**

PLOSHA-RMFR has five execution phases. Each phase consumes inputs that are either directly observed from the simulation environment, computed from previous phases, or fixed hyperparameters set before experiments run. This document specifies exactly how to measure or generate every input, what Python code to use, and what boundary checks to apply so that no input silently drifts out of its mathematical domain.

The guiding rule throughout: every variable that appears in a formula must have a single, unambiguous measurement point in the simulator. There should be no variable that is read from one place for PLOSHA-RMFR and from a different place for baselines.

# **2\. Complete Input Reference**

Table 1 maps every symbol used in the paper to its type, valid range, and measurement method. Hyperparameters are in blue rows and are fixed before any experiment begins.

## **Table 1: All Inputs Across All Phases**

| Phase | Variable | Symbol | Type | Range | Measurement method |
| :---- | :---- | :---- | :---- | :---- | :---- |
| I – Init | Fog node workload (initial) | W\_i(0) | Observed | \[0,1\] | CPU utilization at t=0 via psutil |
| I – Init | Queue utilization (initial) | Q\_i(0) | Observed | \[0,1\] | Queue length / max queue capacity |
| I – Init | Communication latency (initial) | L\_i(0) | Observed | \[0,1\] | Normalized RTT to KRM at startup |
| I – Init | Reliability score (initial) | Rel\_i(0) | Fixed | \= 1.0 | Set to 1 for all nodes at deployment |
| II – Predict | Runtime workload | W\_i(t) | Observed | \[0,1\] | CPU util per epoch via SimPy resource monitor |
| II – Predict | Queue utilization | Q\_i(t) | Observed | \[0,1\] | Queue occupancy / capacity per epoch |
| II – Predict | Communication latency | L\_i(t) | Observed | \[0,1\] | Simulated RTT; normalized to \[0,1\] |
| II – Predict | Reliability score | Rel\_i(t) | Computed | \[0,1\] | RMFR reliability update formula (Phase IV Step 7\) |
| II – Predict | EWMA smoothing factor | α | Hyperparameter | (0,1); default 0.3 | Fixed before experiments; sensitivity in Exp. S1 |
| II – Predict | Capacity weights | ω\_w, ω\_q, ω\_l | Hyperparameter | \[0,1\], sum=1; default 0.4,0.3,0.3 | Fixed before experiments; sensitivity in Exp. S2 |
| II – Predict | Risk weights | η\_1, η\_2 | Hyperparameter | \[0,1\], sum=1; default 0.5,0.5 | Fixed before experiments; sensitivity in Exp. S2 |
| III – PLOSHA | Slot overhead coefficient | β\_t | Calibrated | \>0 | Timed per micro-slot on Gramine-SGX host; averaged over 1000 epochs |
| III – PLOSHA | m\* objective weights | λ\_1, λ\_2, λ\_3 | Hyperparameter | \[0,1\], sum=1; default 1/3 each | Fixed before experiments; sensitivity in Exp. S2 |
| III – PLOSHA | AES-GCM ciphertext (per sensor) | CT\_j | Generated | 16 bytes \+ 12-byte IV \+ 16-byte tag | Synthesized in simulation from d\_j using PyCryptodome |
| III – PLOSHA | Reports received per epoch | N\_recv(t) | Counted | \[0, N\_exp\] | Simulator counts accepted ciphertexts per epoch |
| III – PLOSHA | Expected reports per epoch | N\_exp(t) | Registry lookup | \= sum(Act\_j(t)) | KRM active-sensor registry; updated per epoch |
| IV – RMFR | Recovery urgency weights | ρ\_1, ρ\_2, ρ\_3 | Hyperparameter | \[0,1\], sum=1; default 1/3 each | Fixed before experiments; sensitivity in Exp. S2 |
| IV – RMFR | Candidate utility weights | α\_c, α\_r, α\_k | Hyperparameter | \[0,1\], sum=1; default 1/3 each | Fixed before experiments; sensitivity in Exp. S2 |
| IV – RMFR | Reliability momentum | β\_r | Hyperparameter | (0,1); default 0.9 | Fixed before experiments; controls history decay |
| IV – RMFR | Success / completeness weights | λ\_s, λ\_v | Hyperparameter | \[0,1\], sum=1; default 0.5,0.5 | Fixed before experiments |
| V – AFLTO | Quality score weights | ω\_1, ω\_2 | Hyperparameter | \[0,1\], sum=1; default 0.5,0.5 | Fixed before experiments |
| V – AFLTO | History decay factor | γ | Hyperparameter | (0,1); default 0.9 | Fixed before experiments |
| V – AFLTO | History blend factor | α\_h | Hyperparameter | (0,1); default 0.3 | Fixed before experiments |
| V – AFLTO | Error weights | κ\_1, κ\_2, κ\_3 | Hyperparameter | \[0,1\], sum=1; default 1/3 each | Fixed before experiments |
| V – AFLTO | Threshold learning rates | μ\_x (τ\_v,τ\_r,τ\_1,τ\_2,τ\_3,τ\_f) | Hyperparameter | (0,1); default 0.05 each | Fixed before experiments |
| V – AFLTO | Initial thresholds | τ\_v, τ\_r, τ\_1, τ\_2, τ\_3, τ\_f | Initial values | \[0,1\] with τ\_1\<τ\_2\<τ\_3 | Set once; documented in Supp. Table S1; evolved by AFLTO |

Note: β\_t calibration and recovery-signing now run on the Gramine-SGX host rather than a QEMU OP-TEE host (see Sections 5.2 and 5.4).

# **3\. Sensor Data — The Foundation of All Inputs**

Everything else in the system derives from sensor readings. Getting this right is the most important measurement decision in the entire experiment.

## **3.1 What a sensor reading is**

Each sensor S\_j produces a scalar integer d\_j at each reporting event. In the IIoT context of your paper, d\_j represents an industrial measurement — temperature, vibration, pressure — encoded as an integer so Paillier encryption can operate on it. The integer is in the range \[0, 65535\] (16-bit unsigned), which is a safe plaintext space for 2048-bit Paillier.

## **3.2 Real trace vs. synthetic**

### ***Table 2: Sensor Input Source Options***

| Input | Source | How it drives simulation | Fallback if unavailable |
| :---- | :---- | :---- | :---- |
| Sensor readings d\_j | Intel Berkeley Lab sensor dataset (temperature, humidity, light) — public domain | Read CSV; assign each sensor record to a simulated S\_j; scale to \[0, 65535\] integer | Synthetic: uniform random integers in \[0, 65535\]; use fixed seed |
| Reporting rate | Derived from timestamp column in trace | Interarrival times drive SimPy sensor event schedule | Poisson arrivals at 1/5/10 Hz (Low/Med/High) |
| Workload W\_i(t) | Computed from arrival rate relative to fog node capacity | W \= arrival\_rate(t) / node\_capacity; capped at 1.0 | Sinusoidal synthetic workload with noise |
| Failure events | Deterministic injector seeded by pre-announced seed list | At epoch start, each fog node fails with probability p; same events for all schemes | N/A — always deterministic |
| Communication latency L\_i(t) | Simulated: RTT drawn from log-normal distribution fitted to LAN measurements | Sample per epoch; normalize by 99th percentile baseline | Constant 5 ms as worst-case simplification |

Using a real trace is strongly preferred. The Intel Berkeley Lab dataset is freely available, widely cited in IIoT papers, and contains 54 sensors reporting temperature and humidity over 31 days. Using it means your workload pattern is not circular — you did not design the framework for a trace you already knew.

| \# Load Intel Berkeley Lab trace import pandas as pd   df \= pd.read\_csv('data.txt', sep=' ',     names=\['date','time','epoch','moteid','temp','humidity','light','voltage'\]) df \= df.dropna() df\['value'\] \= (df\['temp'\] \* 100).astype(int).clip(0, 65535\) df\['ts'\] \= pd.to\_datetime(df\['date'\] \+ ' ' \+ df\['time'\])   \# Assign sensors to fog nodes for j, mote in enumerate(df\['moteid'\].unique()\[:Ns\]):     sensor\_trace\[j\] \= df\[df\['moteid'\]==mote\]\[\['ts','value'\]\].values |
| :---- |

## **3.3 Encrypting sensor readings in the simulation**

At each reporting event, sensor S\_j encrypts d\_j with its AES-GCM key k\_i:

| from Crypto.Cipher import AES import os   def sensor\_encrypt(d\_j: int, k\_i: bytes) \-\> bytes:     nonce \= os.urandom(12)     cipher \= AES.new(k\_i, AES.MODE\_GCM, nonce=nonce)     ct, tag \= cipher.encrypt\_and\_digest(d\_j.to\_bytes(8, 'big'))     return nonce \+ ct \+ tag  \# 12 \+ 8 \+ 16 \= 36 bytes total   \# In SimPy: sensor fires this at each reporting interval def sensor\_process(env, sensor\_id, fog\_node, trace, k\_i):     for ts, d\_j in trace:         yield env.timeout(interarrival(ts))  \# follow trace timing         ct \= sensor\_encrypt(int(d\_j), k\_i)         fog\_node.receive(sensor\_id, ct, env.now) |
| :---- |

# **4\. Phase II — Measuring the State Vector**

The state vector State\_i(t) \= \[W\_i(t), Q\_i(t), L\_i(t), Rel\_i(t)\] is the input to the EWMA predictor. All four components must be in \[0,1\]. They are sampled once at the start of each aggregation epoch.

## **Table 3: Phase II Measurement Details**

| Variable | Symbol | Sim. measurement | Python code sketch |
| :---- | :---- | :---- | :---- |
| Workload | W\_i(t) | CPU task queue length / max queue, sampled at epoch start | W \= len(node.queue.items) / node.queue.capacity |
| Queue util | Q\_i(t) | Reports buffered / max buffer; normalized | Q \= node.buffer\_count / node.max\_buffer |
| Latency | L\_i(t) | Round-trip time to KRM / worst-case RTT; normalized | L \= rtt / max\_rtt  \# max\_rtt is 99th pct baseline |
| Reliability | Rel\_i(t) | Computed by RMFR formula at end of each epoch; starts at 1.0 | Rel \= RMFR.update\_reliability(succ, V, Rel\_prev) |
| Predicted state | State\_hat\_i(t+1) | EWMA applied element-wise to state vector | pred \= alpha\*state \+ (1-alpha)\*prev\_pred |
| Capacity | Cap\_i(t+1) | Derived from predicted state \+ weight vector (ω) | Cap \= Rel\_hat\*(1-(ow\*W\_hat+oq\*Q\_hat+ol\*L\_hat)) |
| Failure exposure | FE\_i(t) | Product of W\_hat, Q\_hat, (1-Rel\_hat) | FE \= W\_hat \* Q\_hat \* (1 \- Rel\_hat) |
| Risk | Risk\_i(t) | Weighted sum of capacity deficit and FE | Risk \= eta1\*(1-Cap) \+ eta2\*FE |

## **4.1 Workload W\_i(t)**

Workload represents how loaded the fog node’s processing queue is. In simulation, each fog node is a SimPy Resource with a fixed capacity. W\_i(t) is the queue occupancy normalized by that capacity:

| class FogNode:     def \_\_init\_\_(self, env, capacity, max\_queue):         self.resource \= simpy.Resource(env, capacity=capacity)         self.max\_queue \= max\_queue         self.queue\_len \= 0  \# updated on each arrival       def measure\_W(self):         \# queue\_len \= requests waiting \+ being served         return min(1.0, len(self.resource.queue) / self.max\_queue) |
| :---- |

Important: measure W at the START of the epoch, before new reports arrive. This represents the carried-over load from the previous epoch, which is what the predictor needs.

## **4.2 Queue utilization Q\_i(t)**

Queue utilization measures how full the incoming report buffer is — distinct from the processing queue. It captures whether the node is keeping up with the incoming data rate:

|     def measure\_Q(self):         \# buffer\_count \= ciphertexts received but not yet processed         return min(1.0, self.buffer\_count / self.max\_buffer)   \# max\_buffer is set per node as 2 \* (Ns / Nf) per node tier \# Low-tier node: capacity \= Ns/(2\*Nf), medium: Ns/Nf, high: 2\*Ns/Nf |
| :---- |

## **4.3 Latency L\_i(t)**

Latency is the normalized round-trip time between fog node F\_i and the KRM. In simulation, this is modelled as a random variable drawn from a log-normal distribution, calibrated to realistic LAN timings:

| import numpy as np   \# Calibrate once: mu and sigma for log-normal RTT in milliseconds \# Typical LAN: mean \~2ms, std \~1ms \-\> lognorm(mu=0.6, sigma=0.5) RTT\_MU, RTT\_SIGMA \= 0.6, 0.5 RTT\_99TH \= np.exp(RTT\_MU \+ 2.576 \* RTT\_SIGMA)  \# 99th percentile   def measure\_L(self):     rtt \= np.random.lognormal(RTT\_MU, RTT\_SIGMA)     return min(1.0, rtt / RTT\_99TH) |
| :---- |

The 99th-percentile RTT\_99TH is computed once in a calibration run of 10,000 samples before any experiment, then held fixed. This means L is comparable across all nodes and all runs.

## **4.4 Reliability Rel\_i(t)**

Reliability is not observed directly — it is computed by the RMFR formula at the end of the previous epoch and carried forward. At t=0, Rel\_i(0) \= 1.0 for all nodes. The update is:

| def update\_reliability(Rel\_prev, succ, V,                         beta\_r=0.9, lambda\_s=0.5, lambda\_v=0.5):     return min(1.0,                beta\_r \* Rel\_prev \+                (1 \- beta\_r) \* (lambda\_s \* succ \+ lambda\_v \* V)) |
| :---- |

# **5\. Phase III — Measuring PLOSHA Inputs**

## **Table 4: Phase III Measurement Details**

| Variable | Symbol | Sim. measurement | Python code sketch |
| :---- | :---- | :---- | :---- |
| m\* (optimal slots) | m\* | argmin of joint objective over \[1, m\_max\] | m\_star \= argmin\_m(obj(m, Cap, FE, Rel)) |
| AES-GCM ciphertext | CT\_j | Synthesize: draw d\_j from trace; encrypt with AES-GCM 256 | ct \= AES.new(k,MODE\_GCM,nonce).encrypt(d\_j.to\_bytes(8,'big')) |
| Paillier ciphertext | C\_j | TEE path (Gramine-SGX) or direct python-phe in normal world | C\_j \= pubkey.encrypt(d\_j)  \# python-phe; or via Gramine-SGX enclave (pure Python, see 5.4) |
| Micro-slot aggregate | C\_micro,k | Paillier homomorphic product over slot k's ciphertexts | C\_micro \= functools.reduce(lambda a,b: a\*b, C\_list) |
| Fog aggregate | C\_agg,i | Product of all m\* micro-slot aggregates | C\_agg \= functools.reduce(lambda a,b: a\*b, C\_micros) |
| Completeness | V\_i(t) | N\_recv / N\_exp; both counted by simulator | V \= node.n\_recv / KRM.n\_expected(node, t) |
| Completeness flag | Φ\_i(t) | 1 if V \< τ\_v (incomplete); 0 otherwise | phi \= 1 if V \< tau\_v else 0 |

## **5.1 Computing m\***

m\* is the result of the argmin over \[1, m\_max\]. There is no stochastic component — given the same prediction inputs, m\* is deterministic. The only thing to get right is that the objective function uses the predicted values (Cap, FE, Rel) from Phase II, not the current observed values:

| def compute\_m\_star(Cap, FE, Rel, beta\_t,                     lambda1, lambda2, lambda3, m\_max=20):     best\_m, best\_obj \= 1, float('inf')     for m in range(1, m\_max \+ 1):         T\_agg \= beta\_t \* m         L\_agg \= 1.0 / m         obj \= (lambda1 \* T\_agg \* (1 \- Cap) \+                lambda2 \* FE \* L\_agg \+                lambda3 \* (1 \- Rel) \* L\_agg)         if obj \< best\_obj:             best\_obj, best\_m \= obj, m     return best\_m |
| :---- |

## **5.2 Calibrating beta\_t**

beta\_t is the per-micro-slot processing overhead. It is not a free hyperparameter — it must be measured from the simulator itself before experiments run. Time 1000 single-slot aggregation epochs and take the mean:

| \# Calibration run (done once, before Exp. 1-7) import time, statistics   overhead\_times \= \[\] for \_ in range(1000):     t0 \= time.perf\_counter()     \# simulate one micro-slot: transform \+ aggregate Ns/Nf reports     run\_one\_microslot(Ns=Ns\_default, Nf=Nf\_default)     overhead\_times.append(time.perf\_counter() \- t0)   beta\_t \= statistics.mean(overhead\_times) print(f'beta\_t \= {beta\_t\*1e3:.3f} ms per micro-slot') |
| :---- |

beta\_t is measured on the same Gramine-SGX host as all other experiments. Document the value in Supplementary Table S1. It is used only in the m\* objective — it does not appear in any performance metric.

## **5.3 Measuring N\_recv and N\_exp**

These two counts determine the completeness score V\_i(t). They must be measured from exactly the same epoch window and must count the same type of object — ciphertexts accepted into the aggregation, not raw network packets:

| class FogNode:     def start\_epoch(self, t):         self.n\_recv \= 0         self.epoch\_start \= t       def receive(self, sensor\_id, ct, ts):         if self.epoch\_start \<= ts \< self.epoch\_start \+ EPOCH\_LEN:             self.n\_recv \+= 1             self.buffer.append((sensor\_id, ct))       def measure\_V(self, KRM):         n\_exp \= KRM.count\_active\_sensors(self.node\_id)         return self.n\_recv / n\_exp if n\_exp \> 0 else 1.0 |
| :---- |

## **5.4 Gramine-SGX TEE path (replaces QEMU OP-TEE)**

The trusted path for Paillier encryption and commit signing previously ran as a native OP-TEE Trusted Application (TA) inside a QEMU-emulated Arm TrustZone target, which meant maintaining a separate C codebase for the enclave side. Gramine removes that split: it runs an unmodified CPython interpreter directly inside an Intel SGX enclave under a signed manifest, so the same tee\_sign() / tee\_verify() module is plain Python whether it executes outside the enclave (for local development) or inside it (for the real measurements).

Build and run the enclave-side script with the standard Gramine toolchain (manifest generation, signing, and launch are configuration steps, not application code):

| \# One-time: generate and sign the Gramine manifest for the Python TEE script gramine-manifest \-Dlog\_level=error \-Dpython3=$(which python3) \\     tee\_sign.manifest.template tee\_sign.manifest gramine-sgx-sign \--manifest tee\_sign.manifest \--output tee\_sign.manifest.sgx   \# Run inside the SGX enclave gramine-sgx tee\_sign python3 tee\_sign.py |
| :---- |

tee\_sign.py itself is ordinary Python, built on the cryptography package. The signing key is sealed to the enclave via Gramine’s encrypted-file mount (fs.mounts in the manifest), so it never exists in plaintext on disk:

| \# tee\_sign.py — runs INSIDE the Gramine-SGX enclave (and, for local \# testing, identically outside it — no native TA / C code involved).   from cryptography.hazmat.primitives.asymmetric import ec from cryptography.hazmat.primitives import hashes, serialization   \# Sealed via Gramine's encrypted fs.mounts entry; never touches \# unencrypted disk inside the enclave. SEALED\_KEY\_PATH \= '/enclave/keys/commit\_key.pem'   def load\_enclave\_key(path: str \= SEALED\_KEY\_PATH):     with open(path, 'rb') as f:         return serialization.load\_pem\_private\_key(f.read(), password=None)   def tee\_sign(payload: bytes) \-\> bytes:     """Sign payload with the enclave-sealed EC key (replaces OP-TEE TA call)."""     private\_key \= load\_enclave\_key()     return private\_key.sign(payload, ec.ECDSA(hashes.SHA256()))   def tee\_verify(payload: bytes, signature: bytes, public\_key) \-\> bool:     """Verify at the cloud side using the enclave's public key."""     try:         public\_key.verify(signature, payload, ec.ECDSA(hashes.SHA256()))         return True     except Exception:         return False |
| :---- |

For Paillier homomorphic encryption inside the trusted path, python-phe runs unmodified under Gramine the same way:

| from phe import paillier   def tee\_paillier\_encrypt(d\_j: int, pubkey: 'paillier.PaillierPublicKey'):     """Runs inside the Gramine-SGX enclave; identical call outside it."""     return pubkey.encrypt(d\_j) |
| :---- |

Remote attestation (proving to the cloud verifier that a given signature really came from the signed enclave) uses Gramine’s built-in SGX quote interface, exposed to Python as a plain file read — no separate attestation TA is required:

| def get\_sgx\_quote(report\_data: bytes) \-\> bytes:     """Read the SGX quote Gramine exposes at /dev/attestation,     used once at startup to prove this enclave is the signed     tee\_sign.py running under the expected manifest measurement."""     with open('/dev/attestation/user\_report\_data', 'wb') as f:         f.write(report\_data.ljust(64, b'\\\\x00'))     with open('/dev/attestation/quote', 'rb') as f:         return f.read() |
| :---- |

# **6\. Phase IV — Measuring RMFR Inputs**

## **Table 5: Phase IV Measurement Details**

| Variable | Symbol | Sim. measurement | Python code sketch |
| :---- | :---- | :---- | :---- |
| Recovery urgency | RU\_i(t) | Weighted sum of Risk, (1-V), (1-Rel) | RU \= rho1\*Risk \+ rho2\*(1-V) \+ rho3\*(1-Rel) |
| Recovery mode | Mode\_i(t) | Piecewise rule: Φ and RU thresholds τ\_1, τ\_2, τ\_3, τ\_f | mode \= decide\_mode(phi, RU, Rel, tau1,tau2,tau3,tauf) |
| Candidate utility | U\_j(t) | For each neighbour: weighted Cap, Rel, (1-Risk) | U \= ac\*Cap\_j \+ ar\*Rel\_j \+ ak\*(1-Risk\_j) |
| Incomplete slots | D\_i^miss | Set of slots where n\_recv \< n\_exp | D\_miss \= {k for k in slots if recv\[k\] \< exp\[k\]} |
| Recovery success | Succ\_i(t) | 1 if recovered aggregate passes cloud verification; 0 otherwise | succ \= 1 if cloud\_verify(C\_final, sigma) else 0 |
| Updated reliability | Rel\_i(t+1) | RMFR formula: momentum blend of history and (Succ, V) | Rel\_new \= min(1, br\*Rel \+ (1-br)\*(ls\*succ \+ lv\*V)) |

## **6.1 Recovery success Succ\_i(t)**

Succ\_i(t) \= 1 if and only if the final aggregate C\_final passes cloud verification AND V\_i(t) \>= tau\_v after recovery. This means you need to actually run the Paillier multiplication and the signature verification to record a success — you cannot assume success from the absence of an error:

| def attempt\_recovery(mode, node, KRM, cloud):     try:         if mode \== 'MicroRecovery':             C\_final \= micro\_recover(node)         elif mode \== 'Failover':             C\_final \= failover\_recover(node, KRM)         else:             C\_final \= node.C\_agg         sigma \= tee\_sign(C\_final)  \# Gramine-SGX enclave (Python, see 5.4)         if cloud.verify(C\_final, sigma):             return 1, C\_final  \# Succ \= 1         return 0, node.C\_agg  \# verification failed     except Exception:         return 0, node.C\_agg  \# recovery failed |
| :---- |

# **7\. Phase V — Measuring AFLTO Inputs**

## **Table 6: Phase V Measurement Details**

| Variable | Symbol | Sim. measurement | Python code sketch |
| :---- | :---- | :---- | :---- |
| Final aggregate | C\_i^final | Select C\_agg or C\_agg^rec based on Φ and Succ | C\_final \= pick\_final(phi, succ, C\_agg, C\_agg\_rec) |
| Signed commit | σ\_i(t) | Sign H(T\_i) with TEE private key; verified at cloud | sigma \= tee\_sign(H(T\_i))  \# inside Gramine-SGX enclave |
| Quality score | Score\_i(t) | Weighted sum of V and Rel(t+1) | Score \= w1\*V \+ w2\*Rel\_new |
| Historical performance | Hist\_i(t+1) | EWMA of Score over past epochs | Hist\_new \= gamma\*Hist \+ (1-gamma)\*Score |
| Blended score | Score\*\_i(t) | Blend of Hist and Score | Score\_star \= ah\*Hist\_new \+ (1-ah)\*Score |
| Control error | e\_i(t) | Weighted sum of (S\_bar \- Score\*), RU, (1-Rel) | e \= k1\*(S\_bar \- Score\_star) \+ k2\*RU \+ k3\*(1-Rel\_new) |
| Updated thresholds | τ\_x(t+1) | Projected gradient step; enforce τ\_1\<τ\_2\<τ\_3 after update | tau \= clip(tau \+ mu\*e, 0, 1\)  \# per threshold |

## **7.1 Target score S\_bar**

The error formula contains S\_bar — the performance target. This is a design parameter, not measured from the environment. Set S\_bar \= 0.95 (meaning 95% completeness and reliability is the goal). Fix this before experiments and document it in Supplementary Table S1.

| S\_BAR \= 0.95  \# performance target — fixed before all experiments |
| :---- |

## **7.2 Threshold ordering constraint**

After each AFLTO update, enforce tau\_1 \< tau\_2 \< tau\_3. If an update violates this, clamp the violating threshold to maintain the ordering:

| def enforce\_ordering(tau1, tau2, tau3, margin=0.01):     tau1 \= min(tau1, tau2 \- margin)     tau2 \= max(tau2, tau1 \+ margin)     tau2 \= min(tau2, tau3 \- margin)     tau3 \= max(tau3, tau2 \+ margin)     return clip(tau1), clip(tau2), clip(tau3) |
| :---- |

# **8\. Normalization Protocol**

All state variables must stay in \[0,1\] throughout the simulation. Out-of-range values are not just mathematical errors — they silently corrupt the EWMA predictor and cause Cap, Risk, and RU to drift outside their proofs. The simulator must check every variable at every epoch.

## **Table 7: Normalization Rules and Boundary Checks**

| Variable | Raw unit | Normalization | Boundary check |
| :---- | :---- | :---- | :---- |
| W\_i(t) | Task queue length (count) | W \= queue\_len / max\_queue\_capacity; max set per node tier | Assert 0 \<= W \<= 1 every epoch |
| Q\_i(t) | Buffer occupancy (count) | Q \= buffer\_count / max\_buffer; max\_buffer \= 2 \* Ns/Nf | Assert 0 \<= Q \<= 1 |
| L\_i(t) | Round-trip time (ms) | L \= rtt / rtt\_99th; rtt\_99th computed from 1000-sample baseline run | Assert 0 \<= L \<= 1; clip if rtt \> rtt\_99th |
| Rel\_i(t) | Dimensionless | Already normalized by RMFR formula; min() ensures \<= 1 | Assert 0 \<= Rel \<= 1 |
| Cap, FE, Risk, RU | Dimensionless | Mathematically bounded by construction (all inputs in \[0,1\]); verify empirically | Log any out-of-range value as a simulation bug |
| Score, Hist, Score\* | Dimensionless | \[0,1\] by construction (weighted sum of V and Rel, both in \[0,1\]) | Assert 0 \<= Score \<= 1 |

| \# Add this assertion at the end of every epoch def check\_state(node, t):     assert 0 \<= node.W \<= 1,    f'W out of range at t={t}: {node.W}'     assert 0 \<= node.Q \<= 1,    f'Q out of range at t={t}: {node.Q}'     assert 0 \<= node.L \<= 1,    f'L out of range at t={t}: {node.L}'     assert 0 \<= node.Rel \<= 1,  f'Rel out of range at t={t}: {node.Rel}'     assert 0 \<= node.Cap \<= 1,  f'Cap out of range: {node.Cap}'     assert 0 \<= node.Risk \<= 1, f'Risk out of range: {node.Risk}'     assert 0 \<= node.V \<= 1,    f'V out of range: {node.V}' |
| :---- |

# **9\. Output Metric Measurement**

These are the numbers that appear in your results tables and figures. Each must be measured consistently across all schemes — PLOSHA-RMFR and all four baselines use the same measurement code.

## **Table 8: Output Metric Measurement Specification**

| Metric | Formula | What the simulator records | Where it appears |
| :---- | :---- | :---- | :---- |
| Aggregation latency | t\_done \- t\_epoch\_start (ms) | Timestamp when C\_agg,i is ready minus epoch start; average over all nodes | Exps. 1, 2, 3 |
| Queue utilization | mean(Q\_i(t)) over epoch | Sample Q\_i at each micro-slot boundary; average per epoch | Exp. 3 |
| Recovery invocation count | Count(Mode \!= Normal) | Increment counter whenever mode is Delegation, MicroRecovery, or Failover | Exp. 3, 7 |
| Recovery latency | t\_C\_final \- t\_failure (ms) | Timestamp when recovery completes minus failure injection time; average over failed epochs | Exp. 4 |
| Aggregation completeness | V\_i(t) \= N\_recv / N\_exp | Per epoch; average over all nodes and 30 runs | Exps. 4, 7 |
| System availability | Fraction of epochs with V \>= 0.95 | Boolean per epoch; mean over 30 runs | Exps. 4, 7 |
| Aggregation-loss exposure | L \= 1/m\* (theoretical); empirical: fraction of epoch's reports in failed slot | Count sensors in failed micro-slot / N\_exp; compare to 1/m\* bound | Exp. 5 |
| Recovery comm. overhead | Bytes in recovery messages only (KB) | Log each message tagged 'recovery'; sum per epoch; report mean over 30 runs | Exp. 6 |
| TEE overhead delta (δ) | δ \= mean(T-path) \- mean(N-path) (µs) | 10,000 paired timing trials comparing native Python execution vs. Gramine-SGX enclave execution; report mean ± std | Exp. 8Δ |

## **9.1 Aggregation latency**

| def measure\_latency(node, epoch\_start, env):     \# Record the SimPy time when C\_agg,i is fully computed     t\_done \= env.now     return (t\_done \- epoch\_start) \* 1000  \# convert to ms |
| :---- |

## **9.2 Recovery latency**

| def measure\_recovery\_latency(node, t\_failure, env):     \# t\_failure: SimPy time when failure was injected     \# env.now: SimPy time when C\_final passes cloud verification     return (env.now \- t\_failure) \* 1000  \# ms |
| :---- |

## **9.3 Recovery communication overhead**

| \# Tag every recovery-related message in the simulation class Message:     def \_\_init\_\_(self, type\_, size\_bytes, is\_recovery=False):         self.type\_ \= type\_         self.size\_bytes \= size\_bytes         self.is\_recovery \= is\_recovery   \# At end of epoch: recovery\_bytes \= sum(m.size\_bytes for m in epoch\_messages                       if m.is\_recovery) / 1024  \# KB |
| :---- |

# **10\. Pre-Experiment Implementation Checklist**

Before running any of the 7 primary experiments, confirm all of the following:

1. beta\_t calibrated from 1,000 warmup epochs on the Gramine-SGX host

2. RTT\_99TH computed from 10,000 latency samples

3. All weight hyperparameters fixed and documented in Supp. Table S1

4. All initial thresholds fixed with tau\_1 \< tau\_2 \< tau\_3

5. 30 random seeds pre-announced and committed to disk

6. Deterministic failure injector seeded and verified (same failures to all schemes)

7. Intel Berkeley Lab trace loaded and sensor-to-fog assignment fixed

8. Boundary assertions active in simulation loop

9. Simulator micro-validation passed: sum(Dec(C\_micro,k)) \== plaintext sum

10. EWMA validated on stationary synthetic trace

11. Gramine-SGX environment built: tee\_sign.py manifest generated and signed (gramine-sgx-sign), enclave measurement (MRENCLAVE) recorded, and remote-attestation quote retrieval verified

12. Delta measurement Exp. 8Δ run and TEE\_DELTA\_US recorded

*End of Input Measurement Guide — PLOSHA-RMFR (Gramine-SGX Edition)*