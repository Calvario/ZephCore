/*
 * SPDX-License-Identifier: MIT
 * ZephCore Identity - Ed25519 key pairs
 */

#pragma once

#include <mesh/Utils.h>
#include <mesh/MeshCore.h>
#include <mesh/RNG.h>
#include <stddef.h>
#include <string.h>

namespace mesh {

class Identity {
public:
	uint8_t pub_key[PUB_KEY_SIZE];

	Identity();
	Identity(const char *pub_hex);
	Identity(const uint8_t *_pub) { memcpy(pub_key, _pub, PUB_KEY_SIZE); }

	int copyHashTo(uint8_t *dest) const {
		memcpy(dest, pub_key, PATH_HASH_SIZE);
		return PATH_HASH_SIZE;
	}
	int copyHashTo(uint8_t *dest, uint8_t len) const {
		memcpy(dest, pub_key, len);
		return len;
	}
	bool isHashMatch(const uint8_t *hash) const {
		return memcmp(hash, pub_key, PATH_HASH_SIZE) == 0;
	}
	bool isHashMatch(const uint8_t *hash, uint8_t len) const {
		return memcmp(hash, pub_key, len) == 0;
	}
	bool verify(const uint8_t *sig, const uint8_t *message, int msg_len) const;
	bool matches(const Identity &other) const { return memcmp(pub_key, other.pub_key, PUB_KEY_SIZE) == 0; }
	bool matches(const uint8_t *other_pubkey) const { return memcmp(pub_key, other_pubkey, PUB_KEY_SIZE) == 0; }
	bool readFrom(const uint8_t *src, size_t len);
	bool writeTo(uint8_t *dest, size_t max_len) const;
};

class LocalIdentity : public Identity {
	uint8_t prv_key[PRV_KEY_SIZE];
public:
	LocalIdentity();
	LocalIdentity(const char *prv_hex, const char *pub_hex);
	LocalIdentity(RNG *rng);

	/* Derive Ed25519 keypair from a 32-byte seed. Use this when you've
	 * already produced a high-quality seed externally (e.g. via the
	 * layered ZephyrRNG::mixIdentitySeed entropy mixer) — avoids the
	 * one-shot-RNG-wrapper dance otherwise needed to feed bytes
	 * through the LocalIdentity(RNG*) constructor. */
	void fromSeed(const uint8_t seed[SEED_SIZE]);

	void sign(uint8_t *sig, const uint8_t *message, int msg_len) const;
	void calcSharedSecret(uint8_t *secret, const Identity &other) const { calcSharedSecret(secret, other.pub_key); }
	void calcSharedSecret(uint8_t *secret, const uint8_t *other_pub_key) const;
	static bool validatePrivateKey(const uint8_t prv[64]);

	/* Arduino-compatible *buffer* format: prv || pub (or prv alone when the
	 * buffer is only PRV_KEY_SIZE).  This is protocol-facing — it is what
	 * CMD_EXPORT/IMPORT_PRIVATE_KEY and the `prv.key` CLI put on the wire —
	 * so it must stay byte-identical to upstream.  Do NOT use it for
	 * on-flash storage; use readFromStorage()/writeToStorage() instead. */
	bool readFrom(const uint8_t *src, size_t len);
	size_t writeTo(uint8_t *dest, size_t max_len) const;

	/* On-flash format: pub || prv, matching Arduino MeshCore's IdentityStore
	 * (LocalIdentity::writeTo(Stream&)).  Kept separate from the buffer
	 * format above because the two orders differ and conflating them is
	 * exactly how a node ends up advertising a key it cannot sign for. */
	size_t writeToStorage(uint8_t *dest, size_t max_len) const;

	/* Tolerant counterpart to writeToStorage(). Accepts every layout either
	 * project has ever written and tells them apart by deriving the pub from
	 * the candidate prv — the stored pub is a 256-bit checksum over the prv,
	 * so a wrong guess cannot match:
	 *
	 *   len == 64  ->  prv only          (ZephCore repeater/room <= 1.17.x)
	 *   len == 96  ->  pub || prv        (Arduino MeshCore, ZephCore >= 1.17.2)
	 *   len == 96  ->  prv || pub        (ZephCore companion <= 1.17.x)
	 *
	 * pub_key is always the derived value, never the stored bytes, so a
	 * loaded identity is coherent by construction.  Returns false when no
	 * layout coheres — the pair is unusable and the caller must not run
	 * with it. */
	bool readFromStorage(const uint8_t *src, size_t len);

	/* Last resort after readFromStorage() fails: one half of a 96-byte blob
	 * may still be an intact private key (the other having been clobbered by
	 * a stale/foreign write).  Tests both candidate halves with
	 * validatePrivateKey() and adopts one only if exactly one qualifies —
	 * ambiguity means we cannot tell which half is which, and guessing wrong
	 * would cement a garbage identity.  Recovers in RAM only: the file is
	 * left alone so the evidence survives and the warning repeats each boot.
	 * Note this changes the node's advertised pub_key to the one its private
	 * key actually owns; contacts must re-add it either way, since the pub it
	 * was advertising was never usable. */
	bool recoverFromStorage(const uint8_t *src, size_t len);
};

} /* namespace mesh */
