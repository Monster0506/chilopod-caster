#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <json-c/json_object.h>

#include "conf.h"
#include "livesource.h"
#include "nodes.h"
#include "ntrip_common.h"
#include "rtcm.h"
#include "sourcetable.h"

/*
 * JSON API routines.
 */

static json_object *api_ntrip_json(struct ntrip_state *st) {
	bufferevent_lock(st->bev);

	json_object *new_obj = json_object_new_object();

	if (st->local) {
		json_object *jip = st->local_addr[0] ? json_object_new_string(st->local_addr) : json_object_new_null();
		json_object *jport = json_object_new_int(ip_port(&st->myaddr));
		json_object *j = json_object_new_object();
		json_object_object_add_ex(j, "ip", jip, JSON_C_CONSTANT_NEW);
		json_object_object_add_ex(j, "port", jport, JSON_C_CONSTANT_NEW);
		json_object_object_add_ex(new_obj, "local", j, JSON_C_CONSTANT_NEW);
	}
	if (st->remote) {
		json_object *jip = st->remote_addr[0] ? json_object_new_string(st->remote_addr) : json_object_new_null();
		json_object *jport = json_object_new_int(ip_port(&st->peeraddr));
		json_object_object_add_ex(new_obj, "ip", jip, JSON_C_CONSTANT_NEW);
		json_object_object_add_ex(new_obj, "port", jport, JSON_C_CONSTANT_NEW);
	}

	json_object *jsonid = json_object_new_int64(st->id);
	json_object *received_bytes = json_object_new_int64(st->received_bytes);
	json_object *sent_bytes = json_object_new_int64(st->sent_bytes);
	json_object_object_add_ex(new_obj, "id", jsonid, JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(new_obj, "received_bytes", received_bytes, JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(new_obj, "sent_bytes", sent_bytes, JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(new_obj, "type", json_object_new_string(st->type), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(new_obj, "wildcard", json_object_new_boolean(st->wildcard), JSON_C_CONSTANT_NEW);
	if (!strcmp(st->type, "source") || !strcmp(st->type, "source_fetcher"))
		json_object_object_add_ex(new_obj, "mountpoint", json_object_new_string(st->mountpoint), JSON_C_CONSTANT_NEW);
	else if (!strcmp(st->type, "client")) {
		if (st->mountpoint != NULL)
			json_object_object_add_ex(new_obj, "mountpoint", json_object_new_string(st->mountpoint), JSON_C_CONSTANT_NEW);
		else
			json_object_object_add_ex(new_obj, "mountpoint", json_object_new_null(), JSON_C_CONSTANT_NEW);
	}

	if (st->user_agent)
		json_object_object_add_ex(new_obj, "user_agent", json_object_new_string(st->user_agent), JSON_C_CONSTANT_NEW);

	struct tcp_info ti;
	socklen_t ti_len = sizeof ti;
	if (getsockopt(st->fd, IPPROTO_TCP, TCP_INFO, &ti, &ti_len) >= 0) {
		json_object *tcpi_obj = json_object_new_object();
		json_object_object_add_ex(tcpi_obj, "rtt", json_object_new_int64(ti.tcpi_rtt), JSON_C_CONSTANT_NEW);
		json_object_object_add_ex(tcpi_obj, "rttvar", json_object_new_int64(ti.tcpi_rttvar), JSON_C_CONSTANT_NEW);
		json_object_object_add_ex(tcpi_obj, "snd_mss", json_object_new_int64(ti.tcpi_snd_mss), JSON_C_CONSTANT_NEW);
		json_object_object_add_ex(tcpi_obj, "rcv_mss", json_object_new_int64(ti.tcpi_rcv_mss), JSON_C_CONSTANT_NEW);
		json_object_object_add_ex(tcpi_obj, "last_data_recv", json_object_new_int64(ti.tcpi_last_data_recv), JSON_C_CONSTANT_NEW);
		json_object_object_add_ex(tcpi_obj, "rcv_wnd", json_object_new_int64(ti.tcpi_rcv_space), JSON_C_CONSTANT_NEW);
#ifdef __FreeBSD__
		// FreeBSD-specific
		json_object_object_add_ex(tcpi_obj, "snd_wnd", json_object_new_int64(ti.tcpi_snd_wnd), JSON_C_CONSTANT_NEW);
		json_object_object_add_ex(tcpi_obj, "snd_rexmitpack", json_object_new_int64(ti.tcpi_snd_rexmitpack), JSON_C_CONSTANT_NEW);
#endif
		json_object_object_add_ex(new_obj, "tcp_info", tcpi_obj, JSON_C_CONSTANT_NEW);
	}

	timeval_to_json(&st->start, new_obj, "start");

	bufferevent_unlock(st->bev);
	return new_obj;
}

/*
 * Return a list of ntrip_state as a JSON object.
 */
struct mime_content *api_ntrip_list_json(struct caster_state *caster, struct request *req) {
	char *s;
	json_object *new_list = json_object_new_object();
	struct ntrip_state *sst;

	P_RWLOCK_RDLOCK(&caster->ntrips.lock);
	TAILQ_FOREACH(sst, &caster->ntrips.queue, nextg) {
		char idstr[40];
		json_object *nj = api_ntrip_json(sst);
		snprintf(idstr, sizeof idstr, "%lld", sst->id);
		json_object_object_add(new_list, idstr, nj);
	}
	P_RWLOCK_UNLOCK(&caster->ntrips.lock);

	s = mystrdup(json_object_to_json_string(new_list));
	struct mime_content *m = mime_new(s, -1, "application/json", 1);
	json_object_put(new_list);
	return m;
}

/*
 * Return the RTCM cache as a JSON object.
 */
struct mime_content *api_rtcm_json(struct caster_state *caster, struct request *req) {
	char *s;
	json_object *new_list;

	if (!caster->rtcm_cache) {
		new_list = json_object_new_null();
	} else {
		new_list = json_object_new_object();
		struct hash_iterator hi;
		struct element *e;
		P_RWLOCK_RDLOCK(&caster->rtcm_lock);
		HASH_FOREACH(e, caster->rtcm_cache, hi) {
			json_object *j = rtcm_info_json((struct rtcm_info *)e->value);
			json_object_object_add(new_list, e->key, j);
		}
		P_RWLOCK_UNLOCK(&caster->rtcm_lock);
	}
	s = mystrdup(json_object_to_json_string(new_list));
	struct mime_content *m = mime_new(s, -1, "application/json", 1);
	json_object_put(new_list);
	return m;
}

/*
 * Return memory stats.
 */
struct mime_content *api_mem_json(struct caster_state *caster, struct request *req) {
	struct mime_content *m = malloc_stats_dump(1);
	return m;
}

/*
 * Return the node table.
 */
struct mime_content *api_nodes_json(struct caster_state *caster, struct request *req) {
	struct json_object *jlist = nodes_json(caster->nodes);
	char *s = mystrdup(json_object_to_json_string(jlist));
	struct mime_content *m = mime_new(s, -1, "application/json", 1);
	json_object_put(jlist);
	return m;
}

/*
 * Reload the configuration and return a status code.
 */
struct mime_content *api_reload_json(struct caster_state *caster, struct request *req) {
	char result[40];
	int r = caster_reload(caster);
	snprintf(result, sizeof result, "{\"result\": %d}\n", r);
	char *s = mystrdup(result);
	struct mime_content *m = mime_new(s, -1, "application/json", 1);
	return m;
}

/*
 * Drop a connection by id.
 */
struct mime_content *api_drop_json(struct caster_state *caster, struct request *req) {
	char result[40];
	int r = 0;
	long long id = -1;
	char *idval = (char *)hash_table_get(req->hash, "id");

	if (idval && sscanf(idval, "%lld", &id) == 1)
		r = ntrip_drop_by_id(caster, id);

	snprintf(result, sizeof result, "{\"result\": %d}\n", r);
	char *s = mystrdup(result);
	struct mime_content *m = mime_new(s, -1, "application/json", 1);
	return m;
}

/*
 * Reject characters that would corrupt the line-based auth/sourcetable file format.
 */
static int field_is_safe(const char *s) {
	for (const char *p = s; *p; p++)
		if (*p == ';' || *p == ':' || *p == '\n' || *p == '\r')
			return 0;
	return 1;
}

static struct mime_content *api_error_json(const char *msg) {
	char result[256];
	snprintf(result, sizeof result, "{\"result\": -1, \"error\": \"%s\"}\n", msg);
	char *s = mystrdup(result);
	return mime_new(s, -1, "application/json", 1);
}

/*
 * Add a new mountpoint: append an entry to source_auth_file and a STR line to
 * sourcetable_file, then reload -- the form-based equivalent of the manual
 * "edit source.auth + sourcetable.dat, then POST /api/v1/reload" workflow.
 */
struct mime_content *api_add_source_json(struct caster_state *caster, struct request *req) {
	struct config *config = req->st->config;
	char *end;

	char *mountpoint = (char *)hash_table_get(req->hash, "mountpoint");
	/*
	 * Named source_password, not password, since this route's admin auth check
	 * already consumes the "password" key from the same request body.
	 */
	char *password = (char *)hash_table_get(req->hash, "source_password");
	if (!mountpoint || !*mountpoint || !password || !*password)
		return api_error_json("mountpoint and source_password are required");
	if (!field_is_safe(mountpoint) || !field_is_safe(password))
		return api_error_json("invalid characters in mountpoint or source_password");

	struct sourceline *existing = stack_find_local_mountpoint(caster, &caster->sourcetablestack, mountpoint);
	if (existing) {
		sourceline_decref(existing);
		return api_error_json("mountpoint already exists");
	}

	char *identifier = (char *)hash_table_get(req->hash, "identifier");
	char *format = (char *)hash_table_get(req->hash, "format");
	char *format_details = (char *)hash_table_get(req->hash, "format_details");
	char *carrier = (char *)hash_table_get(req->hash, "carrier");
	char *nav_system = (char *)hash_table_get(req->hash, "nav_system");
	char *network = (char *)hash_table_get(req->hash, "network");
	char *country = (char *)hash_table_get(req->hash, "country");
	char *lat = (char *)hash_table_get(req->hash, "lat");
	char *lon = (char *)hash_table_get(req->hash, "lon");
	char *solution = (char *)hash_table_get(req->hash, "solution");
	char *generator = (char *)hash_table_get(req->hash, "generator");
	char *bitrate = (char *)hash_table_get(req->hash, "bitrate");

	if (!identifier || !*identifier) identifier = mountpoint;
	if (!format || !*format) format = "RTCM3";
	if (!format_details) format_details = "";
	if (!carrier || !*carrier) carrier = "0";
	if (!nav_system || !*nav_system) nav_system = "GPS";
	if (!network || !*network) network = "NONE";
	if (!country || !*country) country = "NONE";
	if (!lat || !*lat) lat = "0.000";
	if (!lon || !*lon) lon = "0.000";
	if (!solution || !*solution) solution = "0";
	if (!generator || !*generator) generator = "unknown";
	if (!bitrate || !*bitrate) bitrate = "0";

	const char *fields[] = {identifier, format, format_details, carrier, nav_system,
		network, country, lat, lon, solution, generator, bitrate};
	for (unsigned i = 0; i < sizeof(fields)/sizeof(fields[0]); i++)
		if (!field_is_safe(fields[i]))
			return api_error_json("invalid characters in a sourcetable field");

	strtod(lat, &end);
	if (end == lat || *end)
		return api_error_json("lat must be a number");
	strtod(lon, &end);
	if (end == lon || *end)
		return api_error_json("lon must be a number");

	FILE *authf = fopen_absolute(caster->config_dir, config->source_auth_filename, "a");
	if (authf == NULL)
		return api_error_json("cannot open source_auth_file");
	fprintf(authf, "%s:%s:%s\n", mountpoint, mountpoint, password);
	fclose(authf);

	FILE *tablef = fopen_absolute(caster->config_dir, config->sourcetable_filename, "a");
	if (tablef == NULL)
		return api_error_json("cannot open sourcetable_file");
	fprintf(tablef, "STR;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;0;%s;%s;none;N;N;%s;\n",
		mountpoint, identifier, format, format_details, carrier, nav_system,
		network, country, lat, lon, solution, generator, bitrate);
	fclose(tablef);

	int r = caster_reload(caster);
	char result[40];
	snprintf(result, sizeof result, "{\"result\": %d}\n", r);
	char *s = mystrdup(result);
	return mime_new(s, -1, "application/json", 1);
}

struct mime_content *api_sync_json(struct caster_state *caster, struct request *req) {
	const char *type = json_object_get_string(json_object_object_get(req->json, "type"));

	if (type == NULL) {
		req->status = 400;
	} else if (!strcmp(type, "sourcetable")) {
		req->status = sourcetable_update_execute(caster, req->json);
	} else
		req->status = livesource_update_execute(caster, caster->livesources, req);
	char *s = mystrdup("");
	struct mime_content *m = mime_new(s, -1, "application/json", 1);
	return m;
}
