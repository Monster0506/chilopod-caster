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

		/*
		 * For a client on a virtual (NEAR) mountpoint, the physical base
		 * it's actually receiving data from right now -- set once NEAR
		 * has resolved a base, updated on each subsequent switch.
		 */
		if (st->virtual_mountpoint)
			json_object_object_add_ex(new_obj, "assigned_base", json_object_new_string(st->virtual_mountpoint), JSON_C_CONSTANT_NEW);
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

/*
 * Rewrite a file, dropping lines whose delimited field at field_index equals
 * value exactly (field 0 is whatever precedes the first separator).
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
 * Rewrite the value of a top-level "key: value" scalar line in a YAML file
 * (matched at the start of a line, unindented), preserving every other line
 * including comments, and any trailing inline "# comment" on the matched
 * line itself. Appends a new "key: value" line at the end if the key isn't
 * present (e.g. an optional field using its compiled-in default).
 * Returns 1 if a line was replaced, 0 if appended, -1 on I/O error.
 */
static int set_yaml_scalar(const char *path, const char *key, const char *value) {
	FILE *in = fopen(path, "r");
	if (in == NULL)
		return -1;

	size_t tmp_len = strlen(path) + 5;
	char *tmp_path = (char *)strmalloc(tmp_len);
	snprintf(tmp_path, tmp_len, "%s.tmp", path);
	FILE *out = fopen(tmp_path, "w");
	if (out == NULL) {
		fclose(in);
		strfree(tmp_path);
		return -1;
	}

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	int found = 0;
	size_t klen = strlen(key);

	while ((linelen = getline(&line, &linecap, in)) >= 0) {
		if (!found && !strncmp(line, key, klen) && line[klen] == ':') {
			char *comment = strchr(line + klen + 1, '#');
			if (comment)
				fprintf(out, "%s:\t%s\t%s", key, value, comment);
			else
				fprintf(out, "%s:\t%s\n", key, value);
			found = 1;
		} else {
			fwrite(line, 1, linelen, out);
		}
	}
	free(line);
	if (!found)
		fprintf(out, "%s:\t%s\n", key, value);

	fclose(in);
	fclose(out);

	if (rename(tmp_path, path) < 0) {
		strfree(tmp_path);
		return -1;
	}
	strfree(tmp_path);
	return found;
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

/*
 * Drop any active source connections pushing to the given mountpoint.
 */
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

/*
 * Remove a mountpoint: drop its entries from source_auth_file and
 * sourcetable_file, reload, and disconnect any active source pushing to it.
 */
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

/*
 * Create or update a source_auth_file entry for a mountpoint.
 */
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

/*
 * Return the current caster.yaml scalar settings as a JSON object.
 */
struct mime_content *api_settings_get_json(struct caster_state *caster, struct request *req) {
	struct config *config = req->st->config;
	json_object *j = json_object_new_object();
	const char *ll = log_level_name(config->log_level);

	json_object_object_add_ex(j, "log_level", json_object_new_string(ll ? ll : "UNKNOWN"), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "hysteresis_m", json_object_new_double(config->hysteresis_m), JSON_C_CONSTANT_NEW);
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

	char *s = mystrdup(json_object_to_json_string(j));
	struct mime_content *m = mime_new(s, -1, "application/json", 1);
	json_object_put(j);
	return m;
}

/*
 * Create or update one or more caster.yaml scalar settings, then reload.
 * Accepts any subset of the fields returned by api_settings_get_json.
 * Each provided field is validated and written to caster->config_file via
 * set_yaml_scalar before caster_reload() picks it up.
 */
struct mime_content *api_settings_set_json(struct caster_state *caster, struct request *req) {
	static const char *STRING_FIELDS[] = {
		"admin_user", "trusted_http_ip_header", "host_auth_filename",
		"source_auth_filename", "blocklist_filename", "sourcetable_filename",
		"access_log", "log", "ui_dir", NULL
	};
	static const char *INT_FIELDS[] = {
		"idle_max_delay", "reconnect_delay", "min_raw_packet", "max_raw_packet",
		"sourcetable_priority", "on_demand_source_timeout", NULL
	};
	static const char *SIZE_FIELDS[] = {
		"backlog_socket", "backlog_evbuffer", "http_header_max_size",
		"http_content_length_max", NULL
	};

	int changed = 0;
	char *end;

	char *log_level = (char *)hash_table_get(req->hash, "log_level");
	if (log_level) {
		if (log_level_from_name(log_level) < 0)
			return api_error_json("invalid log_level");
		if (set_yaml_scalar(caster->config_file, "log_level", log_level) < 0)
			return api_error_json("cannot rewrite config file");
		changed = 1;
	}

	char *hysteresis_m = (char *)hash_table_get(req->hash, "hysteresis_m");
	if (hysteresis_m) {
		strtod(hysteresis_m, &end);
		if (end == hysteresis_m || *end)
			return api_error_json("hysteresis_m must be a number");
		if (set_yaml_scalar(caster->config_file, "hysteresis_m", hysteresis_m) < 0)
			return api_error_json("cannot rewrite config file");
		changed = 1;
	}

	for (int i = 0; STRING_FIELDS[i]; i++) {
		char *value = (char *)hash_table_get(req->hash, STRING_FIELDS[i]);
		if (!value)
			continue;
		if (!*value || !field_is_safe(value))
			return api_error_json("invalid or empty value for a setting");
		if (set_yaml_scalar(caster->config_file, STRING_FIELDS[i], value) < 0)
			return api_error_json("cannot rewrite config file");
		changed = 1;
	}

	for (int i = 0; INT_FIELDS[i]; i++) {
		char *value = (char *)hash_table_get(req->hash, INT_FIELDS[i]);
		if (!value)
			continue;
		strtol(value, &end, 10);
		if (end == value || *end)
			return api_error_json("a numeric setting is not a valid integer");
		if (set_yaml_scalar(caster->config_file, INT_FIELDS[i], value) < 0)
			return api_error_json("cannot rewrite config file");
		changed = 1;
	}

	for (int i = 0; SIZE_FIELDS[i]; i++) {
		char *value = (char *)hash_table_get(req->hash, SIZE_FIELDS[i]);
		if (!value)
			continue;
		/* strtoul() accepts a leading '-' and silently wraps it into a huge
		 * unsigned value instead of failing, so reject it explicitly. */
		if (value[0] == '-')
			return api_error_json("a size setting must be a positive integer");
		unsigned long v = strtoul(value, &end, 10);
		if (end == value || *end || v == 0)
			return api_error_json("a size setting must be a positive integer");
		if (set_yaml_scalar(caster->config_file, SIZE_FIELDS[i], value) < 0)
			return api_error_json("cannot rewrite config file");
		changed = 1;
	}

	if (!changed)
		return api_error_json("no settings provided");

	int r = caster_reload(caster);
	char result[40];
	snprintf(result, sizeof result, "{\"result\": %d}\n", r);
	char *rs = mystrdup(result);
	return mime_new(rs, -1, "application/json", 1);
}

/*
 * Rewrite the sourcetable line for mountpoint (matched on field 1), replacing
 * field target_field with new_value. Returns 1 if a line was updated, 0 if
 * no matching line was found, -1 on I/O error.
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
 * Look up what RTCM3 message types the caster has actually decoded for a
 * mountpoint and update its sourcetable format-details, nav-system, and
 * position fields to match -- the automated form of README's "go back and
 * fix the placeholder STR line" step, for sources where the real message
 * set can't be known in advance.
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
