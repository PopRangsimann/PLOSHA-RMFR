import os
import csv
import numpy as np
from typing import Dict, List, Any, Tuple

PROTOCOL_PRIORITY = {
    "Modbus_TCP": 0.9,
    "MQTT": 0.8,
    "OPC_UA": 0.8,
    "HTTP": 0.3,
    "HTTPS": 0.4,
    "DNS": 0.2
}

class DatasetTransformer:
    def __init__(self, plosha_csv_path: str = None, random_seed: int = 42):
        self.plosha_csv_path = plosha_csv_path or os.path.join(os.path.dirname(__file__), "plosha_dataset.csv")
        self.random_seed = random_seed
        self._rng = np.random.RandomState(random_seed)

    @staticmethod
    def generate_plosha_dataset(output_path: str, num_sensors: int = 100, num_fog_nodes: int = 5):
        """Merge real IIoT edge data and IoTSyn network data into a single evaluation dataset."""
        import os
        base_dir = os.path.dirname(__file__)
        iiot_path = os.path.join(base_dir, "iiot_edge_computing_dataset.csv")
        iotsyn_path = os.path.join(base_dir, "IoTSyn_IIoT_Network_20260404_134500.csv")
        
        # Load IIoT Physical Data
        iiot_rows = []
        with open(iiot_path, 'r', encoding='utf-8') as f:
            reader = csv.DictReader(f)
            for r in reader:
                iiot_rows.append(r)
                
        # Load IoTSyn Network Data
        iotsyn_rows = []
        with open(iotsyn_path, 'r', encoding='utf-8') as f:
            # Skip comment lines at the top of the file
            lines = [line for line in f if not line.startswith('#')]
            reader = csv.DictReader(lines)
            for r in reader:
                iotsyn_rows.append(r)
                
        # Merge them row-by-row
        merged_rows = []
        sensor_to_fog = {f"S{i}": f"F{i % num_fog_nodes}" for i in range(num_sensors)}
        
        num_merged = min(len(iotsyn_rows), 10000) # Cap at 10000 for evaluation speed
        
        for i in range(num_merged):
            net_r = iotsyn_rows[i]
            phys_r = iiot_rows[i % len(iiot_rows)] # Wrap around physical data if needed
            
            sensor_id = f"S{i % num_sensors}"
            fog_id = sensor_to_fog[sensor_id]
            is_failure = 1 if net_r.get("Label", "Normal") == "Malicious" else 0
            
            merged_rows.append({
                "Timestamp": net_r.get("Timestamp", phys_r.get("Timestamp")),
                "Sensor_ID": sensor_id,
                "Fog_Node_ID": fog_id,
                "Temperature": phys_r.get("Temperature", 0.0),
                "Pressure": phys_r.get("Pressure", 0.0),
                "Vibration": phys_r.get("Vibration", 0.0),
                "Is_Failure": is_failure,
                "Protocol": net_r.get("Protocol", "MQTT"),
                "Connection_Duration": net_r.get("Connection_Duration", 1.0),
                "Flow_Rate": net_r.get("Flow_Rate", 100.0),
                "Bytes_Transferred": net_r.get("Bytes_Transferred", 512),
                "Packet_Size": net_r.get("Packet_Size", 512),
                "Device_Role": net_r.get("Device_Role", "Sensor"),
                "Source_IP": net_r.get("Source_IP", f"10.0.2.{10+(i%num_sensors)}"),
                "Dest_IP": net_r.get("Dest_IP", f"10.0.1.{10+(i%num_fog_nodes)}"),
                "Label": net_r.get("Label", "Normal")
            })
            
        fieldnames = ["Timestamp", "Sensor_ID", "Fog_Node_ID", "Temperature", "Pressure", 
                      "Vibration", "Is_Failure", "Protocol", "Connection_Duration", 
                      "Flow_Rate", "Bytes_Transferred", "Packet_Size", "Device_Role", 
                      "Source_IP", "Dest_IP", "Label"]
                      
        with open(output_path, 'w', newline='', encoding='utf-8') as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(merged_rows)
            
        return output_path

    def _ensure_dataset(self, num_sensors: int, num_fog_nodes: int) -> List[Dict[str, Any]]:
        if not os.path.exists(self.plosha_csv_path):
            self.generate_plosha_dataset(self.plosha_csv_path, num_sensors=num_sensors, num_fog_nodes=num_fog_nodes)
        with open(self.plosha_csv_path, 'r', encoding='utf-8') as f:
            return list(csv.DictReader(f))

    def transform_for_plosha(self, num_sensors: int, num_fog_nodes: int, max_sensor_value: int = 65535) -> Dict[str, Any]:
        rows = self._ensure_dataset(num_sensors, num_fog_nodes)
        temps = [float(r["Temperature"]) for r in rows]
        t_min, t_max = min(temps), max(temps)
        
        sensor_traces = {i: [] for i in range(num_sensors)}
        fog_assignment = {i: [] for i in range(num_fog_nodes)}
        failure_schedule = {}
        epoch_workloads = []

        for i, row in enumerate(rows):
            sid = int(row["Sensor_ID"].replace("S", ""))
            fog_id = int(row["Fog_Node_ID"].replace("F", ""))
            epoch = i // num_sensors
            
            if sid not in fog_assignment[fog_id]:
                fog_assignment[fog_id].append(sid)
                
            val = float(row["Temperature"])
            scaled = 0 if t_max == t_min else int((val - t_min) / (t_max - t_min) * max_sensor_value)
            sensor_traces[sid].append((0.0, max(0, min(max_sensor_value, scaled))))
            
            if int(row["Is_Failure"]) == 1:
                if epoch not in failure_schedule: failure_schedule[epoch] = []
                if fog_id not in failure_schedule[epoch]: failure_schedule[epoch].append(fog_id)
                    
        num_epochs = len(rows) // num_sensors
        for ep in range(num_epochs):
            epoch_workloads.append({i: 1.0 for i in range(num_fog_nodes)})

        return {
            "sensor_traces": sensor_traces, "fog_assignment": fog_assignment,
            "failure_schedule": failure_schedule, "epoch_workloads": epoch_workloads,
            "num_data_rows": len(rows), "num_normal_rows": len(rows)
        }

    def transform_for_ref22(self, num_sensors: int, num_fog_nodes: int) -> Dict[str, Any]:
        rows = self._ensure_dataset(num_sensors, num_fog_nodes)
        tasks = []
        max_ps = max((float(r["Packet_Size"]) for r in rows), default=1.0)
        max_fr = max((float(r["Flow_Rate"]) for r in rows), default=1.0)
        
        for row in rows:
            exec_time = float(row["Connection_Duration"])
            cpu_req = float(row["Packet_Size"]) / max_ps
            priority = PROTOCOL_PRIORITY.get(row["Protocol"], 0.5)
            
            flow_factor = float(row["Flow_Rate"]) / max_fr
            base_deadline = exec_time * (2.0 + (1.0 - priority))
            deadline = max(exec_time * 1.1, base_deadline * (1.0 + 0.5 * (1.0 - flow_factor)))
            
            tasks.append({
                "execution_time": exec_time, "cpu_requirement": cpu_req,
                "priority": priority, "deadline": deadline,
                "protocol": row["Protocol"], "device_role": row["Device_Role"], "label": row["Label"]
            })
            
        queue_states = []
        rows_per_epoch = max(1, len(rows) // max(1, num_sensors // num_fog_nodes))
        for i in range(0, len(tasks), rows_per_epoch):
            chunk = tasks[i:i+rows_per_epoch]
            ql = len(chunk)
            avg_cpu = sum(t["cpu_requirement"] for t in chunk) / ql if ql else 0
            avg_pri = sum(t["priority"] for t in chunk) / ql if ql else 0
            queue_states.append([float(ql), avg_cpu, avg_pri])
            
        return {"tasks": tasks, "queue_states": queue_states}

    def transform_for_ref24(self, num_sensors: int, num_fog_nodes: int, max_sensor_value: int = 65535) -> Dict[str, Any]:
        rows = self._ensure_dataset(num_sensors, num_fog_nodes)
        max_bt = max((float(r["Bytes_Transferred"]) for r in rows), default=1.0)
        readings = []
        for row in rows:
            sid = int(row["Sensor_ID"].replace("S", ""))
            fog_id = int(row["Fog_Node_ID"].replace("F", ""))
            m_ij = int(float(row["Bytes_Transferred"]) / max_bt * max_sensor_value)
            readings.append({"sensor_id": sid, "es_id": fog_id, "m_ij": max(0, min(max_sensor_value, m_ij))})
            
        es_batches = {f: 0 for f in range(num_fog_nodes)}
        for r in readings: es_batches[r["es_id"]] += 1
        return {"sensor_readings": readings, "es_batches": es_batches}

    def transform_for_ref37(self, num_sensors: int, num_fog_nodes: int) -> Dict[str, Any]:
        rows = self._ensure_dataset(num_sensors, num_fog_nodes)
        dags = []
        rows_per_epoch = max(1, len(rows) // max(1, num_sensors // num_fog_nodes))
        
        for i in range(0, len(rows), rows_per_epoch):
            chunk = rows[i:i+rows_per_epoch]
            if not chunk: continue
            
            nodes = [{"node_id": idx, "role": r["Device_Role"], "exec_time": float(r["Connection_Duration"])} for idx, r in enumerate(chunk)]
            edges = []
            if len(nodes) > 1:
                for idx in range(len(nodes) - 1):
                    edges.append((idx, idx+1, float(chunk[idx]["Bytes_Transferred"])))
            dags.append({"nodes": nodes, "edges": edges})
            
        return {"dags": dags}

    def transform_for_ref38(self, num_sensors: int, num_fog_nodes: int) -> Dict[str, Any]:
        rows = self._ensure_dataset(num_sensors, num_fog_nodes)
        streams = []
        rows_per_epoch = max(1, len(rows) // max(1, num_sensors // num_fog_nodes))
        
        for i in range(0, len(rows), rows_per_epoch):
            chunk = rows[i:i+rows_per_epoch]
            if not chunk: continue
            sfc = [r["Protocol"] for r in chunk[:5]]
            fr_series = [float(r["Flow_Rate"]) for r in chunk]
            streams.append({"sfc_chain": sfc, "flow_rates": fr_series})
            
        return {"streams": streams}
