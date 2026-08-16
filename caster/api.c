#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <json-c/json_object.h>
#include <yaml.h>

#include "alarms.h"
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
		 * it's actually receiving data from right now -- set once NEAR
		 * has resolved a base, updated on each subsequent switch.
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

struct yaml_path_seg {
	const char *key;
	int index;
};

struct yaml_match_result {
	int found;
	size_t start, end;
	int style_ok;

	int insertable;
	size_t insert_at;
	size_t indent_start, indent_end;
};

static size_t yaml_line_start(const unsigned char *buf, size_t index) {
	while (index > 0 && buf[index - 1] != '\n')
		index--;
	return index;
}

static size_t yaml_indent_end(const unsigned char *buf, size_t line_start) {
	size_t i = line_start;
	while (buf[i] == ' ' || buf[i] == '\t')
		i++;
	return i;
}

static int yaml_consume_node(yaml_parser_t *parser, yaml_event_t *start_ev, const unsigned char *buf,
                              const struct yaml_path_seg *target, int target_len, int cur_len,
                              struct yaml_match_result *res) {
	switch (start_ev->type) {
	case YAML_SCALAR_EVENT:
		if (cur_len == target_len && !res->found) {
			res->found = 1;
			res->start = start_ev->start_mark.index;
			res->end = start_ev->end_mark.index;
			res->style_ok = (start_ev->data.scalar.style == YAML_PLAIN_SCALAR_STYLE);
		}
		yaml_event_delete(start_ev);
		return 0;

	case YAML_MAPPING_START_EVENT: {
		yaml_event_delete(start_ev);
		int is_target_parent = !res->found && cur_len == target_len - 1
			&& target_len > 0 && target[target_len - 1].key;
		size_t last_key_index = 0;
		int have_sibling = 0;

		for (;;) {
			yaml_event_t key_ev;
			if (!yaml_parser_parse(parser, &key_ev))
				return -1;
			if (key_ev.type == YAML_MAPPING_END_EVENT) {
				size_t map_end = buf ? yaml_line_start(buf, key_ev.start_mark.index) : key_ev.start_mark.index;
				yaml_event_delete(&key_ev);
				if (is_target_parent && !res->found && !res->insertable && have_sibling) {
					res->insertable = 1;
					res->insert_at = map_end;
					res->indent_start = yaml_line_start(buf, last_key_index);
					res->indent_end = yaml_indent_end(buf, res->indent_start);
				}
				return 0;
			}
			if (key_ev.type != YAML_SCALAR_EVENT) {
				yaml_event_delete(&key_ev);
				return -1;
			}
			int on_path = !res->found && cur_len < target_len && target[cur_len].key
				&& !strcmp((const char *)key_ev.data.scalar.value, target[cur_len].key);
			last_key_index = key_ev.start_mark.index;
			have_sibling = 1;
			yaml_event_delete(&key_ev);

			yaml_event_t val_ev;
			if (!yaml_parser_parse(parser, &val_ev))
				return -1;
			if (yaml_consume_node(parser, &val_ev, buf, target, target_len,
			                      on_path ? cur_len + 1 : target_len + 1, res) < 0)
				return -1;
			if (res->found)
				return 0;
		}
	}

	case YAML_SEQUENCE_START_EVENT:
		yaml_event_delete(start_ev);
		for (int idx = 0; ; idx++) {
			yaml_event_t item_ev;
			if (!yaml_parser_parse(parser, &item_ev))
				return -1;
			if (item_ev.type == YAML_SEQUENCE_END_EVENT) {
				yaml_event_delete(&item_ev);
				return 0;
			}
			int on_path = !res->found && cur_len < target_len
				&& target[cur_len].key == NULL && target[cur_len].index == idx;
			if (yaml_consume_node(parser, &item_ev, buf, target, target_len,
			                      on_path ? cur_len + 1 : target_len + 1, res) < 0)
				return -1;
			if (res->found)
				return 0;
		}

	default:
		/* Anchors/aliases/tags aren't used in this config; treat as a dead end. */
		yaml_event_delete(start_ev);
		return 0;
	}
}

