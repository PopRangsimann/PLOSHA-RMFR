"""
IIoT Sensor Entity Module
===========================
Models resource-constrained Industrial IoT sensors that generate
operational measurements and encrypt them with AES-GCM.

Paper Reference:
  - System Model: S = {S_1, S_2, ..., S_m} - set of IIoT sensors
  - "Sensors continuously generate operational data such as vibration,
     temperature, pressure, and equipment-health measurements."
  - Phase III, Step 3: CT_j = Enc_{k_i}(d_j) using fog-scoped AES-GCM key
"""

import random
from typing import Optional

from src.crypto import aes_gcm


class Sensor:
    """
    IIoT Sensor entity (S_j).

    Generates simulated industrial sensor readings and encrypts them
    using the fog-scoped AES-GCM key k_i before transmission to the
    assigned fog node.
    """

    def __init__(self, sensor_id: int, assigned_fog_id: int,
                 aes_key: Optional[bytes] = None):
        """
        Initialize a sensor.

        Args:
            sensor_id: Unique sensor identifier j.
            assigned_fog_id: ID of assigned fog node (Γ(S_j) = F_i).
            aes_key: Fog-scoped AES-GCM key k_i (provisioned by KRM).
        """
        self.sensor_id = sensor_id
        self.assigned_fog_id = assigned_fog_id
        self.aes_key = aes_key
        self.active = True  # Act_j(t): 1 if active, 0 if disconnected

    def generate_reading(self, sensor_type: str = "temperature") -> float:
        """
        Generate a simulated industrial sensor reading.

        Args:
            sensor_type: Type of measurement to simulate.

        Returns:
            Simulated sensor value d_j.

        Paper: "vibration, temperature, pressure, equipment-health measurements"
        """
        if not self.active:
            return 0.0

        if sensor_type == "temperature":
            return random.gauss(75.0, 5.0)    # °C, industrial range
        elif sensor_type == "vibration":
            return random.gauss(2.5, 0.5)     # mm/s RMS
        elif sensor_type == "pressure":
            return random.gauss(101.3, 2.0)   # kPa
        elif sensor_type == "health":
            return random.uniform(0.0, 100.0) # Equipment health score
        else:
            return random.gauss(50.0, 10.0)   # Generic

    def encrypt_reading(self, reading: float) -> bytes:
        """
        Encrypt a sensor reading using fog-scoped AES-GCM.

        CT_j = Enc_{k_i}(d_j)

        Args:
            reading: Plaintext sensor value d_j.

        Returns:
            AES-GCM ciphertext CT_j.

        Paper: Phase III, Step 3 - Lightweight AES-GCM at sensor layer
               instead of heavy Paillier encryption
        """
        if self.aes_key is None:
            raise RuntimeError(f"Sensor {self.sensor_id}: AES key not provisioned")
        return aes_gcm.encrypt(self.aes_key, reading)

    def sense_and_encrypt(self, sensor_type: str = "temperature"):
        """
        Combined: generate a reading and encrypt it.

        Returns:
            Tuple (plaintext_value, ciphertext).
        """
        reading = self.generate_reading(sensor_type)
        ciphertext = self.encrypt_reading(reading)
        return reading, ciphertext

    def deactivate(self):
        """Simulate sensor disconnection/outage."""
        self.active = False

    def activate(self):
        """Restore sensor to active state."""
        self.active = True

    def __repr__(self):
        return (f"Sensor(id={self.sensor_id}, fog={self.assigned_fog_id}, "
                f"active={self.active})")
