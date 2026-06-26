"""
AES-GCM Encryption Module
===========================
Implements fog-scoped AES-256-GCM encryption for sensor-to-fog communication.

Paper Reference:
  - Phase I, Step 2: fog-scoped AES-GCM key k_i provisioned to enclave
  - Phase III, Step 3: CT_j = Enc_{k_i}(d_j)
  - Sensor layer uses lightweight AES-GCM instead of heavy Paillier
"""

import os
import struct
from cryptography.hazmat.primitives.ciphers.aead import AESGCM


def generate_key(key_size_bits: int = 256) -> bytes:
    """
    Generate a random AES-GCM key.

    Args:
        key_size_bits: Key size in bits (128, 192, or 256).

    Returns:
        Random key bytes.

    Paper: Phase I, Step 2 - KRM generates unique fog-scoped AES-GCM key k_i
    """
    return AESGCM.generate_key(bit_length=key_size_bits)


def encrypt(key: bytes, plaintext_value: float) -> bytes:
    """
    Encrypt a sensor reading using AES-256-GCM.

    CT_j = Enc_{k_i}(d_j)

    Args:
        key: AES-GCM key (k_i).
        plaintext_value: Sensor reading d_j (float).

    Returns:
        Ciphertext bytes (nonce || ciphertext || tag).

    Paper: Phase III, Step 3 - Each sensed value d_j encrypted with fog-scoped key
    """
    aesgcm = AESGCM(key)
    nonce = os.urandom(12)  # 96-bit nonce for GCM
    # Encode float as 8-byte double
    plaintext_bytes = struct.pack('d', plaintext_value)
    ciphertext = aesgcm.encrypt(nonce, plaintext_bytes, None)
    # Return nonce || ciphertext (includes GCM tag)
    return nonce + ciphertext


def decrypt(key: bytes, ciphertext_blob: bytes) -> float:
    """
    Decrypt an AES-GCM ciphertext back to the sensor reading.

    d_j = Dec_{k_i}(CT_j)

    Args:
        key: AES-GCM key (k_i).
        ciphertext_blob: Nonce || ciphertext || tag.

    Returns:
        Decrypted sensor reading d_j (float).

    Paper: Phase III, Step 3 - TEE enclave decrypts: d_j = Dec_{k_i}(CT_j)
    """
    nonce = ciphertext_blob[:12]
    ciphertext = ciphertext_blob[12:]
    aesgcm = AESGCM(key)
    plaintext_bytes = aesgcm.decrypt(nonce, ciphertext, None)
    return struct.unpack('d', plaintext_bytes)[0]
