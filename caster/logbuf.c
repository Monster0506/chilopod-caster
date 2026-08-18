#include <stdlib.h>
#include <string.h>

#include "logbuf.h"
#include "util.h"

void logbuf_init(struct logbuf *this) {
	P_RWLOCK_INIT(&this->lock, NULL);
	memset(this->entries, 0, sizeof this->entries);
	memset(this->priority_entries, 0, sizeof this->priority_entries);
	this->next_id = 0;
	this->priority_next_id = 0;
}

void logbuf_free(struct logbuf *this) {
	for (int i = 0; i < LOGBUF_CAPACITY; i++) {
		strfree(this->entries[i].remote_ip);
		strfree(this->entries[i].message);
	}
	for (int i = 0; i < LOGBUF_PRIORITY_CAPACITY; i++) {
		strfree(this->priority_entries[i].remote_ip);
		strfree(this->priority_entries[i].message);
	}
	P_RWLOCK_DESTROY(&this->lock);
}

static void logbuf_store(struct logbuf_entry *e, long long id, int level, int thread_id,
		unsigned long long connection_id, const char *remote_ip, int remote_port,
		struct timeval *ts, const char *message) {
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
}

void logbuf_add(struct logbuf *this, int level, int thread_id, unsigned long long connection_id,
		const char *remote_ip, int remote_port, struct timeval *ts, const char *message) {
	P_RWLOCK_WRLOCK(&this->lock);
	long long id = this->next_id++;
	logbuf_store(&this->entries[id % LOGBUF_CAPACITY], id, level, thread_id, connection_id,
		remote_ip, remote_port, ts, message);
	if (level <= LOG_WARNING) {
		long long pid = this->priority_next_id++;
		logbuf_store(&this->priority_entries[pid % LOGBUF_PRIORITY_CAPACITY], id, level, thread_id,
			connection_id, remote_ip, remote_port, ts, message);
	}
	P_RWLOCK_UNLOCK(&this->lock);
}

static void logbuf_entry_add_json(json_object *arr, struct logbuf_entry *e) {
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

static int logbuf_entry_cmp(const void *a, const void *b) {
	long long ida = (*(struct logbuf_entry * const *)a)->id;
	long long idb = (*(struct logbuf_entry * const *)b)->id;
	return (ida > idb) - (ida < idb);
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

	/*
	 * Entries older than "start" have already fallen out of the general
	 * ring -- recover any of them still held in the priority ring, which
	 * only holds LOG_WARNING-or-worse entries and so isn't diluted by
	 * low-severity traffic.
	 */
	struct logbuf_entry *recovered[LOGBUF_PRIORITY_CAPACITY];
	int nrecovered = 0;
	for (int i = 0; i < LOGBUF_PRIORITY_CAPACITY; i++) {
		struct logbuf_entry *e = &this->priority_entries[i];
		if (e->message && e->id > since && e->id < start)
			recovered[nrecovered++] = e;
	}
	qsort(recovered, nrecovered, sizeof(*recovered), logbuf_entry_cmp);
	for (int i = 0; i < nrecovered; i++)
		logbuf_entry_add_json(arr, recovered[i]);

	for (long long id = start; id < this->next_id; id++) {
		struct logbuf_entry *e = &this->entries[id % LOGBUF_CAPACITY];
		if (e->id != id)
			continue;
		logbuf_entry_add_json(arr, e);
	}
	P_RWLOCK_UNLOCK(&this->lock);
	return arr;
}
