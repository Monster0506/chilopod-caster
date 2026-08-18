#ifndef __LOGBUF_H__
#define __LOGBUF_H__

#include <sys/time.h>

#include <json-c/json.h>

#include "conf.h"
#include "log.h"

/*
 * Two fixed-size in-memory ring buffers of recent log entries, mirroring
 * what gets written to the main log file, so the admin UI can serve a live
 * log view without reading the log file off disk.
 *
 * Entries at LOG_WARNING or more severe are also kept in a separate,
 * smaller ring, so a flood of low-severity (INFO/DEBUG/EDEBUG) traffic
 * can't evict them out of the general ring before the UI gets a chance to
 * show them.
 */

#define LOGBUF_CAPACITY 500
#define LOGBUF_PRIORITY_CAPACITY 200

struct logbuf_entry {
	long long id;
	struct timeval ts;
	int level;
	int thread_id;
	unsigned long long connection_id;	// 0 if none
	char *remote_ip;			// NULL if none, owned
	int remote_port;
	char *message;				// owned
};

struct logbuf {
	P_RWLOCK_T lock;
	struct logbuf_entry entries[LOGBUF_CAPACITY];
	struct logbuf_entry priority_entries[LOGBUF_PRIORITY_CAPACITY];
	long long next_id;
	long long priority_next_id;
};

void logbuf_init(struct logbuf *this);
void logbuf_free(struct logbuf *this);
void logbuf_add(struct logbuf *this, int level, int thread_id, unsigned long long connection_id,
	const char *remote_ip, int remote_port, struct timeval *ts, const char *message);
json_object *logbuf_json_since(struct logbuf *this, long long since);

#endif