static int set_yaml_nested_scalar(const char *path, const struct yaml_path_seg *target, int target_len, const char *value) {
	FILE *fp = fopen(path, "rb");
	if (fp == NULL)
		return -1;
	if (fseek(fp, 0, SEEK_END) < 0) { fclose(fp); return -1; }
	long size = ftell(fp);
	if (size <= 0 || fseek(fp, 0, SEEK_SET) < 0) { fclose(fp); return -1; }

	unsigned char *buf = (unsigned char *)malloc(size);
	if (buf == NULL) { fclose(fp); return -1; }
	size_t n = fread(buf, 1, size, fp);
	fclose(fp);
	if (n != (size_t)size) { free(buf); return -1; }

	yaml_parser_t parser;
	if (!yaml_parser_initialize(&parser)) { free(buf); return -1; }
	yaml_parser_set_input_string(&parser, buf, size);

	struct yaml_match_result res = {0};
	int err = 0;

	yaml_event_t ev;
	int seen_doc_start = 0;
	for (;;) {
		if (!yaml_parser_parse(&parser, &ev)) { err = 1; break; }
		yaml_event_type_t t = ev.type;
		yaml_event_delete(&ev);
		if (t == YAML_DOCUMENT_START_EVENT) { seen_doc_start = 1; break; }
		if (t == YAML_STREAM_END_EVENT) break;
	}
	if (!err && seen_doc_start) {
		yaml_event_t root_ev;
		if (!yaml_parser_parse(&parser, &root_ev))
			err = 1;
		else if (yaml_consume_node(&parser, &root_ev, buf, target, target_len, 0, &res) < 0)
			err = 1;
	}
	yaml_parser_delete(&parser);

	if (err) { free(buf); return -1; }
	if (!res.found && !res.insertable) { free(buf); return -1; }
	if (res.found && !res.style_ok) { free(buf); return -2; }

	size_t tmp_len = strlen(path) + 5;
	char *tmp_path = (char *)strmalloc(tmp_len);
	snprintf(tmp_path, tmp_len, "%s.tmp", path);
	FILE *out = fopen(tmp_path, "w");
	if (out == NULL) { strfree(tmp_path); free(buf); return -1; }

	if (res.found) {
		fwrite(buf, 1, res.start, out);
		fwrite(value, 1, strlen(value), out);
		fwrite(buf + res.end, 1, (size_t)size - res.end, out);
	} else {
		fwrite(buf, 1, res.insert_at, out);
		fwrite(buf + res.indent_start, 1, res.indent_end - res.indent_start, out);
		fprintf(out, "%s: %s\n", target[target_len - 1].key, value);
		fwrite(buf + res.insert_at, 1, (size_t)size - res.insert_at, out);
	}
	fclose(out);
	free(buf);

	if (rename(tmp_path, path) < 0) { strfree(tmp_path); return -1; }
	strfree(tmp_path);
	return 0;
}

static int yaml_skip_node(yaml_parser_t *parser, yaml_event_t *ev) {
	struct yaml_match_result dummy = {0};
	return yaml_consume_node(parser, ev, NULL, NULL, -1, 0, &dummy);
}

static int yaml_skip_sequence_capture_end(yaml_parser_t *parser, yaml_event_t *start_ev, size_t *out_end) {
	yaml_event_delete(start_ev);
	for (;;) {
		yaml_event_t item_ev;
		if (!yaml_parser_parse(parser, &item_ev))
			return -1;
		if (item_ev.type == YAML_SEQUENCE_END_EVENT) {
			*out_end = item_ev.end_mark.index;
			yaml_event_delete(&item_ev);
			return 0;
		}
		if (yaml_skip_node(parser, &item_ev) < 0)
			return -1;
	}
}

