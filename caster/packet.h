#ifndef __PACKET_H__
#define __PACKET_H__

#include <assert.h>
#include <stdatomic.h>

#include "conf.h"
#include "refcnt.h"

struct ntrip_state;

enum packet_state {
	PACKET_RAW,
	PACKET_RTCM_UNCHECKED,
	PACKET_RTCM_CHECKED
};

/*
 * A raw packet.
 * Variable-length structure, varies according to packet size.
 */
struct packet {
	REFCNT;
	enum packet_state rtcm_state;
	size_t datalen;
	unsigned char data[];
};

struct caster_state;
struct packet *packet_new(size_t len_raw);
struct packet *packet_new_from_string(const char *s);
int packet_send(struct packet *packet, struct ntrip_state *st, time_t t);

static inline REFCNT_INCREF_BODY(packet_incref, struct packet);
static inline REFCNT_DECREF_BODY(packet_decref, struct packet, free);

#endif
