#include <string.h>

#include "logbuf.h"
#include "util.h"

void logbuf_init(struct logbuf *this) {
	P_RWLOCK_INIT(&this->lock, NULL);
	memset(this->entries, 0, sizeof this->entries);
	this->next_id = 0;
}

void logbuf_free(struct logbuf *this) {
	for (int i = 0; i < LOGBUF_CAPACITY; i++) {
		strfree(this->entries[i].remote_ip);
		strfree(this->entries[i].message);
	}
	P_RWLOCK_DESTROY(&this->lock);
}

void logbuf_add(struct logbuf *this, int level, int thread_id, unsigned long long connection_id,
		const char *remote_ip, int remote_port, struct timeval *ts, const char *message) {
	P_RWLOCK_WRLOCK(&this->lock);
	long long id = this->next_id++;
	struct logbuf_entry *e = &this->entries[id % LOGBUF_CAPACITY];

	strfree(e->remote_ip);
	strfree(e->message);

	e->id = id;
	e->ts = *ts;
	e->level = level;
	e->thread_id = thread_id;
	e->connection_id = connection_id;
	e->remote_ip = remote_ip ? mystrdup(remote_ip) : NULL;
	e->remote_port = remote_port;
	e->message = mystrdup(message);
	P_RWLOCK_UNLOCK(&this->lock);
}

/*
 * Return buffered entries with id > since, oldest first, as a JSON array.
 * since < 0 returns everything currently in the buffer.
 */
json_object *logbuf_json_since(struct logbuf *this, long long since) {
	json_object *arr = json_object_new_array();

	P_RWLOCK_RDLOCK(&this->lock);
	long long start = this->next_id > LOGBUF_CAPACITY ? this->next_id - LOGBUF_CAPACITY : 0;
	if (start <= since)
		start = since + 1;

	for (long long id = start; id < this->next_id; id++) {
		struct logbuf_entry *e = &this->entries[id % LOGBUF_CAPACITY];
		if (e->id != id)
			continue;

		json_object *j = json_object_new_object();
		json_object_object_add_ex(j, "id", json_object_new_int64(e->id), JSON_C_CONSTANT_NEW);
		json_object_object_add_ex(j, "level", json_object_new_int(e->level), JSON_C_CONSTANT_NEW);
		json_object_object_add_ex(j, "thread_id", json_object_new_int(e->thread_id), JSON_C_CONSTANT_NEW);
		if (e->connection_id)
			json_object_object_add_ex(j, "connection_id", json_object_new_int64((long long)e->connection_id), JSON_C_CONSTANT_NEW);
		if (e->remote_ip) {
			json_object_object_add_ex(j, "remote_ip", json_object_new_string(e->remote_ip), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(j, "remote_port", json_object_new_int(e->remote_port), JSON_C_CONSTANT_NEW);
		}
		json_object_object_add_ex(j, "message", json_object_new_string(e->message), JSON_C_CONSTANT_NEW);
		timeval_to_json(&e->ts, j, "timestamp");
		json_object_array_add(arr, j);
	}
	P_RWLOCK_UNLOCK(&this->lock);
	return arr;
}