struct yaml_recipients_info {
	int item_count;
	size_t item_start[32];		/* start of each item's "- " line */
	size_t seq_end;			/* byte offset right after the last item */
	size_t dash_prefix_start, dash_prefix_end;	/* e.g. "    - ", from item 0 */
	size_t cont_indent_start, cont_indent_end;	/* e.g. "      ", from item 0's 2nd key */
	int have_cont_indent;

	int alarm_types_found;		/* want_alarm_types_idx's "alarm_types" key exists */
	size_t alarm_types_key_line;	/* start of that key's own line */
	size_t alarm_types_start, alarm_types_end;	/* its value's byte range, e.g. "[...]" */
	int alarm_types_insertable;	/* key absent, but the item has other keys to anchor on */
};

static int yaml_locate_recipients(const unsigned char *buf, size_t size, int want_alarm_types_idx, struct yaml_recipients_info *info) {
	memset(info, 0, sizeof *info);

	yaml_parser_t parser;
	if (!yaml_parser_initialize(&parser))
		return -1;
	yaml_parser_set_input_string(&parser, buf, size);

	int err = 0, found_alarms = 0, found_recipients = 0;
	yaml_event_t ev;
	int seen_doc_start = 0;
	for (;;) {
		if (!yaml_parser_parse(&parser, &ev)) { err = 1; break; }
		yaml_event_type_t t = ev.type;
		yaml_event_delete(&ev);
		if (t == YAML_DOCUMENT_START_EVENT) { seen_doc_start = 1; break; }
		if (t == YAML_STREAM_END_EVENT) break;
	}

	if (!err && seen_doc_start) {
		yaml_event_t root_ev;
		if (!yaml_parser_parse(&parser, &root_ev)) {
			err = 1;
		} else if (root_ev.type != YAML_MAPPING_START_EVENT) {
			if (yaml_skip_node(&parser, &root_ev) < 0) err = 1;
		} else {
			yaml_event_delete(&root_ev);
			while (!err) {
				yaml_event_t key_ev;
				if (!yaml_parser_parse(&parser, &key_ev)) { err = 1; break; }
				if (key_ev.type == YAML_MAPPING_END_EVENT) { yaml_event_delete(&key_ev); break; }
				if (key_ev.type != YAML_SCALAR_EVENT) { yaml_event_delete(&key_ev); err = 1; break; }
				int is_alarms = !found_alarms && !strcmp((const char *)key_ev.data.scalar.value, "alarms");
				yaml_event_delete(&key_ev);

				yaml_event_t val_ev;
				if (!yaml_parser_parse(&parser, &val_ev)) { err = 1; break; }
				if (!is_alarms || val_ev.type != YAML_MAPPING_START_EVENT) {
					if (yaml_skip_node(&parser, &val_ev) < 0) err = 1;
					continue;
				}
				found_alarms = 1;
				yaml_event_delete(&val_ev);

				while (!err) {
					yaml_event_t akey_ev;
					if (!yaml_parser_parse(&parser, &akey_ev)) { err = 1; break; }
					if (akey_ev.type == YAML_MAPPING_END_EVENT) { yaml_event_delete(&akey_ev); break; }
					if (akey_ev.type != YAML_SCALAR_EVENT) { yaml_event_delete(&akey_ev); err = 1; break; }
					int is_recipients = !found_recipients && !strcmp((const char *)akey_ev.data.scalar.value, "recipients");
					yaml_event_delete(&akey_ev);

					yaml_event_t aval_ev;
					if (!yaml_parser_parse(&parser, &aval_ev)) { err = 1; break; }
					if (!is_recipients || aval_ev.type != YAML_SEQUENCE_START_EVENT) {
						if (yaml_skip_node(&parser, &aval_ev) < 0) err = 1;
						continue;
					}
					found_recipients = 1;
					yaml_event_delete(&aval_ev);

					while (!err) {
						yaml_event_t item_ev;
						if (!yaml_parser_parse(&parser, &item_ev)) { err = 1; break; }
						if (item_ev.type == YAML_SEQUENCE_END_EVENT) {
							info->seq_end = yaml_line_start(buf, item_ev.start_mark.index);
							yaml_event_delete(&item_ev);
							break;
						}
						if (item_ev.type != YAML_MAPPING_START_EVENT) {
							if (yaml_skip_node(&parser, &item_ev) < 0) err = 1;
							continue;
						}
						size_t item_line = yaml_line_start(buf, item_ev.start_mark.index);
						if (info->item_count < 32)
							info->item_start[info->item_count] = item_line;
						yaml_event_delete(&item_ev);

						int first_key = 1;
						int is_target_item = (info->item_count == want_alarm_types_idx);
						while (!err) {
							yaml_event_t k2;
							if (!yaml_parser_parse(&parser, &k2)) { err = 1; break; }
							if (k2.type == YAML_MAPPING_END_EVENT) { yaml_event_delete(&k2); break; }
							if (k2.type != YAML_SCALAR_EVENT) { yaml_event_delete(&k2); err = 1; break; }
							int is_wanted_alarm_types = is_target_item
								&& !strcmp((const char *)k2.data.scalar.value, "alarm_types");
							if (is_target_item) {
								info->alarm_types_insertable = 1;
								if (is_wanted_alarm_types)
									info->alarm_types_key_line = yaml_line_start(buf, k2.start_mark.index);
							}
							if (info->item_count == 0 && first_key) {
								info->dash_prefix_start = item_line;
								info->dash_prefix_end = k2.start_mark.index;
							} else if (info->item_count == 0 && !info->have_cont_indent) {
								size_t l = yaml_line_start(buf, k2.start_mark.index);
								info->cont_indent_start = l;
								info->cont_indent_end = yaml_indent_end(buf, l);
								info->have_cont_indent = 1;
							}
							first_key = 0;
							yaml_event_delete(&k2);

							yaml_event_t v2;
							if (!yaml_parser_parse(&parser, &v2)) { err = 1; break; }
							if (is_wanted_alarm_types && v2.type == YAML_SEQUENCE_START_EVENT) {
								info->alarm_types_found = 1;
								info->alarm_types_start = v2.start_mark.index;
								if (yaml_skip_sequence_capture_end(&parser, &v2, &info->alarm_types_end) < 0) { err = 1; break; }
							} else if (yaml_skip_node(&parser, &v2) < 0) {
								err = 1; break;
							}
						}
						if (!err)
							info->item_count++;
					}
				}
			}
		}
	}

	yaml_parser_delete(&parser);
	if (err || !found_recipients)
		return -1;
	return 0;
}

