#ifndef __LOGBUF_H__
#define __LOGBUF_H__

#include <sys/time.h>

#include <json-c/json.h>

#include "conf.h"

/*
 * Fixed-size in-memory ring buffer of recent log entries, mirroring what
 * gets written to the main log file, so the admin UI can serve a live log
 * view without reading the log file off disk.
 */

#define LOGBUF_CAPACITY 500

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
	long long next_id;
};

void logbuf_init(struct logbuf *this);
void logbuf_free(struct logbuf *this);
void logbuf_add(struct logbuf *this, int level, int thread_id, unsigned long long connection_id,
	const char *remote_ip, int remote_port, struct timeval *ts, const char *message);
json_object *logbuf_json_since(struct logbuf *this, long long since);

#endif
