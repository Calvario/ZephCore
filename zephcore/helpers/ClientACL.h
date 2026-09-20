/*
 * SPDX-License-Identifier: MIT
 * ClientACL - Access Control List for repeater clients
 */

#pragma once

#include <mesh/Mesh.h>
#include <helpers/IdentityStore.h>

#define PERM_ACL_ROLE_MASK     3   // lower 2 bits
#define PERM_ACL_GUEST         0
#define PERM_ACL_READ_ONLY     1
#define PERM_ACL_READ_WRITE    2
#define PERM_ACL_ADMIN         3

#define OUT_PATH_UNKNOWN  0xFF

struct ClientInfo {
	mesh::Identity id;
	uint8_t permissions;
	uint8_t out_path_len;
	uint8_t out_path[MAX_PATH_SIZE];
	uint8_t shared_secret[PUB_KEY_SIZE];
	uint32_t last_timestamp;   // by THEIR clock (transient)
	uint32_t last_activity;    // by OUR clock (transient)
	union {
		struct {
			uint32_t sync_since;       // sync messages SINCE this timestamp (by OUR clock)
			uint32_t pending_ack;
			uint32_t push_post_timestamp;
			unsigned long ack_timeout;
			uint8_t push_failures;
		} room;
	} extra;

	bool isAdmin() const { return (permissions & PERM_ACL_ROLE_MASK) == PERM_ACL_ADMIN; }

	/* Clear all fields properly */
	void clear() {
		id = mesh::Identity();
		permissions = 0;
		/* NOT 0 -- that is a valid path_len meaning "zero-hop direct", so a
		 * cleared slot would claim a usable route and every reply to that
		 * client would go out DIRECT with an empty path, reaching only its
		 * immediate neighbours.  Silent: the sender believes it answered.
		 * putClient() happens to overwrite this today, so nothing currently
		 * observes the 0; the correct default belongs here regardless. */
		out_path_len = OUT_PATH_UNKNOWN;
		memset(out_path, 0, sizeof(out_path));
		memset(shared_secret, 0, sizeof(shared_secret));
		last_timestamp = 0;
		last_activity = 0;
		memset(&extra, 0, sizeof(extra));
	}
};

#ifndef MAX_CLIENTS
	#ifdef CONFIG_ZEPHCORE_MAX_CLIENTS
		#define MAX_CLIENTS  CONFIG_ZEPHCORE_MAX_CLIENTS
	#else
		#define MAX_CLIENTS  32
	#endif
#endif

class ClientACL {
	const char* _path;
	ClientInfo clients[MAX_CLIENTS];
	int num_clients;

public:
	ClientACL() : _path(nullptr), num_clients(0) {
		for (int i = 0; i < MAX_CLIENTS; i++) {
			clients[i].clear();
		}
	}

	void load(const char* path, const mesh::LocalIdentity& self_id);
	void save(const char* path, bool (*filter)(ClientInfo*) = nullptr);
	bool clear();

	ClientInfo* getClient(const uint8_t* pubkey, int key_len);
	ClientInfo* putClient(const mesh::Identity& id, uint8_t init_perms);
	bool applyPermissions(const mesh::LocalIdentity& self_id, const uint8_t* pubkey, int key_len, uint8_t perms);

	int getNumClients() const { return num_clients; }
	ClientInfo* getClientByIdx(int idx) { return &clients[idx]; }
};