static int add_yaml_recipient(const char *path, const char *name, const char *email) {
	FILE *fp = fopen(path, "rb");
	if (fp == NULL)
		return -1;
	if (fseek(fp, 0, SEEK_END) < 0) { fclose(fp); return -1; }
	long size = ftell(fp);
	if (size <= 0 || fseek(fp, 0, SEEK_SET) < 0) { fclose(fp); return -1; }
	unsigned char *buf = (unsigned char *)malloc(size);
	if (buf == NULL) { fclose(fp); return -1; }
	size_t n = fread(buf, 1, size, fp);
	fclose(fp);
	if (n != (size_t)size) { free(buf); return -1; }

	struct yaml_recipients_info info;
	if (yaml_locate_recipients(buf, size, -1, &info) < 0 || info.item_count == 0) {
		free(buf);
		return -1;
	}

	size_t tmp_len = strlen(path) + 5;
	char *tmp_path = (char *)strmalloc(tmp_len);
	snprintf(tmp_path, tmp_len, "%s.tmp", path);
	FILE *out = fopen(tmp_path, "w");
	if (out == NULL) { strfree(tmp_path); free(buf); return -1; }

	fwrite(buf, 1, info.seq_end, out);
	fwrite(buf + info.dash_prefix_start, 1, info.dash_prefix_end - info.dash_prefix_start, out);
	if (name && *name) {
		fprintf(out, "name: %s\n", name);
		if (info.have_cont_indent)
			fwrite(buf + info.cont_indent_start, 1, info.cont_indent_end - info.cont_indent_start, out);
		else
			for (size_t i = 0; i < info.dash_prefix_end - info.dash_prefix_start; i++)
				fputc(' ', out);
		fprintf(out, "email: %s\n", email);
	} else {
		fprintf(out, "email: %s\n", email);
	}
	fwrite(buf + info.seq_end, 1, (size_t)size - info.seq_end, out);
	fclose(out);
	free(buf);

	if (rename(tmp_path, path) < 0) { strfree(tmp_path); return -1; }
	strfree(tmp_path);
	return 0;
}

