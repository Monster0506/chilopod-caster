#ifndef __RTCM_H__
#define __RTCM_H__

#include <stdatomic.h>
#include <sys/time.h>

#include <json-c/json_object.h>

#include "bitfield.h"
#include "hash.h"
#include "packet.h"
#include "refcnt.h"
#include "util.h"

struct ntrip_state;
struct caster_dynconfig;

#define	RTCM_1K_MIN	1000
#define	RTCM_1K_MAX	1230
#define	RTCM_4K_MIN	4000
#define	RTCM_4K_MAX	4095

enum rtcm_conversion {
        RTCM_CONV_MSM7_3,
        RTCM_CONV_MSM7_4
};

struct rtcm_typeset {
	/* bit field for RTCM types 1000-1230 */
	_Atomic unsigned char set1k[(RTCM_1K_MAX-RTCM_1K_MIN+8)>>3];
	/* bit field for RTCM types 4000-4095 */
	_Atomic unsigned char set4k[(RTCM_4K_MAX-RTCM_4K_MIN+8)>>3];
};

struct rtcm_info {
	REFCNT;

	// ECEF coordinates for a base, in tenths of millimeters
	long x, y, z;
	struct rtcm_typeset typeset;
	struct packet *copy1005, *copy1006;
	struct timeval date1005, date1006, posdate;

	int drift_samples;
	int has_lat_lon_drift;		// 1 once at least one sample had a declared position to compare against
	double avg_lat_drift_mm, avg_lon_drift_mm, avg_alt_drift_mm;
	int has_baseline_alt;
	double baseline_alt;

	// Last observed (received) position/altitude decoded from 1005/1006, and
	// the declared position it was last compared against, for alarm reporting.
	pos_t last_observed_pos;
	double last_observed_alt;
	pos_t last_declared_pos;
};

/*
 * RTCM filter description
 */
struct rtcm_filter {
	struct rtcm_typeset pass;		// types to pass directly
	struct rtcm_typeset convert;		// types to convert
	enum rtcm_conversion conversion;	// type of conversion
};

int rtcm_crc_check(struct packet *p);
int rtcm_typeset_parse(struct rtcm_typeset *this, const char *typelist);
char *rtcm_typeset_str(struct rtcm_typeset *this);
struct packet *rtcm_convert_msm7(struct packet *p, int msm_version);
struct hash_table *rtcm_filter_dict_parse(struct rtcm_filter *this, const char *apply);
void rtcm_filter_free(struct rtcm_filter *this);
struct rtcm_filter *rtcm_filter_new(const char *pass, const char *convert, enum rtcm_conversion conversion);
int rtcm_filter_check_mountpoint(struct caster_dynconfig *dyn, const char *mountpoint);
int rtcm_filter_pass(struct rtcm_filter *this, struct packet *packet);
struct packet *rtcm_filter_convert(struct rtcm_filter *this, struct ntrip_state *st, struct packet *p);
struct rtcm_info *rtcm_info_new();
static inline REFCNT_INCREF_BODY(rtcm_info_incref, struct rtcm_info);
REFCNT_DECREF_DECL(rtcm_info_decref, struct rtcm_info);
struct packet *rtcm_info_pos_packet(struct rtcm_info *this, struct caster_state *caster);
void rtcm_info_update_drift(struct rtcm_info *this, pos_t *declared_pos);
json_object *rtcm_info_json(struct rtcm_info *this);
int rtcm_info_get_pos(struct rtcm_info *this, pos_t *pos);
char *rtcm_info_get_nav_system(struct rtcm_info *this);
int rtcm_packet_is_pos(struct packet *p);
int rtcm_packet_handle(struct ntrip_state *st);

/*
 * Return RTCM packet type, or -1 if not a RTCM packet.
 */
static inline unsigned short rtcm_get_type(struct packet *p) {
	unsigned char *d = p->data;
	return (p->rtcm_state != PACKET_RAW) ? getbits(d+3, 0, 12) : -1;
}

#endif
