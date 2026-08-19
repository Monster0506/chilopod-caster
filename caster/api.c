#include <netinet/tcp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <json-c/json_object.h>
#include <json-c/json_tokener.h>

#include "alarms.h"
#include "auth.h"
#include "conf.h"
#include "livesource.h"
#include "nodes.h"
#include "ntrip_common.h"
#include "rtcm.h"
#include "sidecar.h"
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

		/*
		 * For a client on a virtual (NEAR) mountpoint, the physical base
		 * it's receiving from now; set once NEAR resolves a base.
		 */
		if (st->virtual_mountpoint) {
			json_object_object_add_ex(new_obj, "assigned_base", json_object_new_string(st->virtual_mountpoint), JSON_C_CONSTANT_NEW);
			if (st->last_pos_valid)
				json_object_object_add_ex(new_obj, "dist_to_base_m", json_object_new_double(distance(&st->mountpoint_pos, &st->last_pos)), JSON_C_CONSTANT_NEW);
		}

		if (st->last_pos_valid) {
			json_object_object_add_ex(new_obj, "lat", json_object_new_double(st->last_pos.lat), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(new_obj, "lon", json_object_new_double(st->last_pos.lon), JSON_C_CONSTANT_NEW);

			gga_t *gga = &st->last_gga;
			json_object *jgga = json_object_new_object();
			json_object_object_add_ex(jgga, "time", json_object_new_string(gga->time), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(jgga, "quality", json_object_new_int(gga->quality), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(jgga, "alt", json_object_new_double(gga->alt), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(jgga, "geoid_sep", json_object_new_double(gga->geoid_sep), JSON_C_CONSTANT_NEW);
			if (gga->nsats >= 0)
				json_object_object_add_ex(jgga, "nsats", json_object_new_int(gga->nsats), JSON_C_CONSTANT_NEW);
			if (gga->hdop >= 0)
				json_object_object_add_ex(jgga, "hdop", json_object_new_double(gga->hdop), JSON_C_CONSTANT_NEW);
			if (gga->diff_age >= 0)
				json_object_object_add_ex(jgga, "diff_age", json_object_new_double(gga->diff_age), JSON_C_CONSTANT_NEW);
			if (gga->diff_station >= 0)
				json_object_object_add_ex(jgga, "diff_station", json_object_new_int(gga->diff_station), JSON_C_CONSTANT_NEW);
			timeval_to_json(&st->last_gga_date, jgga, "date");
			json_object_object_add_ex(new_obj, "gga", jgga, JSON_C_CONSTANT_NEW);
		}

		if (st->pos_history_count > 1) {
			json_object *trail = json_object_new_array_ext(st->pos_history_count);
			int start = (st->pos_history_next - st->pos_history_count + POS_HISTORY_SIZE) % POS_HISTORY_SIZE;
			for (int i = 0; i < st->pos_history_count; i++) {
				pos_t *p = &st->pos_history[(start + i) % POS_HISTORY_SIZE];
				json_object *pt = json_object_new_array_ext(2);
				json_object_array_add(pt, json_object_new_double(p->lat));
				json_object_array_add(pt, json_object_new_double(p->lon));
				json_object_array_add(trail, pt);
			}
			json_object_object_add_ex(new_obj, "trail", trail, JSON_C_CONSTANT_NEW);
		}
	}

	if (st->user_agent)
		json_object_object_add_ex(new_obj, "user_agent", json_object_new_string(st->user_agent), JSON_C_CONSTANT_NEW);

	if (st->scheme_basic && st->user)
		json_object_object_add_ex(new_obj, "auth_user", json_object_new_string(st->user), JSON_C_CONSTANT_NEW);

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
	json_object *new_list = json_object_new_object();
	json_object *sidecar_doc = sidecar_stats_json(caster->config_dir, req->st->config);
	json_object *sidecar_stats = NULL;
	json_object *type_names = NULL;
	if (sidecar_doc) {
		json_object_object_get_ex(sidecar_doc, "mountpoints", &sidecar_stats);
		json_object_object_get_ex(sidecar_doc, "type_names", &type_names);
	}

	if (caster->rtcm_cache) {
		struct hash_iterator hi;
		struct element *e;
		P_RWLOCK_RDLOCK(&caster->rtcm_lock);
		HASH_FOREACH(e, caster->rtcm_cache, hi) {
			json_object *j = rtcm_info_json((struct rtcm_info *)e->value);
			json_object *sc;
			if (sidecar_stats && json_object_object_get_ex(sidecar_stats, e->key, &sc)) {
				json_object_object_add_ex(j, "sidecar", json_object_get(sc), JSON_C_CONSTANT_NEW);
			}
			json_object_object_add(new_list, e->key, j);
		}
		P_RWLOCK_UNLOCK(&caster->rtcm_lock);
	}

	if (sidecar_stats) {
		json_object_object_foreach(sidecar_stats, key, val) {
			if (!json_object_object_get_ex(new_list, key, NULL)) {
				json_object *j = json_object_new_object();
				json_object_object_add_ex(j, "sidecar", json_object_get(val), JSON_C_CONSTANT_NEW);
				json_object_object_add(new_list, key, j);
			}
		}
	}

	if (type_names) {
		json_object_object_add_ex(new_list, "_type_names", json_object_get(type_names), JSON_C_CONSTANT_NEW);
	}

	if (sidecar_doc) {
		json_object_put(sidecar_doc);
	}

	if (json_object_object_length(new_list) == 0) {
		json_object_put(new_list);
		new_list = json_object_new_null();
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
 * Return buffered log entries, optionally only those after a given id
 * (?since=<id>), for incremental polling.
 */
struct mime_content *api_log_json(struct caster_state *caster, struct request *req) {
	long long since = -1;
	char *since_str = (char *)hash_table_get(req->hash, "since");
	if (since_str)
		sscanf(since_str, "%lld", &since);

	json_object *arr = logbuf_json_since(&caster->logbuf, since);
	char *s = mystrdup(json_object_to_json_string(arr));
	struct mime_content *m = mime_new(s, -1, "application/json", 1);
	json_object_put(arr);
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
 * Return the source_auth table (source.auth) as a JSON array.
 */
struct mime_content *api_auth_list_json(struct caster_state *caster, struct request *req) {
	struct config *config = req->st->config;
	json_object *new_list = json_object_new_array();

	if (config->source_auth) {
		for (struct auth_entry *a = config->source_auth; a->user; a++) {
			json_object *j = json_object_new_object();
			json_object_object_add_ex(j, "mountpoint", json_object_new_string(a->key), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(j, "user", json_object_new_string(a->user), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(j, "password", json_object_new_string(a->password), JSON_C_CONSTANT_NEW);
			json_object_array_add(new_list, j);
		}
	}

	char *s = mystrdup(json_object_to_json_string(new_list));
	struct mime_content *m = mime_new(s, -1, "application/json", 1);
	json_object_put(new_list);
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
 * Add a new mountpoint: append to source_auth_file and sourcetable_file,
 * then reload; the form-based equivalent of editing both files by hand.
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

/*
 * Rewrite a file, dropping lines whose field at field_index equals value.
 * Returns the number of lines dropped, or -1 on I/O error.
 */
static int remove_matching_lines(const char *dir, const char *filename, char sep, int field_index, const char *value) {
	char *path = joinpath(dir, filename);
	if (path == NULL)
		return -1;

	FILE *in = fopen(path, "r");
	if (in == NULL) {
		strfree(path);
		return -1;
	}

	size_t tmp_len = strlen(path) + 5;
	char *tmp_path = (char *)strmalloc(tmp_len);
	snprintf(tmp_path, tmp_len, "%s.tmp", path);
	FILE *out = fopen(tmp_path, "w");
	if (out == NULL) {
		fclose(in);
		strfree(path);
		strfree(tmp_path);
		return -1;
	}

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	int removed = 0;
	size_t vlen = strlen(value);

	while ((linelen = getline(&line, &linecap, in)) >= 0) {
		int idx = 0;
		char *field_start = line;
		char *field_end = NULL;
		for (char *p = line; *p; p++) {
			if (*p == sep) {
				if (idx == field_index) {
					field_end = p;
					break;
				}
				idx++;
				if (idx == field_index)
					field_start = p + 1;
			}
		}
		int is_match = field_end && (size_t)(field_end - field_start) == vlen
			&& !strncmp(field_start, value, vlen);
		if (is_match)
			removed++;
		else
			fwrite(line, 1, linelen, out);
	}
	free(line);
	fclose(in);
	fclose(out);

	if (rename(tmp_path, path) < 0)
		removed = -1;

	strfree(path);
	strfree(tmp_path);
	return removed;
}

/*
 * Rewrite one field (by index) of the line whose field 0 equals key, in a
 * colon-file like rover.auth. Returns 1 if updated, 0 if no match, -1 on I/O error.
 */
static int set_colon_file_field(const char *dir, const char *filename, char sep, const char *key, int target_field, const char *new_value) {
	char *path = joinpath(dir, filename);
	if (path == NULL)
		return -1;

	FILE *in = fopen(path, "r");
	if (in == NULL) { strfree(path); return -1; }

	size_t tmp_len = strlen(path) + 5;
	char *tmp_path = (char *)strmalloc(tmp_len);
	snprintf(tmp_path, tmp_len, "%s.tmp", path);
	FILE *out = fopen(tmp_path, "w");
	if (out == NULL) { fclose(in); strfree(path); strfree(tmp_path); return -1; }

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	int updated = 0;
	size_t klen = strlen(key);

	while ((linelen = getline(&line, &linecap, in)) >= 0) {
		while (linelen > 0 && (line[linelen-1] == '\n' || line[linelen-1] == '\r'))
			line[--linelen] = '\0';

		char *first_sep = strchr(line, sep);
		int is_match = !updated && first_sep && (size_t)(first_sep - line) == klen
			&& !strncmp(line, key, klen);

		if (!is_match) {
			fprintf(out, "%s\n", line);
			continue;
		}

		int fidx = 0;
		char *seg_start = line;
		for (char *p = line; ; p++) {
			if (*p == sep || *p == '\0') {
				if (fidx == target_field)
					fprintf(out, "%s", new_value);
				else
					fwrite(seg_start, 1, p - seg_start, out);
				if (*p == '\0')
					break;
				fputc(sep, out);
				fidx++;
				seg_start = p + 1;
			}
		}
		fputc('\n', out);
		updated = 1;
	}
	free(line);
	fclose(in);
	fclose(out);

	if (!updated) {
		remove(tmp_path);
		strfree(path);
		strfree(tmp_path);
		return 0;
	}

	int r = rename(tmp_path, path) < 0 ? -1 : 1;
	strfree(path);
	strfree(tmp_path);
	return r;
}

static const struct { const char *name; int level; } LOG_LEVELS[] = {
	{ "EMERG", LOG_EMERG }, { "ALERT", LOG_ALERT }, { "CRIT", LOG_CRIT },
	{ "ERR", LOG_ERR }, { "WARNING", LOG_WARNING }, { "NOTICE", LOG_NOTICE },
	{ "INFO", LOG_INFO }, { "DEBUG", LOG_DEBUG }, { "EDEBUG", LOG_EDEBUG },
};

static const char *log_level_name(int level) {
	for (unsigned i = 0; i < sizeof(LOG_LEVELS)/sizeof(LOG_LEVELS[0]); i++)
		if (LOG_LEVELS[i].level == level)
			return LOG_LEVELS[i].name;
	return NULL;
}

static int log_level_from_name(const char *name) {
	for (unsigned i = 0; i < sizeof(LOG_LEVELS)/sizeof(LOG_LEVELS[0]); i++)
		if (!strcasecmp(LOG_LEVELS[i].name, name))
			return LOG_LEVELS[i].level;
	return -1;
}

static int drop_source_connections(struct caster_state *caster, const char *mountpoint) {
	int r = 0;
restart:
	P_RWLOCK_WRLOCK(&caster->ntrips.lock);
	struct ntrip_state *st;
	TAILQ_FOREACH(st, &caster->ntrips.queue, nextg) {
		if (strcmp(st->type, "source") || !st->mountpoint || strcmp(st->mountpoint, mountpoint))
			continue;
		bufferevent_lock(st->bev);
		ntrip_notify_close(st);
		ntrip_decref_end(st, "drop_source_connections");
		r++;
		bufferevent_unlock(st->bev);
		P_RWLOCK_UNLOCK(&caster->ntrips.lock);
		goto restart;
	}
	P_RWLOCK_UNLOCK(&caster->ntrips.lock);
	return r;
}

struct mime_content *api_remove_source_json(struct caster_state *caster, struct request *req) {
	struct config *config = req->st->config;
	char *mountpoint = (char *)hash_table_get(req->hash, "mountpoint");

	if (!mountpoint || !*mountpoint)
		return api_error_json("mountpoint is required");
	if (!field_is_safe(mountpoint))
		return api_error_json("invalid characters in mountpoint");

	int auth_removed = remove_matching_lines(caster->config_dir, config->source_auth_filename, ':', 0, mountpoint);
	if (auth_removed < 0)
		return api_error_json("cannot rewrite source_auth_file");

	int str_removed = remove_matching_lines(caster->config_dir, config->sourcetable_filename, ';', 1, mountpoint);
	if (str_removed < 0)
		return api_error_json("cannot rewrite sourcetable_file");

	if (auth_removed == 0 && str_removed == 0)
		return api_error_json("mountpoint not found");

	int dropped = drop_source_connections(caster, mountpoint);
	int r = caster_reload(caster);
	char result[80];
	snprintf(result, sizeof result, "{\"result\": %d, \"dropped_connections\": %d}\n", r, dropped);
	char *s = mystrdup(result);
	return mime_new(s, -1, "application/json", 1);
}

struct mime_content *api_auth_set_json(struct caster_state *caster, struct request *req) {
	struct config *config = req->st->config;

	char *mountpoint = (char *)hash_table_get(req->hash, "mountpoint");
	char *auth_user = (char *)hash_table_get(req->hash, "auth_user");
	char *auth_password = (char *)hash_table_get(req->hash, "auth_password");

	if (!mountpoint || !*mountpoint || !auth_user || !*auth_user || !auth_password || !*auth_password)
		return api_error_json("mountpoint, auth_user and auth_password are required");
	if (!field_is_safe(mountpoint) || !field_is_safe(auth_user) || !field_is_safe(auth_password))
		return api_error_json("invalid characters in mountpoint, auth_user or auth_password");

	if (remove_matching_lines(caster->config_dir, config->source_auth_filename, ':', 0, mountpoint) < 0)
		return api_error_json("cannot rewrite source_auth_file");

	FILE *authf = fopen_absolute(caster->config_dir, config->source_auth_filename, "a");
	if (authf == NULL)
		return api_error_json("cannot open source_auth_file");
	fprintf(authf, "%s:%s:%s\n", mountpoint, auth_user, auth_password);
	fclose(authf);

	caster_reload(caster);

	json_object *j = json_object_new_object();
	json_object_object_add_ex(j, "mountpoint", json_object_new_string(mountpoint), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "user", json_object_new_string(auth_user), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "password", json_object_new_string(auth_password), JSON_C_CONSTANT_NEW);
	char *s = mystrdup(json_object_to_json_string(j));
	struct mime_content *m = mime_new(s, -1, "application/json", 1);
	json_object_put(j);
	return m;
}

struct mime_content *api_settings_get_json(struct caster_state *caster, struct request *req) {
	struct config *config = req->st->config;
	json_object *j = json_object_new_object();
	const char *ll = log_level_name(config->log_level);

	json_object_object_add_ex(j, "log_level", json_object_new_string(ll ? ll : "UNKNOWN"), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "hysteresis_m", json_object_new_double(config->hysteresis_m), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "max_nearest_lookup_distance_m", json_object_new_double(config->max_nearest_lookup_distance_m), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "backlog_socket", json_object_new_int64((long long)config->backlog_socket), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "backlog_evbuffer", json_object_new_int64((long long)config->backlog_evbuffer), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "admin_user", json_object_new_string(config->admin_user), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "idle_max_delay", json_object_new_int(config->idle_max_delay), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "reconnect_delay", json_object_new_int(config->reconnect_delay), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "min_raw_packet", json_object_new_int(config->min_raw_packet), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "max_raw_packet", json_object_new_int(config->max_raw_packet), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "http_header_max_size", json_object_new_int64((long long)config->http_header_max_size), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "http_content_length_max", json_object_new_int64((long long)config->http_content_length_max), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "sourcetable_priority", json_object_new_int(config->sourcetable_priority), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "on_demand_source_timeout", json_object_new_int(config->on_demand_source_timeout), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "nearest_base_count_target", json_object_new_int(config->nearest_base_count_target), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "min_nearest_recompute_interval", json_object_new_int(config->min_nearest_recompute_interval), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "max_nearest_recompute_interval", json_object_new_int(config->max_nearest_recompute_interval), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "min_nearest_recompute_pos_delta", json_object_new_double(config->min_nearest_recompute_pos_delta), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "source_read_timeout", json_object_new_int(config->source_read_timeout), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "ntripcli_default_read_timeout", json_object_new_int(config->ntripcli_default_read_timeout), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "ntripcli_default_write_timeout", json_object_new_int(config->ntripcli_default_write_timeout), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "ntripsrv_default_read_timeout", json_object_new_int(config->ntripsrv_default_read_timeout), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "ntripsrv_default_write_timeout", json_object_new_int(config->ntripsrv_default_write_timeout), JSON_C_CONSTANT_NEW);

	json_object_object_add_ex(j, "trusted_http_ip_header",
		config->trusted_http_ip_header ? json_object_new_string(config->trusted_http_ip_header) : json_object_new_null(),
		JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "host_auth_filename",
		config->host_auth_filename ? json_object_new_string(config->host_auth_filename) : json_object_new_null(),
		JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "source_auth_filename", json_object_new_string(config->source_auth_filename), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "blocklist_filename",
		config->blocklist_filename ? json_object_new_string(config->blocklist_filename) : json_object_new_null(),
		JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "sourcetable_filename", json_object_new_string(config->sourcetable_filename), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "access_log",
		config->access_log ? json_object_new_string(config->access_log) : json_object_new_null(),
		JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "log",
		config->log ? json_object_new_string(config->log) : json_object_new_null(),
		JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "ui_dir",
		config->ui_dir ? json_object_new_string(config->ui_dir) : json_object_new_null(),
		JSON_C_CONSTANT_NEW);

	/*
	 * "configured" reflects the config block being present; "ready" is a real
	 * runtime check
	 */
	json_object *jsidecar = json_object_new_object();
	int sidecar_configured = config->sidecar_stats_filename != NULL;
	int sidecar_ready = 0;
	if (sidecar_configured) {
		json_object *sidecar_doc = sidecar_stats_json(caster->config_dir, config);
		if (sidecar_doc) {
			json_object *mountpoints;
			if (json_object_object_get_ex(sidecar_doc, "mountpoints", &mountpoints)
				&& json_object_object_length(mountpoints) > 0)
				sidecar_ready = 1;
			json_object_put(sidecar_doc);
		}
	}
	json_object_object_add_ex(jsidecar, "configured", json_object_new_boolean(sidecar_configured), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(jsidecar, "ready", json_object_new_boolean(sidecar_ready), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(jsidecar, "path",
		config->sidecar_stats_filename ? json_object_new_string(config->sidecar_stats_filename) : json_object_new_null(),
		JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "sidecar", jsidecar, JSON_C_CONSTANT_NEW);

	json_object *jruckus = json_object_new_object();
	int ruckus_configured = config->alarms != NULL;
	int ruckus_ready = 0;
	if (ruckus_configured && config->alarms->ruckus_path)
		ruckus_ready = access(config->alarms->ruckus_path, X_OK) == 0;
	json_object_object_add_ex(jruckus, "configured", json_object_new_boolean(ruckus_configured), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(jruckus, "ready", json_object_new_boolean(ruckus_ready), JSON_C_CONSTANT_NEW);
	const char *ruckus_path = ruckus_configured ? config->alarms->ruckus_path : NULL;
	json_object_object_add_ex(jruckus, "path",
		ruckus_path ? json_object_new_string(ruckus_path) : json_object_new_null(),
		JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "ruckus", jruckus, JSON_C_CONSTANT_NEW);

	if (config->alarms) {
		struct config_alarms *a = config->alarms;
		json_object *ja = json_object_new_object();
		json_object_object_add_ex(ja, "subject",
			a->subject ? json_object_new_string(a->subject) : json_object_new_null(), JSON_C_CONSTANT_NEW);
		json_object_object_add_ex(ja, "min_interval_minutes", json_object_new_int(a->min_interval_minutes), JSON_C_CONSTANT_NEW);
		json_object_object_add_ex(ja, "ruckus_path",
			a->ruckus_path ? json_object_new_string(a->ruckus_path) : json_object_new_null(), JSON_C_CONSTANT_NEW);
		json_object_object_add_ex(ja, "email_template",
			a->email_template ? json_object_new_string(a->email_template) : json_object_new_null(), JSON_C_CONSTANT_NEW);

		if (a->smtp) {
			json_object *jsmtp = json_object_new_object();
			json_object_object_add_ex(jsmtp, "host",
				a->smtp->host ? json_object_new_string(a->smtp->host) : json_object_new_null(), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(jsmtp, "port", json_object_new_int(a->smtp->port), JSON_C_CONSTANT_NEW);
			const char *tls_name = a->smtp->tls == CONFIG_ALARMS_TLS_STARTTLS ? "starttls"
				: a->smtp->tls == CONFIG_ALARMS_TLS_SMTPS ? "smtps" : "none";
			json_object_object_add_ex(jsmtp, "tls", json_object_new_string(tls_name), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(jsmtp, "auth_file",
				a->smtp->auth_file ? json_object_new_string(a->smtp->auth_file) : json_object_new_null(), JSON_C_CONSTANT_NEW);
			/*
			 * List the auth_file's entries so the UI can manage them the
			 * same way it manages rover accounts and source_auth.
			 */
			json_object *jcreds = json_object_new_array();
			if (a->smtp->auth_file) {
				struct auth_entry *entries = auth_parse(caster, a->smtp->auth_file);
				if (entries) {
					for (struct auth_entry *e = entries; e->key; e++) {
						json_object *jc = json_object_new_object();
						json_object_object_add_ex(jc, "host", json_object_new_string(e->key), JSON_C_CONSTANT_NEW);
						json_object_object_add_ex(jc, "user", json_object_new_string(e->user), JSON_C_CONSTANT_NEW);
						json_object_object_add_ex(jc, "password", json_object_new_string(e->password), JSON_C_CONSTANT_NEW);
						json_object_array_add(jcreds, jc);
					}
					auth_free(entries);
				}
			}
			json_object_object_add_ex(jsmtp, "credentials", jcreds, JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(ja, "smtp", jsmtp, JSON_C_CONSTANT_NEW);
		} else {
			json_object_object_add_ex(ja, "smtp", json_object_new_null(), JSON_C_CONSTANT_NEW);
		}

		json_object *jrecipients = json_object_new_array();
		for (int i = 0; i < a->recipients_count; i++) {
			json_object *jr = json_object_new_object();
			json_object_object_add_ex(jr, "name",
				a->recipients[i].name ? json_object_new_string(a->recipients[i].name) : json_object_new_null(), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(jr, "email", json_object_new_string(a->recipients[i].email), JSON_C_CONSTANT_NEW);
			if (a->recipients[i].alarm_types) {
				json_object *jtypes = json_object_new_array_ext(a->recipients[i].alarm_types_count);
				for (int t = 0; t < a->recipients[i].alarm_types_count; t++)
					json_object_array_add(jtypes, json_object_new_string(a->recipients[i].alarm_types[t]));
				json_object_object_add_ex(jr, "alarm_types", jtypes, JSON_C_CONSTANT_NEW);
			} else {
				json_object_object_add_ex(jr, "alarm_types", json_object_new_null(), JSON_C_CONSTANT_NEW);
			}
			json_object_array_add(jrecipients, jr);
		}
		json_object_object_add_ex(ja, "recipients", jrecipients, JSON_C_CONSTANT_NEW);

		json_object *jmountpoints = json_object_new_array();
		for (int i = 0; i < a->mountpoints_count; i++) {
			json_object *jm = json_object_new_object();
			json_object_object_add_ex(jm, "mountpoint", json_object_new_string(a->mountpoints[i].mountpoint), JSON_C_CONSTANT_NEW);
			json_object *jtypes = json_object_new_array_ext(a->mountpoints[i].alarm_types_count);
			for (int t = 0; t < a->mountpoints[i].alarm_types_count; t++)
				json_object_array_add(jtypes, json_object_new_string(a->mountpoints[i].alarm_types[t]));
			json_object_object_add_ex(jm, "alarm_types", jtypes, JSON_C_CONSTANT_NEW);
			json_object_array_add(jmountpoints, jm);
		}
		json_object_object_add_ex(ja, "mountpoints", jmountpoints, JSON_C_CONSTANT_NEW);

		if (a->station_offline) {
			json_object *jt = json_object_new_object();
			json_object_object_add_ex(jt, "after_minutes", json_object_new_int(a->station_offline->after_minutes), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(ja, "station_offline", jt, JSON_C_CONSTANT_NEW);
		} else {
			json_object_object_add_ex(ja, "station_offline", json_object_new_null(), JSON_C_CONSTANT_NEW);
		}
		if (a->station_online) {
			json_object *jt = json_object_new_object();
			json_object_object_add_ex(jt, "after_minutes", json_object_new_int(a->station_online->after_minutes), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(ja, "station_online", jt, JSON_C_CONSTANT_NEW);
		} else {
			json_object_object_add_ex(ja, "station_online", json_object_new_null(), JSON_C_CONSTANT_NEW);
		}
		if (a->low_sv_count) {
			json_object *jt = json_object_new_object();
			json_object_object_add_ex(jt, "min_sats", json_object_new_int(a->low_sv_count->min_sats), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(jt, "after_minutes", json_object_new_int(a->low_sv_count->after_minutes), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(ja, "low_sv_count", jt, JSON_C_CONSTANT_NEW);
		} else {
			json_object_object_add_ex(ja, "low_sv_count", json_object_new_null(), JSON_C_CONSTANT_NEW);
		}
		if (a->position_drift) {
			json_object *jt = json_object_new_object();
			json_object_object_add_ex(jt, "lat_mm", json_object_new_int(a->position_drift->lat_mm), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(jt, "lon_mm", json_object_new_int(a->position_drift->lon_mm), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(jt, "alt_mm", json_object_new_int(a->position_drift->alt_mm), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(jt, "after_minutes", json_object_new_int(a->position_drift->after_minutes), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(ja, "position_drift", jt, JSON_C_CONSTANT_NEW);
		} else {
			json_object_object_add_ex(ja, "position_drift", json_object_new_null(), JSON_C_CONSTANT_NEW);
		}

		json_object_object_add_ex(j, "alarms", ja, JSON_C_CONSTANT_NEW);
	} else {
		json_object_object_add_ex(j, "alarms", json_object_new_null(), JSON_C_CONSTANT_NEW);
	}

	/*
	 * Rover (NTRIP GET client) accounts. Passwords are write-only, never
	 * echoed back, same policy as the SMTP credential and admin password.
	 */
	json_object *jrover = json_object_new_object();
	json_object_object_add_ex(jrover, "configured", json_object_new_boolean(config->rover_auth_filename != NULL), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(jrover, "filename",
		config->rover_auth_filename ? json_object_new_string(config->rover_auth_filename) : json_object_new_null(),
		JSON_C_CONSTANT_NEW);
	json_object *jaccounts = json_object_new_array();
	if (config->rover_auth) {
		for (struct rover_auth_entry *e = config->rover_auth; e->user; e++) {
			json_object *ja2 = json_object_new_object();
			json_object_object_add_ex(ja2, "user", json_object_new_string(e->user), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(ja2, "password", json_object_new_string(e->password), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(ja2, "enabled", json_object_new_boolean(e->enabled), JSON_C_CONSTANT_NEW);
			json_object_array_add(jaccounts, ja2);
		}
	}
	json_object_object_add_ex(jrover, "accounts", jaccounts, JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "rover_auth", jrover, JSON_C_CONSTANT_NEW);

	/*
	 * /adm console accounts (beyond the single admin_user bootstrap account).
	 * Passwords are write-only, never echoed back, same policy as above.
	 */
	json_object *juser = json_object_new_object();
	json_object_object_add_ex(juser, "configured", json_object_new_boolean(config->user_auth_filename != NULL), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(juser, "filename",
		config->user_auth_filename ? json_object_new_string(config->user_auth_filename) : json_object_new_null(),
		JSON_C_CONSTANT_NEW);
	json_object *juseraccounts = json_object_new_array();
	if (config->user_auth) {
		for (struct user_auth_entry *e = config->user_auth; e->user; e++) {
			json_object *ja2 = json_object_new_object();
			json_object_object_add_ex(ja2, "user", json_object_new_string(e->user), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(ja2, "password", json_object_new_string(e->password), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(ja2, "role", json_object_new_string(e->role), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(ja2, "enabled", json_object_new_boolean(e->enabled), JSON_C_CONSTANT_NEW);
			json_object_array_add(juseraccounts, ja2);
		}
	}
	json_object_object_add_ex(juser, "accounts", juseraccounts, JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "user_auth", juser, JSON_C_CONSTANT_NEW);

	char *s = mystrdup(json_object_to_json_string(j));
	struct mime_content *m = mime_new(s, -1, "application/json", 1);
	json_object_put(j);
	return m;
}

/* Must match alarm_event_names[] in alarms.c */
static const char *ALARM_TYPE_NAMES[] = { "station_offline", "station_online", "low_sv_count", "position_drift" };

static int is_valid_alarm_type(const char *s) {
	for (unsigned i = 0; i < sizeof(ALARM_TYPE_NAMES) / sizeof(ALARM_TYPE_NAMES[0]); i++)
		if (!strcmp(ALARM_TYPE_NAMES[i], s))
			return 1;
	return 0;
}


enum settings_field_type { SFT_STRING, SFT_INT, SFT_SIZE, SFT_FLOAT };

struct top_field_def {
	const char *post_key;
	size_t offset;
	enum settings_field_type type;
};

#define TOPFIELD(key, field, ftype) { (key), offsetof(struct config, field), (ftype) }
static const struct top_field_def TOP_FIELDS[] = {
	TOPFIELD("admin_user", admin_user, SFT_STRING),
	TOPFIELD("trusted_http_ip_header", trusted_http_ip_header, SFT_STRING),
	TOPFIELD("host_auth_filename", host_auth_filename, SFT_STRING),
	TOPFIELD("source_auth_filename", source_auth_filename, SFT_STRING),
	TOPFIELD("rover_auth_filename", rover_auth_filename, SFT_STRING),
	TOPFIELD("user_auth_filename", user_auth_filename, SFT_STRING),
	TOPFIELD("blocklist_filename", blocklist_filename, SFT_STRING),
	TOPFIELD("sourcetable_filename", sourcetable_filename, SFT_STRING),
	TOPFIELD("sidecar_stats_filename", sidecar_stats_filename, SFT_STRING),
	TOPFIELD("access_log", access_log, SFT_STRING),
	TOPFIELD("log", log, SFT_STRING),
	TOPFIELD("ui_dir", ui_dir, SFT_STRING),

	TOPFIELD("idle_max_delay", idle_max_delay, SFT_INT),
	TOPFIELD("reconnect_delay", reconnect_delay, SFT_INT),
	TOPFIELD("min_raw_packet", min_raw_packet, SFT_INT),
	TOPFIELD("max_raw_packet", max_raw_packet, SFT_INT),
	TOPFIELD("sourcetable_priority", sourcetable_priority, SFT_INT),
	TOPFIELD("on_demand_source_timeout", on_demand_source_timeout, SFT_INT),
	TOPFIELD("nearest_base_count_target", nearest_base_count_target, SFT_INT),
	TOPFIELD("min_nearest_recompute_interval", min_nearest_recompute_interval, SFT_INT),
	TOPFIELD("max_nearest_recompute_interval", max_nearest_recompute_interval, SFT_INT),
	TOPFIELD("source_read_timeout", source_read_timeout, SFT_INT),
	TOPFIELD("ntripcli_default_read_timeout", ntripcli_default_read_timeout, SFT_INT),
	TOPFIELD("ntripcli_default_write_timeout", ntripcli_default_write_timeout, SFT_INT),
	TOPFIELD("ntripsrv_default_read_timeout", ntripsrv_default_read_timeout, SFT_INT),
	TOPFIELD("ntripsrv_default_write_timeout", ntripsrv_default_write_timeout, SFT_INT),

	TOPFIELD("backlog_socket", backlog_socket, SFT_SIZE),
	TOPFIELD("backlog_evbuffer", backlog_evbuffer, SFT_SIZE),
	TOPFIELD("http_header_max_size", http_header_max_size, SFT_SIZE),
	TOPFIELD("http_content_length_max", http_content_length_max, SFT_SIZE),

	TOPFIELD("hysteresis_m", hysteresis_m, SFT_FLOAT),
	TOPFIELD("max_nearest_lookup_distance_m", max_nearest_lookup_distance_m, SFT_FLOAT),
	TOPFIELD("min_nearest_recompute_pos_delta", min_nearest_recompute_pos_delta, SFT_FLOAT),
	{ NULL, 0, 0 }
};
#undef TOPFIELD

/* Validate and assign value into a scalar C field. errmsg is left
 * untouched on success. */
static int apply_scalar_field(void *field_ptr, enum settings_field_type type, const char *value, const char **errmsg) {
	char *end;
	switch (type) {
	case SFT_STRING:
		if (!*value || !field_is_safe(value)) {
			*errmsg = "invalid or empty value for a setting";
			return -1;
		}
		free(*(char **)field_ptr);
		*(const char **)field_ptr = strdup(value);
		return 0;
	case SFT_INT: {
		long v = strtol(value, &end, 10);
		if (end == value || *end) {
			*errmsg = "a numeric setting is not a valid integer";
			return -1;
		}
		*(int *)field_ptr = (int)v;
		return 0;
	}
	case SFT_SIZE: {
		if (value[0] == '-') {
			*errmsg = "a size setting must be a positive integer";
			return -1;
		}
		unsigned long v = strtoul(value, &end, 10);
		if (end == value || *end || v == 0) {
			*errmsg = "a size setting must be a positive integer";
			return -1;
		}
		*(size_t *)field_ptr = (size_t)v;
		return 0;
	}
	case SFT_FLOAT: {
		double v = strtod(value, &end);
		if (end == value || *end) {
			*errmsg = "a numeric setting is not a valid number";
			return -1;
		}
		*(float *)field_ptr = (float)v;
		return 0;
	}
	}
	return -1;
}

static int apply_string_field(const char **field, const char *value, const char **errmsg) {
	return apply_scalar_field(field, SFT_STRING, value, errmsg);
}

static int apply_int_field(int *field, const char *value, const char **errmsg) {
	return apply_scalar_field(field, SFT_INT, value, errmsg);
}

static struct config_alarms *ensure_alarms(struct config *edit) {
	if (!edit->alarms)
		edit->alarms = (struct config_alarms *)calloc(1, sizeof(struct config_alarms));
	return edit->alarms;
}

static struct config_alarms_smtp *ensure_smtp(struct config_alarms *alarms) {
	if (!alarms->smtp)
		alarms->smtp = (struct config_alarms_smtp *)calloc(1, sizeof(struct config_alarms_smtp));
	return alarms->smtp;
}

/*
 * Parse a comma-separated list of alarm type names into a strdup'd array.
 * Returns -1, leaving out params untouched, if any token is unrecognized.
 */
static int parse_alarm_types(const char *value, const char ***out_types, int *out_count) {
	char *copy = mystrdup(value);
	const char **types = NULL;
	int count = 0;
	int bad = 0;
	if (*copy) {
		char *tok = strtok(copy, ",");
		while (tok) {
			while (*tok == ' ')
				tok++;
			if (!is_valid_alarm_type(tok)) { bad = 1; break; }
			const char **nt = (const char **)realloc((void *)types, (count + 1) * sizeof(*types));
			if (!nt) { bad = 1; break; }
			types = nt;
			types[count++] = strdup(tok);
			tok = strtok(NULL, ",");
		}
	}
	strfree(copy);
	if (bad) {
		for (int j = 0; j < count; j++)
			free((char *)types[j]);
		free((void *)types);
		return -1;
	}
	*out_types = types;
	*out_count = count;
	return 0;
}

/*
 * Replace a recipient list wholesale from a JSON array of
 * {"email", "name", "alarm_types"} objects; empty alarm_types means all.
 */
static int recipients_from_json(json_object *jarr, struct config_alarms_recipient **out, int *out_count, const char **errmsg) {
	if (!json_object_is_type(jarr, json_type_array)) {
		*errmsg = "alarms.recipients.set must be a JSON array";
		return -1;
	}
	int n = json_object_array_length(jarr);
	if (n == 0) {
		*errmsg = "at least one recipient is required";
		return -1;
	}
	struct config_alarms_recipient *recipients = (struct config_alarms_recipient *)calloc(n, sizeof(*recipients));
	if (!recipients) { *errmsg = "out of memory"; return -1; }

	int i;
	for (i = 0; i < n; i++) {
		json_object *jr = json_object_array_get_idx(jarr, i);
		json_object *jemail, *jname, *jtypes;
		const char *email = json_object_object_get_ex(jr, "email", &jemail) ? json_object_get_string(jemail) : NULL;
		if (!email || !*email || !field_is_safe(email)) {
			*errmsg = "invalid or empty recipient email";
			goto fail;
		}
		recipients[i].email = strdup(email);
		if (json_object_object_get_ex(jr, "name", &jname)) {
			const char *name = json_object_get_string(jname);
			if (name && *name) recipients[i].name = strdup(name);
		}
		const char *types_str = json_object_object_get_ex(jr, "alarm_types", &jtypes) ? json_object_get_string(jtypes) : NULL;
		if (parse_alarm_types(types_str ? types_str : "", &recipients[i].alarm_types, &recipients[i].alarm_types_count) < 0) {
			*errmsg = "unknown alarm type in a recipient's alarm_types";
			goto fail;
		}
	}
	*out = recipients;
	*out_count = n;
	return 0;
fail:
	for (int j = 0; j <= i && j < n; j++) {
		free((char *)recipients[j].name);
		free((char *)recipients[j].email);
		for (int k = 0; k < recipients[j].alarm_types_count; k++)
			free((char *)recipients[j].alarm_types[k]);
		free(recipients[j].alarm_types);
	}
	free(recipients);
	return -1;
}

/*
 * Replace the per-mountpoint alarm filter list from a JSON array of
 * {"mountpoint", "alarm_types"} objects; empty alarm_types suppresses all.
 */
static int mountpoints_from_json(json_object *jarr, struct config_alarms_mountpoint **out, int *out_count, const char **errmsg) {
	if (!json_object_is_type(jarr, json_type_array)) {
		*errmsg = "alarms.mountpoints.set must be a JSON array";
		return -1;
	}
	int n = json_object_array_length(jarr);
	struct config_alarms_mountpoint *mountpoints = (struct config_alarms_mountpoint *)calloc(n ? n : 1, sizeof(*mountpoints));
	if (!mountpoints) { *errmsg = "out of memory"; return -1; }

	int i;
	for (i = 0; i < n; i++) {
		json_object *jm = json_object_array_get_idx(jarr, i);
		json_object *jmp, *jtypes;
		const char *mp = json_object_object_get_ex(jm, "mountpoint", &jmp) ? json_object_get_string(jmp) : NULL;
		if (!mp || !*mp || !field_is_safe(mp)) {
			*errmsg = "invalid or empty mountpoint name";
			goto fail;
		}
		for (int k = 0; k < i; k++)
			if (!strcmp(mountpoints[k].mountpoint, mp)) {
				*errmsg = "duplicate mountpoint in alarms.mountpoints.set";
				goto fail;
			}
		mountpoints[i].mountpoint = strdup(mp);
		const char *types_str = json_object_object_get_ex(jm, "alarm_types", &jtypes) ? json_object_get_string(jtypes) : NULL;
		if (parse_alarm_types(types_str ? types_str : "", &mountpoints[i].alarm_types, &mountpoints[i].alarm_types_count) < 0) {
			*errmsg = "unknown alarm type in a mountpoint's alarm_types";
			goto fail;
		}
	}
	*out = mountpoints;
	*out_count = n;
	return 0;
fail:
	for (int j = 0; j <= i && j < n; j++) {
		free((char *)mountpoints[j].mountpoint);
		for (int k = 0; k < mountpoints[j].alarm_types_count; k++)
			free((char *)mountpoints[j].alarm_types[k]);
		free(mountpoints[j].alarm_types);
	}
	free(mountpoints);
	return -1;
}

struct mime_content *api_settings_set_json(struct caster_state *caster, struct request *req) {
	struct config *config = req->st->config;
	int changed = 0;
	const char *errmsg = NULL;

	/* rover_auth.* fields operate on the separate rover_auth_filename file,
	 * not caster.yaml; stays on the live config, independent of the struct-edit below. */
	if (config->rover_auth_filename) {
		char *ra_add_user = (char *)hash_table_get(req->hash, "rover_auth.add.user");
		if (ra_add_user) {
			char *ra_add_password = (char *)hash_table_get(req->hash, "rover_auth.add.password");
			char *ra_add_enabled = (char *)hash_table_get(req->hash, "rover_auth.add.enabled");
			if (!*ra_add_user || !field_is_safe(ra_add_user))
				return api_error_json("invalid or empty rover_auth.add.user");
			if (!ra_add_password || !*ra_add_password || !field_is_safe(ra_add_password))
				return api_error_json("invalid or empty rover_auth.add.password");
			if (config->rover_auth && rover_auth_lookup(config->rover_auth, ra_add_user))
				return api_error_json("that rover user already exists");
			int enabled = !ra_add_enabled || !strcasecmp(ra_add_enabled, "Y")
				|| !strcasecmp(ra_add_enabled, "true") || !strcmp(ra_add_enabled, "1");
			FILE *f = fopen_absolute(caster->config_dir, config->rover_auth_filename, "a");
			if (f == NULL)
				return api_error_json("cannot open rover_auth_filename");
			fprintf(f, "%s:%s:%s\n", ra_add_user, ra_add_password, enabled ? "Y" : "N");
			fclose(f);
			changed = 1;
		}

		char *ra_remove = (char *)hash_table_get(req->hash, "rover_auth.remove");
		if (ra_remove) {
			if (!field_is_safe(ra_remove))
				return api_error_json("invalid characters in rover_auth.remove");
			int r = remove_matching_lines(caster->config_dir, config->rover_auth_filename, ':', 0, ra_remove);
			if (r <= 0)
				return api_error_json(r < 0 ? "cannot rewrite rover_auth_filename" : "that rover user isn't present in the config file");
			changed = 1;
		}

		char *ra_enable_user = (char *)hash_table_get(req->hash, "rover_auth.set_enabled.user");
		if (ra_enable_user) {
			char *ra_enable_value = (char *)hash_table_get(req->hash, "rover_auth.set_enabled.value");
			if (!field_is_safe(ra_enable_user))
				return api_error_json("invalid characters in rover_auth.set_enabled.user");
			if (!ra_enable_value || (strcasecmp(ra_enable_value, "Y") && strcasecmp(ra_enable_value, "N")))
				return api_error_json("rover_auth.set_enabled.value must be Y or N");
			int r = set_colon_file_field(caster->config_dir, config->rover_auth_filename, ':', ra_enable_user, 2, ra_enable_value);
			if (r <= 0)
				return api_error_json(r < 0 ? "cannot rewrite rover_auth_filename" : "that rover user isn't present in the config file");
			changed = 1;
		}

		char *ra_pw_user = (char *)hash_table_get(req->hash, "rover_auth.set_password.user");
		if (ra_pw_user) {
			char *ra_pw_value = (char *)hash_table_get(req->hash, "rover_auth.set_password.value");
			if (!field_is_safe(ra_pw_user))
				return api_error_json("invalid characters in rover_auth.set_password.user");
			if (!ra_pw_value || !*ra_pw_value || !field_is_safe(ra_pw_value))
				return api_error_json("invalid or empty rover_auth.set_password.value");
			int r = set_colon_file_field(caster->config_dir, config->rover_auth_filename, ':', ra_pw_user, 1, ra_pw_value);
			if (r <= 0)
				return api_error_json(r < 0 ? "cannot rewrite rover_auth_filename" : "that rover user isn't present in the config file");
			changed = 1;
		}
	}

	/* user_auth.* fields operate on the separate user_auth_filename text
	 * file, same shape as rover_auth above, plus a role column. */
	if (config->user_auth_filename) {
		char *ua_add_user = (char *)hash_table_get(req->hash, "user_auth.add.user");
		if (ua_add_user) {
			char *ua_add_password = (char *)hash_table_get(req->hash, "user_auth.add.password");
			char *ua_add_role = (char *)hash_table_get(req->hash, "user_auth.add.role");
			if (!*ua_add_user || !field_is_safe(ua_add_user))
				return api_error_json("invalid or empty user_auth.add.user");
			if (!ua_add_password || !*ua_add_password || !field_is_safe(ua_add_password))
				return api_error_json("invalid or empty user_auth.add.password");
			if (!ua_add_role || (strcmp(ua_add_role, "admin") && strcmp(ua_add_role, "viewer")))
				return api_error_json("user_auth.add.role must be admin or viewer");
			if (config->user_auth && user_auth_lookup(config->user_auth, ua_add_user))
				return api_error_json("that console user already exists");
			FILE *f = fopen_absolute(caster->config_dir, config->user_auth_filename, "a");
			if (f == NULL)
				return api_error_json("cannot open user_auth_filename");
			fprintf(f, "%s:%s:%s:Y\n", ua_add_user, ua_add_password, ua_add_role);
			fclose(f);
			changed = 1;
		}

		char *ua_remove = (char *)hash_table_get(req->hash, "user_auth.remove");
		if (ua_remove) {
			if (!field_is_safe(ua_remove))
				return api_error_json("invalid characters in user_auth.remove");
			int r = remove_matching_lines(caster->config_dir, config->user_auth_filename, ':', 0, ua_remove);
			if (r <= 0)
				return api_error_json(r < 0 ? "cannot rewrite user_auth_filename" : "that console user isn't present in the config file");
			changed = 1;
		}

		char *ua_enable_user = (char *)hash_table_get(req->hash, "user_auth.set_enabled.user");
		if (ua_enable_user) {
			char *ua_enable_value = (char *)hash_table_get(req->hash, "user_auth.set_enabled.value");
			if (!field_is_safe(ua_enable_user))
				return api_error_json("invalid characters in user_auth.set_enabled.user");
			if (!ua_enable_value || (strcasecmp(ua_enable_value, "Y") && strcasecmp(ua_enable_value, "N")))
				return api_error_json("user_auth.set_enabled.value must be Y or N");
			int r = set_colon_file_field(caster->config_dir, config->user_auth_filename, ':', ua_enable_user, 3, ua_enable_value);
			if (r <= 0)
				return api_error_json(r < 0 ? "cannot rewrite user_auth_filename" : "that console user isn't present in the config file");
			changed = 1;
		}

		char *ua_pw_user = (char *)hash_table_get(req->hash, "user_auth.set_password.user");
		if (ua_pw_user) {
			char *ua_pw_value = (char *)hash_table_get(req->hash, "user_auth.set_password.value");
			if (!field_is_safe(ua_pw_user))
				return api_error_json("invalid characters in user_auth.set_password.user");
			if (!ua_pw_value || !*ua_pw_value || !field_is_safe(ua_pw_value))
				return api_error_json("invalid or empty user_auth.set_password.value");
			int r = set_colon_file_field(caster->config_dir, config->user_auth_filename, ':', ua_pw_user, 1, ua_pw_value);
			if (r <= 0)
				return api_error_json(r < 0 ? "cannot rewrite user_auth_filename" : "that console user isn't present in the config file");
			changed = 1;
		}

		char *ua_role_user = (char *)hash_table_get(req->hash, "user_auth.set_role.user");
		if (ua_role_user) {
			char *ua_role_value = (char *)hash_table_get(req->hash, "user_auth.set_role.value");
			if (!field_is_safe(ua_role_user))
				return api_error_json("invalid characters in user_auth.set_role.user");
			if (!ua_role_value || (strcmp(ua_role_value, "admin") && strcmp(ua_role_value, "viewer")))
				return api_error_json("user_auth.set_role.value must be admin or viewer");
			int r = set_colon_file_field(caster->config_dir, config->user_auth_filename, ':', ua_role_user, 2, ua_role_value);
			if (r <= 0)
				return api_error_json(r < 0 ? "cannot rewrite user_auth_filename" : "that console user isn't present in the config file");
			changed = 1;
		}
	}

	/*
	 * alarms.smtp_auth.* fields operate on the separate smtp.auth_file,
	 * not caster.yaml; independent of the struct-edit mechanism below.
	 */
	if (config->alarms && config->alarms->smtp && config->alarms->smtp->auth_file) {
		const char *smtp_auth_file = config->alarms->smtp->auth_file;

		char *sa_set_host = (char *)hash_table_get(req->hash, "alarms.smtp_auth.set.host");
		if (sa_set_host) {
			char *sa_set_user = (char *)hash_table_get(req->hash, "alarms.smtp_auth.set.user");
			char *sa_set_password = (char *)hash_table_get(req->hash, "alarms.smtp_auth.set.password");
			if (!*sa_set_host || !field_is_safe(sa_set_host))
				return api_error_json("invalid or empty alarms.smtp_auth.set.host");
			if (!sa_set_user || !*sa_set_user || !field_is_safe(sa_set_user))
				return api_error_json("invalid or empty alarms.smtp_auth.set.user");
			if (!sa_set_password || !*sa_set_password || !field_is_safe(sa_set_password))
				return api_error_json("invalid or empty alarms.smtp_auth.set.password");
			/* Upsert: drop any existing entry for this host, then append the new one. */
			if (remove_matching_lines(caster->config_dir, smtp_auth_file, ':', 0, sa_set_host) < 0)
				return api_error_json("cannot rewrite smtp auth_file");
			FILE *f = fopen_absolute(caster->config_dir, smtp_auth_file, "a");
			if (f == NULL)
				return api_error_json("cannot open smtp auth_file");
			fprintf(f, "%s:%s:%s\n", sa_set_host, sa_set_user, sa_set_password);
			fclose(f);
			changed = 1;
		}

		char *sa_remove = (char *)hash_table_get(req->hash, "alarms.smtp_auth.remove");
		if (sa_remove) {
			if (!field_is_safe(sa_remove))
				return api_error_json("invalid characters in alarms.smtp_auth.remove");
			int r = remove_matching_lines(caster->config_dir, smtp_auth_file, ':', 0, sa_remove);
			if (r <= 0)
				return api_error_json(r < 0 ? "cannot rewrite smtp auth_file" : "that host isn't present in the smtp auth_file");
			changed = 1;
		}
	}

	/*
	 * Everything below mutates a scratch copy of the struct loaded from
	 * caster.yaml and saves it back via cyaml, not hand-rolled text editing.
	 */
	struct config *edit = config_load_for_edit(caster->config_file);
	if (!edit)
		return api_error_json("cannot read config file");
	int yaml_changed = 0;

	char *log_level = (char *)hash_table_get(req->hash, "log_level");
	if (log_level) {
		int lvl = log_level_from_name(log_level);
		if (lvl < 0) { config_free_edit(edit); return api_error_json("invalid log_level"); }
		edit->log_level = lvl;
		changed = 1;
		yaml_changed = 1;
	}

	for (int i = 0; TOP_FIELDS[i].post_key; i++) {
		char *value = (char *)hash_table_get(req->hash, TOP_FIELDS[i].post_key);
		if (!value)
			continue;
		void *field_ptr = (char *)edit + TOP_FIELDS[i].offset;
		if (apply_scalar_field(field_ptr, TOP_FIELDS[i].type, value, &errmsg) < 0) {
			config_free_edit(edit);
			return api_error_json(errmsg);
		}
		changed = 1;
		yaml_changed = 1;
	}

	char *v;

	v = (char *)hash_table_get(req->hash, "alarms.subject");
	if (v) {
		struct config_alarms *a = ensure_alarms(edit);
		if (!a || apply_string_field(&a->subject, v, &errmsg) < 0) {
			config_free_edit(edit);
			return api_error_json(a ? errmsg : "out of memory");
		}
		changed = 1; yaml_changed = 1;
	}
	v = (char *)hash_table_get(req->hash, "alarms.min_interval_minutes");
	if (v) {
		struct config_alarms *a = ensure_alarms(edit);
		if (!a || apply_int_field(&a->min_interval_minutes, v, &errmsg) < 0) {
			config_free_edit(edit);
			return api_error_json(a ? errmsg : "out of memory");
		}
		changed = 1; yaml_changed = 1;
	}
	v = (char *)hash_table_get(req->hash, "alarms.ruckus_path");
	if (v) {
		struct config_alarms *a = ensure_alarms(edit);
		if (!a || apply_string_field(&a->ruckus_path, v, &errmsg) < 0) {
			config_free_edit(edit);
			return api_error_json(a ? errmsg : "out of memory");
		}
		changed = 1; yaml_changed = 1;
	}
	v = (char *)hash_table_get(req->hash, "alarms.email_template");
	if (v) {
		struct config_alarms *a = ensure_alarms(edit);
		if (!a || apply_string_field(&a->email_template, v, &errmsg) < 0) {
			config_free_edit(edit);
			return api_error_json(a ? errmsg : "out of memory");
		}
		changed = 1; yaml_changed = 1;
	}

	v = (char *)hash_table_get(req->hash, "alarms.smtp.host");
	if (v) {
		struct config_alarms *a = ensure_alarms(edit);
		struct config_alarms_smtp *s = a ? ensure_smtp(a) : NULL;
		if (!s || apply_string_field(&s->host, v, &errmsg) < 0) {
			config_free_edit(edit);
			return api_error_json(s ? errmsg : "out of memory");
		}
		changed = 1; yaml_changed = 1;
	}
	v = (char *)hash_table_get(req->hash, "alarms.smtp.port");
	if (v) {
		struct config_alarms *a = ensure_alarms(edit);
		struct config_alarms_smtp *s = a ? ensure_smtp(a) : NULL;
		if (!s) { config_free_edit(edit); return api_error_json("out of memory"); }
		char *end;
		long portval = strtol(v, &end, 10);
		if (end == v || *end || portval < 0 || portval > 65535) {
			config_free_edit(edit);
			return api_error_json("alarms.smtp.port must be a valid port number");
		}
		s->port = (unsigned short)portval;
		changed = 1; yaml_changed = 1;
	}
	v = (char *)hash_table_get(req->hash, "alarms.smtp.auth_file");
	if (v) {
		struct config_alarms *a = ensure_alarms(edit);
		struct config_alarms_smtp *s = a ? ensure_smtp(a) : NULL;
		if (!s || apply_string_field(&s->auth_file, v, &errmsg) < 0) {
			config_free_edit(edit);
			return api_error_json(s ? errmsg : "out of memory");
		}
		changed = 1; yaml_changed = 1;
	}
	v = (char *)hash_table_get(req->hash, "alarms.smtp.tls");
	if (v) {
		if (strcasecmp(v, "none") && strcasecmp(v, "starttls") && strcasecmp(v, "smtps")) {
			config_free_edit(edit);
			return api_error_json("alarms.smtp.tls must be none, starttls or smtps");
		}
		struct config_alarms *a = ensure_alarms(edit);
		struct config_alarms_smtp *s = a ? ensure_smtp(a) : NULL;
		if (!s) { config_free_edit(edit); return api_error_json("out of memory"); }
		s->tls = !strcasecmp(v, "starttls") ? CONFIG_ALARMS_TLS_STARTTLS
			: !strcasecmp(v, "smtps") ? CONFIG_ALARMS_TLS_SMTPS : CONFIG_ALARMS_TLS_NONE;
		changed = 1; yaml_changed = 1;
	}

	if (hash_table_get(req->hash, "alarms.station_offline.enable")) {
		struct config_alarms *a = ensure_alarms(edit);
		if (!a) { config_free_edit(edit); return api_error_json("out of memory"); }
		if (!a->station_offline)
			a->station_offline = (struct config_alarms_threshold *)calloc(1, sizeof(struct config_alarms_threshold));
		if (!a->station_offline) { config_free_edit(edit); return api_error_json("out of memory"); }
		changed = 1; yaml_changed = 1;
	}
	if (hash_table_get(req->hash, "alarms.station_offline.remove") && edit->alarms && edit->alarms->station_offline) {
		free(edit->alarms->station_offline);
		edit->alarms->station_offline = NULL;
		changed = 1; yaml_changed = 1;
	}
	v = (char *)hash_table_get(req->hash, "alarms.station_offline.after_minutes");
	if (v) {
		struct config_alarms *a = ensure_alarms(edit);
		if (!a) { config_free_edit(edit); return api_error_json("out of memory"); }
		if (!a->station_offline)
			a->station_offline = (struct config_alarms_threshold *)calloc(1, sizeof(struct config_alarms_threshold));
		if (!a->station_offline || apply_int_field(&a->station_offline->after_minutes, v, &errmsg) < 0) {
			config_free_edit(edit);
			return api_error_json(a->station_offline ? errmsg : "out of memory");
		}
		changed = 1; yaml_changed = 1;
	}

	if (hash_table_get(req->hash, "alarms.station_online.enable")) {
		struct config_alarms *a = ensure_alarms(edit);
		if (!a) { config_free_edit(edit); return api_error_json("out of memory"); }
		if (!a->station_online)
			a->station_online = (struct config_alarms_threshold *)calloc(1, sizeof(struct config_alarms_threshold));
		if (!a->station_online) { config_free_edit(edit); return api_error_json("out of memory"); }
		changed = 1; yaml_changed = 1;
	}
	if (hash_table_get(req->hash, "alarms.station_online.remove") && edit->alarms && edit->alarms->station_online) {
		free(edit->alarms->station_online);
		edit->alarms->station_online = NULL;
		changed = 1; yaml_changed = 1;
	}
	v = (char *)hash_table_get(req->hash, "alarms.station_online.after_minutes");
	if (v) {
		struct config_alarms *a = ensure_alarms(edit);
		if (!a) { config_free_edit(edit); return api_error_json("out of memory"); }
		if (!a->station_online)
			a->station_online = (struct config_alarms_threshold *)calloc(1, sizeof(struct config_alarms_threshold));
		if (!a->station_online || apply_int_field(&a->station_online->after_minutes, v, &errmsg) < 0) {
			config_free_edit(edit);
			return api_error_json(a->station_online ? errmsg : "out of memory");
		}
		changed = 1; yaml_changed = 1;
	}

	if (hash_table_get(req->hash, "alarms.low_sv_count.enable")) {
		struct config_alarms *a = ensure_alarms(edit);
		if (!a) { config_free_edit(edit); return api_error_json("out of memory"); }
		if (!a->low_sv_count)
			a->low_sv_count = (struct config_alarms_low_sv *)calloc(1, sizeof(struct config_alarms_low_sv));
		if (!a->low_sv_count) { config_free_edit(edit); return api_error_json("out of memory"); }
		changed = 1; yaml_changed = 1;
	}
	if (hash_table_get(req->hash, "alarms.low_sv_count.remove") && edit->alarms && edit->alarms->low_sv_count) {
		free(edit->alarms->low_sv_count);
		edit->alarms->low_sv_count = NULL;
		changed = 1; yaml_changed = 1;
	}
	char *lv_min_sats = (char *)hash_table_get(req->hash, "alarms.low_sv_count.min_sats");
	char *lv_after = (char *)hash_table_get(req->hash, "alarms.low_sv_count.after_minutes");
	if (lv_min_sats || lv_after) {
		struct config_alarms *a = ensure_alarms(edit);
		if (!a) { config_free_edit(edit); return api_error_json("out of memory"); }
		if (!a->low_sv_count)
			a->low_sv_count = (struct config_alarms_low_sv *)calloc(1, sizeof(struct config_alarms_low_sv));
		if (!a->low_sv_count) { config_free_edit(edit); return api_error_json("out of memory"); }
		if (lv_min_sats && apply_int_field(&a->low_sv_count->min_sats, lv_min_sats, &errmsg) < 0) {
			config_free_edit(edit);
			return api_error_json(errmsg);
		}
		if (lv_after && apply_int_field(&a->low_sv_count->after_minutes, lv_after, &errmsg) < 0) {
			config_free_edit(edit);
			return api_error_json(errmsg);
		}
		changed = 1; yaml_changed = 1;
	}

	if (hash_table_get(req->hash, "alarms.position_drift.enable")) {
		struct config_alarms *a = ensure_alarms(edit);
		if (!a) { config_free_edit(edit); return api_error_json("out of memory"); }
		if (!a->position_drift)
			a->position_drift = (struct config_alarms_position_drift *)calloc(1, sizeof(struct config_alarms_position_drift));
		if (!a->position_drift) { config_free_edit(edit); return api_error_json("out of memory"); }
		changed = 1; yaml_changed = 1;
	}
	if (hash_table_get(req->hash, "alarms.position_drift.remove") && edit->alarms && edit->alarms->position_drift) {
		free(edit->alarms->position_drift);
		edit->alarms->position_drift = NULL;
		changed = 1; yaml_changed = 1;
	}
	char *pd_lat = (char *)hash_table_get(req->hash, "alarms.position_drift.lat_mm");
	char *pd_lon = (char *)hash_table_get(req->hash, "alarms.position_drift.lon_mm");
	char *pd_alt = (char *)hash_table_get(req->hash, "alarms.position_drift.alt_mm");
	char *pd_after = (char *)hash_table_get(req->hash, "alarms.position_drift.after_minutes");
	if (pd_lat || pd_lon || pd_alt || pd_after) {
		struct config_alarms *a = ensure_alarms(edit);
		if (!a) { config_free_edit(edit); return api_error_json("out of memory"); }
		if (!a->position_drift)
			a->position_drift = (struct config_alarms_position_drift *)calloc(1, sizeof(struct config_alarms_position_drift));
		if (!a->position_drift) { config_free_edit(edit); return api_error_json("out of memory"); }
		if (pd_lat && apply_int_field(&a->position_drift->lat_mm, pd_lat, &errmsg) < 0) { config_free_edit(edit); return api_error_json(errmsg); }
		if (pd_lon && apply_int_field(&a->position_drift->lon_mm, pd_lon, &errmsg) < 0) { config_free_edit(edit); return api_error_json(errmsg); }
		if (pd_alt && apply_int_field(&a->position_drift->alt_mm, pd_alt, &errmsg) < 0) { config_free_edit(edit); return api_error_json(errmsg); }
		if (pd_after && apply_int_field(&a->position_drift->after_minutes, pd_after, &errmsg) < 0) { config_free_edit(edit); return api_error_json(errmsg); }
		changed = 1; yaml_changed = 1;
	}

	v = (char *)hash_table_get(req->hash, "alarms.recipients.set");
	if (v) {
		json_object *jarr = json_tokener_parse(v);
		if (!jarr) { config_free_edit(edit); return api_error_json("alarms.recipients.set is not valid JSON"); }
		struct config_alarms_recipient *recipients;
		int count;
		if (recipients_from_json(jarr, &recipients, &count, &errmsg) < 0) {
			json_object_put(jarr);
			config_free_edit(edit);
			return api_error_json(errmsg);
		}
		json_object_put(jarr);
		struct config_alarms *a = ensure_alarms(edit);
		if (!a) { config_free_edit(edit); return api_error_json("out of memory"); }
		for (int i = 0; i < a->recipients_count; i++) {
			free((char *)a->recipients[i].name);
			free((char *)a->recipients[i].email);
			for (int j = 0; j < a->recipients[i].alarm_types_count; j++)
				free((char *)a->recipients[i].alarm_types[j]);
			free(a->recipients[i].alarm_types);
		}
		free(a->recipients);
		a->recipients = recipients;
		a->recipients_count = count;
		changed = 1; yaml_changed = 1;
	}

	v = (char *)hash_table_get(req->hash, "alarms.mountpoints.set");
	if (v) {
		json_object *jarr = json_tokener_parse(v);
		if (!jarr) { config_free_edit(edit); return api_error_json("alarms.mountpoints.set is not valid JSON"); }
		struct config_alarms_mountpoint *mountpoints;
		int count;
		if (mountpoints_from_json(jarr, &mountpoints, &count, &errmsg) < 0) {
			json_object_put(jarr);
			config_free_edit(edit);
			return api_error_json(errmsg);
		}
		json_object_put(jarr);
		struct config_alarms *a = ensure_alarms(edit);
		if (!a) { config_free_edit(edit); return api_error_json("out of memory"); }
		for (int i = 0; i < a->mountpoints_count; i++) {
			free((char *)a->mountpoints[i].mountpoint);
			for (int j = 0; j < a->mountpoints[i].alarm_types_count; j++)
				free((char *)a->mountpoints[i].alarm_types[j]);
			free(a->mountpoints[i].alarm_types);
		}
		free(a->mountpoints);
		a->mountpoints = mountpoints;
		a->mountpoints_count = count;
		changed = 1; yaml_changed = 1;
	}

	if (!changed) {
		config_free_edit(edit);
		return api_error_json("no settings provided");
	}

	if (yaml_changed && config_save_for_edit(caster->config_file, edit) < 0) {
		config_free_edit(edit);
		return api_error_json("cannot write config file");
	}
	config_free_edit(edit);

	int r = caster_reload(caster);
	char result[40];
	snprintf(result, sizeof result, "{\"result\": %d}\n", r);
	char *rs = mystrdup(result);
	return mime_new(rs, -1, "application/json", 1);
}

/*
 * Rewrite the sourcetable line for mountpoint (matched on field 1),
 * replacing target_field. Returns 1 if updated, 0 if no match, -1 on I/O error.
 */
static int update_sourcetable_field(const char *dir, const char *filename, const char *mountpoint, int target_field, const char *new_value) {
	char *path = joinpath(dir, filename);
	if (path == NULL)
		return -1;

	FILE *in = fopen(path, "r");
	if (in == NULL) {
		strfree(path);
		return -1;
	}

	size_t tmp_len = strlen(path) + 5;
	char *tmp_path = (char *)strmalloc(tmp_len);
	snprintf(tmp_path, tmp_len, "%s.tmp", path);
	FILE *out = fopen(tmp_path, "w");
	if (out == NULL) {
		fclose(in);
		strfree(path);
		strfree(tmp_path);
		return -1;
	}

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	int updated = 0;
	size_t mlen = strlen(mountpoint);

	while ((linelen = getline(&line, &linecap, in)) >= 0) {
		while (linelen > 0 && (line[linelen-1] == '\n' || line[linelen-1] == '\r'))
			line[--linelen] = '\0';

		int idx = 0;
		char *field_start = line;
		char *field_end = NULL;
		for (char *p = line; *p; p++) {
			if (*p == ';') {
				if (idx == 1) {
					field_end = p;
					break;
				}
				idx++;
				if (idx == 1)
					field_start = p + 1;
			}
		}
		int is_match = !updated && field_end && (size_t)(field_end - field_start) == mlen
			&& !strncmp(field_start, mountpoint, mlen);

		if (!is_match) {
			fprintf(out, "%s\n", line);
			continue;
		}

		int fidx = 0;
		char *seg_start = line;
		for (char *p = line; ; p++) {
			if (*p == ';' || *p == '\0') {
				if (fidx == target_field)
					fprintf(out, "%s", new_value);
				else
					fwrite(seg_start, 1, p - seg_start, out);
				if (*p == '\0')
					break;
				fputc(';', out);
				fidx++;
				seg_start = p + 1;
			}
		}
		fputc('\n', out);
		updated = 1;
	}
	free(line);
	fclose(in);
	fclose(out);

	if (rename(tmp_path, path) < 0)
		updated = -1;

	strfree(path);
	strfree(tmp_path);
	return updated;
}

/*
 * Update a mountpoint's sourcetable format-details, nav-system, and position
 * fields from the RTCM3 message types actually decoded for it.
 */
struct mime_content *api_detect_source_json(struct caster_state *caster, struct request *req) {
	struct config *config = req->st->config;
	char *mountpoint = (char *)hash_table_get(req->hash, "mountpoint");

	if (!mountpoint || !*mountpoint)
		return api_error_json("mountpoint is required");
	if (!field_is_safe(mountpoint))
		return api_error_json("invalid characters in mountpoint");

	char *types = NULL;
	char *nav_system = NULL;
	pos_t pos;
	int have_pos = 0;
	P_RWLOCK_RDLOCK(&caster->rtcm_lock);
	if (caster->rtcm_cache) {
		struct rtcm_info *info = (struct rtcm_info *)hash_table_get(caster->rtcm_cache, mountpoint);
		if (info) {
			types = rtcm_typeset_str(&info->typeset);
			nav_system = rtcm_info_get_nav_system(info);
			have_pos = rtcm_info_get_pos(info, &pos);
		}
	}
	P_RWLOCK_UNLOCK(&caster->rtcm_lock);

	if (!types || !*types) {
		strfree(types);
		strfree(nav_system);
		return api_error_json("no RTCM3 data observed yet for this mountpoint -- it may not be connected yet, or isn't RTCM3");
	}

	int r = update_sourcetable_field(caster->config_dir, config->sourcetable_filename, mountpoint, 4, types);
	if (r <= 0) {
		strfree(types);
		strfree(nav_system);
		return api_error_json(r < 0 ? "cannot rewrite sourcetable_file" : "mountpoint not found in sourcetable");
	}

	if (nav_system)
		update_sourcetable_field(caster->config_dir, config->sourcetable_filename, mountpoint, 6, nav_system);

	if (have_pos) {
		char latstr[32], lonstr[32];
		snprintf(latstr, sizeof latstr, "%.6f", pos.lat);
		snprintf(lonstr, sizeof lonstr, "%.6f", pos.lon);
		update_sourcetable_field(caster->config_dir, config->sourcetable_filename, mountpoint, 9, latstr);
		update_sourcetable_field(caster->config_dir, config->sourcetable_filename, mountpoint, 10, lonstr);
	}

	caster_reload(caster);
	char result[400];
	int n = snprintf(result, sizeof result, "{\"result\": 0, \"types\": \"%s\"", types);
	if (nav_system)
		n += snprintf(result+n, sizeof(result)-n, ", \"nav_system\": \"%s\"", nav_system);
	if (have_pos)
		n += snprintf(result+n, sizeof(result)-n, ", \"lat\": %.6f, \"lon\": %.6f", pos.lat, pos.lon);
	snprintf(result+n, sizeof(result)-n, "}\n");
	strfree(types);
	strfree(nav_system);
	char *s = mystrdup(result);
	return mime_new(s, -1, "application/json", 1);
}

/*
 * Update an existing mountpoint's group 
 */
struct mime_content *api_edit_source_json(struct caster_state *caster, struct request *req) {
	struct config *config = req->st->config;
	char *mountpoint = (char *)hash_table_get(req->hash, "mountpoint");
	char *group = (char *)hash_table_get(req->hash, "group");
	char *identifier = (char *)hash_table_get(req->hash, "identifier");

	if (!mountpoint || !*mountpoint)
		return api_error_json("mountpoint is required");
	if (!field_is_safe(mountpoint))
		return api_error_json("invalid characters in mountpoint");

	if (group) {
		if (!*group)
			group = "NONE";
		if (!field_is_safe(group))
			return api_error_json("invalid characters in group");
		int r = update_sourcetable_field(caster->config_dir, config->sourcetable_filename, mountpoint, 7, group);
		if (r <= 0)
			return api_error_json(r < 0 ? "cannot rewrite sourcetable_file" : "mountpoint not found in sourcetable");
	}

	if (identifier) {
		if (!*identifier)
			identifier = mountpoint;
		if (!field_is_safe(identifier))
			return api_error_json("invalid characters in identifier");
		int r = update_sourcetable_field(caster->config_dir, config->sourcetable_filename, mountpoint, 2, identifier);
		if (r <= 0)
			return api_error_json(r < 0 ? "cannot rewrite sourcetable_file" : "mountpoint not found in sourcetable");
	}

	caster_reload(caster);
	char *s = mystrdup("{\"result\": 0}\n");
	return mime_new(s, -1, "application/json", 1);
}

/*
 * Diagnose NEAR base selection for a position: list candidate physical
 * bases sorted by distance
 */
struct mime_content *api_near_json(struct caster_state *caster, struct request *req) {
	struct config *config = req->st->config;
	char *idval = (char *)hash_table_get(req->hash, "id");
	char *latval = (char *)hash_table_get(req->hash, "lat");
	char *lonval = (char *)hash_table_get(req->hash, "lon");

	pos_t pos;
	char *current_base = NULL;

	if (idval && *idval) {
		long long id;
		if (sscanf(idval, "%lld", &id) != 1)
			return api_error_json("invalid id");

		struct ntrip_state *st;
		int found = 0;
		P_RWLOCK_RDLOCK(&caster->ntrips.lock);
		TAILQ_FOREACH(st, &caster->ntrips.queue, nextg) {
			if (st->id == id) {
				bufferevent_lock(st->bev);
				if (st->last_pos_valid) {
					pos = st->last_pos;
					found = 1;
				}
				if (st->virtual_mountpoint)
					current_base = mystrdup(st->virtual_mountpoint);
				bufferevent_unlock(st->bev);
				break;
			}
		}
		P_RWLOCK_UNLOCK(&caster->ntrips.lock);

		if (!found) {
			strfree(current_base);
			return api_error_json("connection not found, or no position reported yet");
		}
	} else if (latval && lonval) {
		if (sscanf(latval, "%f", &pos.lat) != 1 || sscanf(lonval, "%f", &pos.lon) != 1)
			return api_error_json("invalid lat/lon");
	} else
		return api_error_json("id or lat/lon is required");

	struct sourcetable *pos_sourcetable = stack_flatten_dist(caster, &caster->sourcetablestack, &pos, config->max_nearest_lookup_distance_m);
	if (pos_sourcetable == NULL) {
		strfree(current_base);
		return api_error_json("could not build sourcetable");
	}

	struct dist_table *d = sourcetable_find_pos(pos_sourcetable, &pos);
	sourcetable_decref(pos_sourcetable);
	if (d == NULL) {
		strfree(current_base);
		return api_error_json("could not compute distances");
	}

	json_object *j = json_object_new_object();

	json_object *jpos = json_object_new_object();
	json_object_object_add_ex(jpos, "lat", json_object_new_double(pos.lat), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(jpos, "lon", json_object_new_double(pos.lon), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "pos", jpos, JSON_C_CONSTANT_NEW);

	json_object *jcandidates = json_object_new_array_ext(d->size_dist_array);
	for (int i = 0; i < d->size_dist_array; i++) {
		json_object *jc = json_object_new_object();
		json_object_object_add_ex(jc, "mountpoint", json_object_new_string(d->dist_array[i].mountpoint), JSON_C_CONSTANT_NEW);
		json_object_object_add_ex(jc, "dist_m", json_object_new_double(d->dist_array[i].dist), JSON_C_CONSTANT_NEW);
		json_object_object_add_ex(jc, "lat", json_object_new_double(d->dist_array[i].pos.lat), JSON_C_CONSTANT_NEW);
		json_object_object_add_ex(jc, "lon", json_object_new_double(d->dist_array[i].pos.lon), JSON_C_CONSTANT_NEW);
		json_object_array_add(jcandidates, jc);
	}
	json_object_object_add_ex(j, "candidates", jcandidates, JSON_C_CONSTANT_NEW);

	const char *assigned = current_base ? current_base : (d->size_dist_array > 0 ? d->dist_array[0].mountpoint : NULL);
	json_object_object_add_ex(j, "assigned_base", assigned ? json_object_new_string(assigned) : json_object_new_null(), JSON_C_CONSTANT_NEW);

	json_object_object_add_ex(j, "hysteresis_m", json_object_new_double(config->hysteresis_m), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "lookup_dist_m", json_object_new_double(config->max_nearest_lookup_distance_m), JSON_C_CONSTANT_NEW);

	strfree(current_base);
	dist_table_free(d);

	char *s = mystrdup(json_object_to_json_string(j));
	struct mime_content *m = mime_new(s, -1, "application/json", 1);
	json_object_put(j);
	return m;
}

struct mime_content *api_alarms_json(struct caster_state *caster, struct request *req) {
	json_object *j = alarms_ring_json(caster);
	char *s = mystrdup(json_object_to_json_string(j));
	struct mime_content *m = mime_new(s, -1, "application/json", 1);
	json_object_put(j);
	return m;
}

struct mime_content *api_whoami_json(struct caster_state *caster, struct request *req) {
	const char *user = req->st->user;
	if (!user && req->hash)
		user = hash_table_get(req->hash, "user");

	json_object *j = json_object_new_object();
	json_object_object_add_ex(j, "user", user ? json_object_new_string(user) : json_object_new_null(), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "role", json_object_new_string(admin_role_name(req->st->admin_role)), JSON_C_CONSTANT_NEW);
	char *s = mystrdup(json_object_to_json_string(j));
	struct mime_content *m = mime_new(s, -1, "application/json", 1);
	json_object_put(j);
	return m;
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