static int remove_yaml_recipient(const char *path, int index) {
	FILE *fp = fopen(path, "rb");
	if (fp == NULL)
		return -1;
	if (fseek(fp, 0, SEEK_END) < 0) { fclose(fp); return -1; }
	long size = ftell(fp);
	if (size <= 0 || fseek(fp, 0, SEEK_SET) < 0) { fclose(fp); return -1; }
	unsigned char *buf = (unsigned char *)malloc(size);
	if (buf == NULL) { fclose(fp); return -1; }
	size_t n = fread(buf, 1, size, fp);
	fclose(fp);
	if (n != (size_t)size) { free(buf); return -1; }

	struct yaml_recipients_info info;
	if (yaml_locate_recipients(buf, size, -1, &info) < 0) { free(buf); return -1; }
	if (index < 0 || index >= info.item_count) { free(buf); return -1; }
	if (info.item_count <= 1) { free(buf); return -2; }

	size_t item_start = info.item_start[index];
	size_t item_end = (index + 1 < info.item_count) ? info.item_start[index + 1] : info.seq_end;

	size_t tmp_len = strlen(path) + 5;
	char *tmp_path = (char *)strmalloc(tmp_len);
	snprintf(tmp_path, tmp_len, "%s.tmp", path);
	FILE *out = fopen(tmp_path, "w");
	if (out == NULL) { strfree(tmp_path); free(buf); return -1; }

	fwrite(buf, 1, item_start, out);
	fwrite(buf + item_end, 1, (size_t)size - item_end, out);
	fclose(out);
	free(buf);

	if (rename(tmp_path, path) < 0) { strfree(tmp_path); return -1; }
	strfree(tmp_path);
	return 0;
}

