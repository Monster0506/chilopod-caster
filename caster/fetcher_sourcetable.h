#ifndef __FETCHER_SOURCETABLE_H__
#define __FETCHER_SOURCETABLE_H__

#include "caster.h"
#include "refcnt.h"
#include "sourcetable.h"

struct sourcetable_fetch_args {
	REFCNT;
	struct sourcetable *sourcetable;
	int priority;			// priority in a sourcetable stack
	struct ntrip_task *task;

	// Optional Json configuration
	json_object *json_config;
	// Pre-fetched pointer to sub-object "sources" from the above configuration
	json_object *json_sources;
};

struct sourcetable_fetch_args *fetcher_sourcetable_new(struct caster_state *caster,
	const char *host, unsigned short port, int tls, int refresh_delay, int priority,
	const char *json_filename, struct config *config);

REFCNT_INCREF_DECL(fetcher_sourcetable_incref, struct sourcetable_fetch_args);
REFCNT_DECREF_DECL(fetcher_sourcetable_decref, struct sourcetable_fetch_args);

void fetcher_sourcetable_stop(struct sourcetable_fetch_args *this);
void fetcher_sourcetable_reload(struct sourcetable_fetch_args *this, int refresh_delay, int sourcetable_priority);
void fetcher_sourcetable_start(void *arg_cb, int n);
void fetcher_sourcetable_start_with_config(void *arg_cb, int n, struct config *new_config);

#endif
