/*
 * SPDX-License-Identifier: MIT
 * Static pool PacketManager - no dynamic allocation
 */

#include <mesh/StaticPoolPacketManager.h>
#include <mesh/Packet.h>
#include <string.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(zephcore_pktpool, CONFIG_ZEPHCORE_LORA_LOG_LEVEL);

namespace mesh {

#define POOL_SIZE 32
#define QUEUE_SIZE 32

struct PacketQueue {
	Packet *_table[QUEUE_SIZE];
	uint8_t _pri_table[QUEUE_SIZE];
	uint32_t _schedule_table[QUEUE_SIZE];
	int _num;

	PacketQueue() : _num(0) {
		memset(_table, 0, sizeof(_table));
	}

	int countBefore(uint32_t now) const {
		int n = 0;
		for (int j = 0; j < _num; j++) {
			if ((int32_t)(_schedule_table[j] - now) > 0) continue;
			n++;
		}
		return n;
	}

	Packet *get(uint32_t now) {
		uint8_t min_pri = 0xFF;
		int best_idx = -1;
		for (int j = 0; j < _num; j++) {
			if ((int32_t)(_schedule_table[j] - now) > 0) continue;
			if (_pri_table[j] < min_pri) {
				min_pri = _pri_table[j];
				best_idx = j;
			}
		}
		if (best_idx < 0) return nullptr;

		return removeByIdx(best_idx);
	}

	Packet *removeByIdx(int i) {
		if (i >= _num) return nullptr;
		Packet *item = _table[i];
		for (int j = i; j < _num - 1; j++) {
			_table[j] = _table[j + 1];
			_pri_table[j] = _pri_table[j + 1];
			_schedule_table[j] = _schedule_table[j + 1];
		}
		_num--;
		return item;
	}

	bool add(Packet *packet, uint8_t priority, uint32_t scheduled_for) {
		if (_num >= QUEUE_SIZE) return false;
		_table[_num] = packet;
		_pri_table[_num] = priority;
		_schedule_table[_num] = scheduled_for;
		_num++;
		return true;
	}

	/* Find index of the entry with the highest priority number (least
	 * important).  Returns -1 when the queue is empty. */
	int findLowestPriority() const {
		if (_num == 0) return -1;
		uint8_t worst = 0;
		int idx = 0;
		for (int j = 0; j < _num; j++) {
			if (_pri_table[j] >= worst) {
				worst = _pri_table[j];
				idx = j;
			}
		}
		return idx;
	}

	uint32_t scheduleAt(int i) const {
		return (i < _num) ? _schedule_table[i] : 0;
	}

	/* Priority of the next packet that get(now) would return, without
	 * removing it.  Returns 0xFF if no due packet exists. */
	uint8_t peekPriority(uint32_t now) const {
		uint8_t best = 0xFF;
		for (int j = 0; j < _num; j++) {
			if ((int32_t)(_schedule_table[j] - now) > 0) continue;
			if (_pri_table[j] < best) best = _pri_table[j];
		}
		return best;
	}

	bool reschedule(int i, uint32_t new_scheduled_for) {
		if (i >= _num) return false;
		_schedule_table[i] = new_scheduled_for;
		return true;
	}

	int count() const { return _num; }
	Packet *itemAt(int i) const { return (i < _num) ? _table[i] : nullptr; }
};

static Packet _packet_pool[POOL_SIZE];
static PacketQueue _unused;
static PacketQueue _send_queue;
static bool _initialized = false;

static void init_pool() {
	if (_initialized) return;
	_initialized = true;
	for (int i = 0; i < POOL_SIZE; i++) {
		_unused.add(&_packet_pool[i], 0, 0);
	}
}

Packet *StaticPoolPacketManager::allocNew()
{
	init_pool();
	return _unused.removeByIdx(0);
}

void StaticPoolPacketManager::free(Packet *packet)
{
	if (packet == nullptr) return;

	/* _unused is sized to hold the whole pool, so the only way this add can
	 * fail is a slot being returned twice.  That is silent corruption — the
	 * second free drops the packet on the floor and the pool shrinks by one
	 * for the rest of the run — so say so rather than ignoring the return.
	 * Upstream 1d5283de. */
	if (!_unused.add(packet, 0, 0)) {
		LOG_WRN("free: unused queue full, possible double-free");
	}
}

void StaticPoolPacketManager::queueOutbound(Packet *packet, uint8_t priority, uint32_t scheduled_for)
{
	if (packet == nullptr) return;

	if (_send_queue.add(packet, priority, scheduled_for)) return;

	/* Queue full — evict the least-important entry to make room.
	 * Only evict if the new packet is higher priority (lower number). */
	int worst = _send_queue.findLowestPriority();
	if (worst >= 0 && _send_queue._pri_table[worst] > priority) {
		uint8_t evicted_pri = _send_queue._pri_table[worst];
		Packet *evicted = _send_queue.removeByIdx(worst);
		LOG_WRN("queueOutbound: FULL — evicted type=%d pri=%d for type=%d pri=%d",
			evicted->getPayloadType(), evicted_pri,
			packet->getPayloadType(), priority);
		free(evicted);
		_send_queue.add(packet, priority, scheduled_for);
	} else {
		LOG_WRN("queueOutbound: FULL (%d entries) — dropping type=%d pri=%d",
			_send_queue.count(), packet->getPayloadType(), priority);
		free(packet);
	}
}

Packet *StaticPoolPacketManager::getNextOutbound(uint32_t now)
{
	return _send_queue.get(now);
}

int StaticPoolPacketManager::getOutboundCount(uint32_t now) const
{
	return _send_queue.countBefore(now);
}

int StaticPoolPacketManager::getOutboundTotal() const
{
	return _send_queue.count();
}

int StaticPoolPacketManager::getFreeCount() const
{
	return _unused.count();
}

Packet *StaticPoolPacketManager::getOutboundByIdx(int i)
{
	return _send_queue.itemAt(i);
}

Packet *StaticPoolPacketManager::removeOutboundByIdx(int i)
{
	return _send_queue.removeByIdx(i);
}

uint32_t StaticPoolPacketManager::getOutboundSchedule(int i) const
{
	return _send_queue.scheduleAt(i);
}

bool StaticPoolPacketManager::rescheduleOutbound(int i, uint32_t new_scheduled_for)
{
	return _send_queue.reschedule(i, new_scheduled_for);
}

uint8_t StaticPoolPacketManager::peekNextOutboundPriority(uint32_t now) const
{
	return _send_queue.peekPriority(now);
}

} /* namespace mesh */