static int set_yaml_recipient_alarm_types(const char *path, int index, const char **types, int count) {
	FILE *fp = fopen(path, "rb");
	if (fp == NULL)
		return -1;
	if (fseek(fp, 0, SEEK_END) < 0) { fclose(fp); return -1; }
	long size = ftell(fp);
	if (size <= 0 || fseek(fp, 0, SEEK_SET) < 0) { fclose(fp); return -1; }
	unsigned char *buf = (unsigned char *)malloc(size);
	if (buf == NULL) { fclose(fp); return -1; }
	size_t n = fread(buf, 1, size, fp);
	fclose(fp);
	if (n != (size_t)size) { free(buf); return -1; }

	struct yaml_recipients_info info;
	if (yaml_locate_recipients(buf, size, index, &info) < 0) { free(buf); return -1; }
	if (index < 0 || index >= info.item_count) { free(buf); return -1; }

	size_t value_cap = 4;
	for (int i = 0; i < count; i++)
		value_cap += strlen(types[i]) + 2;
	char *value = (char *)malloc(value_cap);
	if (!value) { free(buf); return -1; }
	char *p = value;
	*p++ = '[';
	for (int i = 0; i < count; i++) {
		if (i) { *p++ = ','; *p++ = ' '; }
		size_t l = strlen(types[i]);
		memcpy(p, types[i], l);
		p += l;
	}
	*p++ = ']';
	*p = '\0';

	size_t tmp_len = strlen(path) + 5;
	char *tmp_path = (char *)strmalloc(tmp_len);
	snprintf(tmp_path, tmp_len, "%s.tmp", path);
	FILE *out = fopen(tmp_path, "w");
	if (out == NULL) { strfree(tmp_path); free(value); free(buf); return -1; }

	int ok = 1;
	if (count == 0) {
		if (info.alarm_types_found) {
			size_t line_after = info.alarm_types_end;
			while (line_after < (size_t)size && buf[line_after] != '\n')
				line_after++;
			if (line_after < (size_t)size)
				line_after++;
			fwrite(buf, 1, info.alarm_types_key_line, out);
			fwrite(buf + line_after, 1, (size_t)size - line_after, out);
		} else {
			fwrite(buf, 1, size, out);
		}
	} else if (info.alarm_types_found) {
		fwrite(buf, 1, info.alarm_types_start, out);
		fwrite(value, 1, strlen(value), out);
		fwrite(buf + info.alarm_types_end, 1, (size_t)size - info.alarm_types_end, out);
	} else if (info.alarm_types_insertable) {
		size_t insert_at = (index + 1 < info.item_count) ? info.item_start[index + 1] : info.seq_end;
		fwrite(buf, 1, insert_at, out);
		if (info.have_cont_indent)
			fwrite(buf + info.cont_indent_start, 1, info.cont_indent_end - info.cont_indent_start, out);
		fprintf(out, "alarm_types: %s\n", value);
		fwrite(buf + insert_at, 1, (size_t)size - insert_at, out);
	} else {
		ok = 0;
	}
	fclose(out);
	free(value);
	free(buf);

	if (!ok) {
		remove(tmp_path);
		strfree(tmp_path);
		return -1;
	}
	if (rename(tmp_path, path) < 0) { strfree(tmp_path); return -1; }
	strfree(tmp_path);
	return 0;
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


struct mime_content *api_settings_set_json(struct caster_state *caster, struct request *req) {
	static const char *STRING_FIELDS[] = {
		"admin_user", "trusted_http_ip_header", "host_auth_filename",
		"source_auth_filename", "blocklist_filename", "sourcetable_filename",
		"sidecar_stats_filename", "access_log", "log", "ui_dir", NULL
	};
	static const char *INT_FIELDS[] = {
		"idle_max_delay", "reconnect_delay", "min_raw_packet", "max_raw_packet",
		"sourcetable_priority", "on_demand_source_timeout", "nearest_base_count_target",
		"min_nearest_recompute_interval", "max_nearest_recompute_interval",
		"source_read_timeout", "ntripcli_default_read_timeout", "ntripcli_default_write_timeout",
		"ntripsrv_default_read_timeout", "ntripsrv_default_write_timeout", NULL
	};
	static const char *SIZE_FIELDS[] = {
		"backlog_socket", "backlog_evbuffer", "http_header_max_size",
		"http_content_length_max", NULL
	};
	static const char *FLOAT_FIELDS[] = {
		"hysteresis_m", "max_nearest_lookup_distance_m", "min_nearest_recompute_pos_delta", NULL
	};

#define SEG_KEY(k) { (k), -1 }
	struct nested_field_def {
		const char *post_key;
		struct yaml_path_seg path[4];
		int path_len;
		int is_int;	
	};
	static const struct nested_field_def NESTED_FIELDS[] = {
		{ "alarms.subject", { SEG_KEY("alarms"), SEG_KEY("subject") }, 2, 0 },
		{ "alarms.min_interval_minutes", { SEG_KEY("alarms"), SEG_KEY("min_interval_minutes") }, 2, 1 },
		{ "alarms.ruckus_path", { SEG_KEY("alarms"), SEG_KEY("ruckus_path") }, 2, 0 },
		{ "alarms.email_template", { SEG_KEY("alarms"), SEG_KEY("email_template") }, 2, 0 },
		{ "alarms.smtp.host", { SEG_KEY("alarms"), SEG_KEY("smtp"), SEG_KEY("host") }, 3, 0 },
		{ "alarms.smtp.port", { SEG_KEY("alarms"), SEG_KEY("smtp"), SEG_KEY("port") }, 3, 1 },
		{ "alarms.smtp.auth_file", { SEG_KEY("alarms"), SEG_KEY("smtp"), SEG_KEY("auth_file") }, 3, 0 },
		{ "alarms.station_offline.after_minutes", { SEG_KEY("alarms"), SEG_KEY("station_offline"), SEG_KEY("after_minutes") }, 3, 1 },
		{ "alarms.station_online.after_minutes", { SEG_KEY("alarms"), SEG_KEY("station_online"), SEG_KEY("after_minutes") }, 3, 1 },
		{ "alarms.low_sv_count.min_sats", { SEG_KEY("alarms"), SEG_KEY("low_sv_count"), SEG_KEY("min_sats") }, 3, 1 },
		{ "alarms.low_sv_count.after_minutes", { SEG_KEY("alarms"), SEG_KEY("low_sv_count"), SEG_KEY("after_minutes") }, 3, 1 },
		{ "alarms.position_drift.lat_mm", { SEG_KEY("alarms"), SEG_KEY("position_drift"), SEG_KEY("lat_mm") }, 3, 1 },
		{ "alarms.position_drift.lon_mm", { SEG_KEY("alarms"), SEG_KEY("position_drift"), SEG_KEY("lon_mm") }, 3, 1 },
		{ "alarms.position_drift.alt_mm", { SEG_KEY("alarms"), SEG_KEY("position_drift"), SEG_KEY("alt_mm") }, 3, 1 },
		{ "alarms.position_drift.after_minutes", { SEG_KEY("alarms"), SEG_KEY("position_drift"), SEG_KEY("after_minutes") }, 3, 1 },
		{ NULL, {{0}}, 0, 0 }
	};
#undef SEG_KEY

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

	char *recipients_remove = (char *)hash_table_get(req->hash, "alarms.recipients.remove");
	if (recipients_remove) {
		char *rend;
		long idx = strtol(recipients_remove, &rend, 10);
		if (rend == recipients_remove || *rend || idx < 0)
			return api_error_json("alarms.recipients.remove must be a non-negative integer");
		int r = remove_yaml_recipient(caster->config_file, (int)idx);
		if (r == -2)
			return api_error_json("at least one recipient is required");
		if (r < 0)
			return api_error_json("that recipient index isn't present in the config file");
		changed = 1;
	}

	char *recipients_add_email = (char *)hash_table_get(req->hash, "alarms.recipients.add.email");
	if (recipients_add_email) {
		char *recipients_add_name = (char *)hash_table_get(req->hash, "alarms.recipients.add.name");
		if (!*recipients_add_email || !field_is_safe(recipients_add_email))
			return api_error_json("invalid or empty value for a setting");
		if (recipients_add_name && !field_is_safe(recipients_add_name))
			return api_error_json("invalid characters in a setting");
		int r = add_yaml_recipient(caster->config_file, recipients_add_name, recipients_add_email);
		if (r < 0)
			return api_error_json("cannot add a recipient (alarms.recipients needs at least one existing entry in caster.yaml to copy formatting from)");
		changed = 1;
	}

	for (int i = 0; FLOAT_FIELDS[i]; i++) {
		char *value = (char *)hash_table_get(req->hash, FLOAT_FIELDS[i]);
		if (!value)
			continue;
		strtod(value, &end);
		if (end == value || *end)
			return api_error_json("a numeric setting is not a valid number");
		if (set_yaml_scalar(caster->config_file, FLOAT_FIELDS[i], value) < 0)
			return api_error_json("cannot rewrite config file");
		changed = 1;
	}

	for (int i = 0; NESTED_FIELDS[i].post_key; i++) {
		const struct nested_field_def *nf = &NESTED_FIELDS[i];
		char *value = (char *)hash_table_get(req->hash, nf->post_key);
		if (!value)
			continue;
		if (nf->is_int) {
			strtol(value, &end, 10);
			if (end == value || *end)
				return api_error_json("a numeric setting is not a valid integer");
		} else if (!*value || !field_is_safe(value)) {
			return api_error_json("invalid or empty value for a setting");
		}
		int r = set_yaml_nested_scalar(caster->config_file, nf->path, nf->path_len, value);
		if (r == -2)
			return api_error_json("current value is not a plain scalar in the config file; edit it directly");
		if (r < 0)
			return api_error_json("setting not present in the config file (its parent block may be disabled)");
		changed = 1;
	}

	char *smtp_tls = (char *)hash_table_get(req->hash, "alarms.smtp.tls");
	if (smtp_tls) {
		if (strcasecmp(smtp_tls, "none") && strcasecmp(smtp_tls, "starttls") && strcasecmp(smtp_tls, "smtps"))
			return api_error_json("alarms.smtp.tls must be none, starttls or smtps");
		static const struct yaml_path_seg TLS_PATH[] = { {"alarms", -1}, {"smtp", -1}, {"tls", -1} };
		int r = set_yaml_nested_scalar(caster->config_file, TLS_PATH, 3, smtp_tls);
		if (r == -2)
			return api_error_json("alarms.smtp.tls is not a plain scalar in the config file");
		if (r < 0)
			return api_error_json("alarms.smtp.tls not present in the config file");
		changed = 1;
	}

	for (int idx = 0; idx < 20; idx++) {
		char key[64];
		snprintf(key, sizeof key, "alarms.recipients[%d].name", idx);
		char *value = (char *)hash_table_get(req->hash, key);
		if (value) {
			if (!field_is_safe(value))
				return api_error_json("invalid characters in a setting");
			struct yaml_path_seg path[] = { {"alarms", -1}, {"recipients", -1}, {NULL, idx}, {"name", -1} };
			int r = set_yaml_nested_scalar(caster->config_file, path, 4, value);
			if (r == -2)
				return api_error_json("a recipient name is not a plain scalar in the config file");
			if (r < 0)
				return api_error_json("that recipient index isn't present in the config file");
			changed = 1;
		}

		snprintf(key, sizeof key, "alarms.recipients[%d].email", idx);
		value = (char *)hash_table_get(req->hash, key);
		if (value) {
			if (!*value || !field_is_safe(value))
				return api_error_json("invalid or empty value for a setting");
			struct yaml_path_seg path[] = { {"alarms", -1}, {"recipients", -1}, {NULL, idx}, {"email", -1} };
			int r = set_yaml_nested_scalar(caster->config_file, path, 4, value);
			if (r == -2)
				return api_error_json("a recipient email is not a plain scalar in the config file");
			if (r < 0)
				return api_error_json("that recipient index isn't present in the config file");
			changed = 1;
		}

		snprintf(key, sizeof key, "alarms.recipients[%d].alarm_types", idx);
		value = (char *)hash_table_get(req->hash, key);
		if (value) {
			const char *type_ptrs[ALARM_EVENT_TYPE_COUNT];
			int type_count = 0;
			char *copy = mystrdup(value);
			int bad = 0;
			if (*copy) {
				char *tok = strtok(copy, ",");
				while (tok && type_count < ALARM_EVENT_TYPE_COUNT) {
					while (*tok == ' ')
						tok++;
					if (!is_valid_alarm_type(tok)) { bad = 1; break; }
					type_ptrs[type_count++] = tok;
					tok = strtok(NULL, ",");
				}
			}
			if (bad) {
				strfree(copy);
				return api_error_json("unknown alarm type in alarms.recipients[N].alarm_types");
			}
			int r = set_yaml_recipient_alarm_types(caster->config_file, idx, type_ptrs, type_count);
			strfree(copy);
			if (r < 0)
				return api_error_json("that recipient index isn't present in the config file");
			changed = 1;
		}
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
