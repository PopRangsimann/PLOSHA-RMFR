"""
Trusted Execution Environment (TEE) Simulation Module
======================================================
Simulates TEE enclave operations for secure ciphertext transformation,
digital signing, and attestation.

Paper Reference:
  - Phase I, Step 1: Attest(F_i) = 1 for TEE attestation
  - Phase III, Step 3: TEE transforms AES ciphertext → Paillier ciphertext
    d_j = Dec_{k_i}(CT_j), then C_j = Enc_{pk_P}(d_j)
    Plaintext exists only transiently within the protected enclave
  - Phase V, Step 1: σ_i(t) = Sign_{sk_i^TEE}(H(T_i(t)))
"""

import hashlib
import hmac
import os

from src.crypto import aes_gcm
from src.crypto import paillier


class TEEEnclave:
    """
    Simulated Trusted Execution Environment (TEE) enclave.

    Provides:
    - Remote attestation
    - Ciphertext transformation (AES → Paillier)
    - Digital signing within the enclave
    - Isolated key storage
    """

    def __init__(self, node_id: str):
        """
        Initialize a TEE enclave for a fog node.

        Args:
            node_id: Identifier of the fog node hosting this enclave.
        """
        self.node_id = node_id
        self.attested = False
        # TEE signing key (generated during attestation)
        self._signing_key = None
        # Fog-scoped AES key (provisioned by KRM)
        self._aes_key = None
        # Paillier public key (provisioned by KRM)
        self._paillier_pk = None
        # Sequence tracking for replay resistance (Theorem 5)
        self._last_seq = -1

    def attest(self) -> bool:
        """
        Perform remote attestation with KRM.

        Returns:
            True if attestation succeeds (Attest(F_i) = 1).

        Paper: Phase I, Step 1 - Fog node admitted only if Attest(F_i) = 1
        """
        # Generate enclave signing key upon successful attestation
        self._signing_key = os.urandom(32)
        self.attested = True
        return True

    def provision_keys(self, aes_key: bytes, paillier_pk):
        """
        Securely provision cryptographic keys into the enclave.

        Args:
            aes_key: Fog-scoped AES-GCM key k_i.
            paillier_pk: Paillier public key pk_P.

        Paper: Phase I, Step 2-3 - KRM provisions k_i and pk_P to attested enclave
        """
        if not self.attested:
            raise RuntimeError("TEE not attested — cannot provision keys")
        self._aes_key = aes_key
        self._paillier_pk = paillier_pk

    def transform_ciphertext(self, aes_ciphertext: bytes):
        """
        Transform AES-GCM ciphertext to Paillier ciphertext inside the enclave.

        Process:
            1. d_j = Dec_{k_i}(CT_j)          — AES decryption inside enclave
            2. C_j = Enc_{pk_P}(d_j)          — Paillier encryption inside enclave
            3. Erase d_j from enclave memory   — Plaintext transient only

        Args:
            aes_ciphertext: CT_j = Enc_{k_i}(d_j)

        Returns:
            Tuple (paillier_ciphertext, plaintext_value) where plaintext is for
            simulation verification only. In real TEE, plaintext is erased.

        Paper: Phase III, Step 3 - TEE-assisted ciphertext transformation
        """
        if not self.attested or self._aes_key is None:
            raise RuntimeError("TEE not properly initialized")

        # Step 1: Decrypt AES ciphertext inside enclave
        plaintext_value = aes_gcm.decrypt(self._aes_key, aes_ciphertext)

        # Step 2: Encrypt with Paillier inside enclave
        paillier_ct = paillier.encrypt(self._paillier_pk, plaintext_value)

        # Step 3: In a real TEE, plaintext_value would be erased here
        # We return it for verification purposes only
        return paillier_ct, plaintext_value

    def sign(self, data: bytes) -> bytes:
        """
        Generate a digital signature inside the TEE enclave.

        σ_i(t) = Sign_{sk_i^TEE}(H(T_i(t)))

        Args:
            data: Data to sign (typically H(T_i(t))).

        Returns:
            HMAC-SHA256 signature bytes.

        Paper: Phase V, Step 1 - Enclave signs aggregation commitment
        """
        if not self.attested or self._signing_key is None:
            raise RuntimeError("TEE not attested — cannot sign")
        return hmac.new(self._signing_key, data, hashlib.sha256).digest()

    def verify_signature(self, data: bytes, signature: bytes) -> bool:
        """
        Verify a TEE-generated signature.

        Args:
            data: Original data that was signed.
            signature: Signature to verify.

        Returns:
            True if signature is valid.
        """
        expected = self.sign(data)
        return hmac.compare_digest(expected, signature)

    def check_sequence(self, seq: int) -> bool:
        """
        Verify sequence number for replay resistance.

        Seq_i(t) > Seq_i^{last}

        Args:
            seq: Incoming sequence number.

        Returns:
            True if sequence is fresh (not replayed).

        Paper: Theorem 5 - Replay Resistance via unique sequence identifiers
        """
        if seq > self._last_seq:
            self._last_seq = seq
            return True
        return False

    def get_public_verification_key(self) -> bytes:
        """Return the public portion of the signing key for verification."""
        if not self.attested:
            raise RuntimeError("TEE not attested")
        # In simulation, HMAC uses symmetric key. In practice this would
        # be an asymmetric verification key.
        return self._signing_key


def compute_hash(data_tuple: tuple) -> bytes:
    """
    Compute hash of an aggregation package tuple.

    H(T_i(t)) where T_i(t) = (C_i^{final}, Mode_i, Rel_i, RecStatus_i)

    Args:
        data_tuple: Tuple of values to hash.

    Returns:
        SHA-256 hash bytes.
    """
    hasher = hashlib.sha256()
    for item in data_tuple:
        hasher.update(str(item).encode('utf-8'))
    return hasher.digest()
