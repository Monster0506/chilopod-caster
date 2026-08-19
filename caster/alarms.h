#ifndef __ALARMS_H__
#define __ALARMS_H__

#include <sys/time.h>
#include <sys/types.h>

#include <event2/event.h>
#include <json-c/json_object.h>

#include "conf.h"
#include "hash.h"
#include "queue.h"

struct caster_state;

/* Recent alarm outcomes, for GET /adm/api/v1/alarms and the admin UI */
#define ALARM_RING_SIZE 200

enum alarm_event_type {
	ALARM_STATION_OFFLINE = 0,
	ALARM_STATION_ONLINE,
	ALARM_LOW_SV,
	ALARM_POSITION_DRIFT,
	ALARM_EVENT_TYPE_COUNT
};

/*
 * Per-mountpoint alarm tracking. Persists across config reloads (stored in
 * caster_state, not config), keyed by mountpoint name.
 */
struct alarm_mountpoint_state {
	int was_live;			// -1 unknown (first check), 0 offline, 1 online
	struct timeval offline_since;
	int offline_crossed_threshold;	// this outage reached station_offline->after_minutes
	struct timeval online_since;	// zero while offline; set when the source first comes back
	struct timeval low_sv_since;	// zero if not currently under threshold
	struct timeval drift_since;	// zero if not currently over threshold
	struct timeval last_sent[ALARM_EVENT_TYPE_COUNT];
};

/*
 * A ruckus subprocess in flight, tracked from fork() until SIGCHLD reaps it.
 */
struct alarm_send {
	TAILQ_ENTRY(alarm_send) next;
	pid_t pid;
	int stderr_fd;
	struct event *stderr_event;
	char *mountpoint;
	enum alarm_event_type type;
	char *summary;		// human-readable body text, carried through to the ring entry
	char errbuf[512];
	int errlen;
};
TAILQ_HEAD(alarm_sendq, alarm_send);

/* One outcome, written once a ruckus subprocess's exit status is known */
struct alarm_ring_entry {
	struct timeval time;
	char mountpoint[64];
	enum alarm_event_type type;
	char summary[256];
	int sent;		// 1 = accepted by the remote server, 0 = failed
	int suppressed;		// 1 = not sent on purpose (e.g. no subscribed recipients), not a failure
	int exitcode;
	char error[512];	// ruckus's stderr, if sent == 0
};

struct alarms_state {
	struct hash_table *mountpoints;	// mountpoint -> struct alarm_mountpoint_state
	struct event_base *base;
	struct event *tick_event;
	struct event *sigchld_event;
	struct alarm_sendq sends;

	struct alarm_ring_entry ring[ALARM_RING_SIZE];
	int ring_head;		// next write index
	int ring_count;		// valid entries, caps at ALARM_RING_SIZE
	P_RWLOCK_T ring_lock;
};

int alarms_init(struct caster_state *caster);
void alarms_free(struct caster_state *caster);

/* Most-recent-first JSON array of recent alarm outcomes. Caller owns the reference. */
json_object *alarms_ring_json(struct caster_state *caster);

#endif
