#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <event2/event.h>
#include <json-c/json_object.h>

#include "alarms.h"
#include "auth.h"
#include "caster.h"
#include "config.h"
#include "hash.h"
#include "livesource.h"
#include "log.h"
#include "rtcm.h"
#include "sidecar.h"
#include "sourceline.h"
#include "sourcetable.h"
#include "util.h"

/*
 * Alarm detection and notification (issue #27).
 *
 * A periodic timer scans local, non-virtual mountpoints for four
 * conditions (station offline, station back online, low satellite count,
 * position drift) and, when a configured threshold is crossed, spawns the
 * "ruckus" helper binary as a one-shot subprocess to send an email. The
 * spawn is asynchronous (SIGCHLD + a stderr pipe watched by libevent) so a
 * slow or retrying send never blocks the caster's own event loop.
 */

#define ALARM_CHECK_INTERVAL_S	30

static const char *alarm_event_names[ALARM_EVENT_TYPE_COUNT] = {
	"station_offline", "station_online", "low_sv_count", "position_drift"
};

static void alarm_mountpoint_state_free(void *p) {
	free(p);
}

static double timeval_diff_minutes(struct timeval *now, struct timeval *since) {
	return (now->tv_sec - since->tv_sec) / 60.0;
}

static const struct { const char *json_name; const char *abbrev; } constellation_names[] = {
	{ "GPS", "GPS" }, { "GLONASS", "GLO" }, { "Galileo", "GAL" },
	{ "BeiDou", "BDS" }, { "QZSS", "QZS" }, { "SBAS", "SBAS" },
};

/* Build "8GPS+6GLO+6GAL+7BDS" from the sidecar's per-mountpoint "constellations"
 * object. Leaves out unchanged if that key isn't present. */
static void build_constellation_summary(json_object *sc, char *out, size_t cap) {
	out[0] = '\0';
	json_object *jconst;
	if (!json_object_object_get_ex(sc, "constellations", &jconst))
		return;
	for (unsigned i = 0; i < sizeof(constellation_names) / sizeof(constellation_names[0]); i++) {
		json_object *jcount;
		if (!json_object_object_get_ex(jconst, constellation_names[i].json_name, &jcount))
			continue;
		char part[24];
		snprintf(part, sizeof part, "%s%d%s", out[0] ? "+" : "", json_object_get_int(jcount), constellation_names[i].abbrev);
		strncat(out, part, cap - strlen(out) - 1);
	}
}

/* Append "; axis Xmm (limit Ymm)" to a position_drift reason string. */
static void append_drift_reason(char *reason, size_t cap, const char *axis, double drift_mm, int limit_mm) {
	char part[96];
	snprintf(part, sizeof part, "%s%s %.0fmm (limit %dmm)", reason[0] ? "; " : "", axis, drift_mm, limit_mm);
	strncat(reason, part, cap - strlen(reason) - 1);
}

/* True if this alarm type hasn't fired recently enough to be rate-limited */
static int alarm_rate_ok(struct alarm_mountpoint_state *state, enum alarm_event_type type,
		struct timeval *now, int min_interval_minutes) {
	struct timeval *last = &state->last_sent[type];
	if (last->tv_sec == 0)
		return 1;
	return timeval_diff_minutes(now, last) >= min_interval_minutes;
}

/* NULL alarm_types means "every alarm type" (the default, unfiltered). */
static int recipient_wants(struct config_alarms_recipient *r, enum alarm_event_type type) {
	if (!r->alarm_types)
		return 1;
	for (int i = 0; i < r->alarm_types_count; i++)
		if (!strcmp(r->alarm_types[i], alarm_event_names[type]))
			return 1;
	return 0;
}

static int mountpoint_wants(struct config_alarms *alarms, const char *mountpoint, enum alarm_event_type type) {
	for (int i = 0; i < alarms->mountpoints_count; i++) {
		if (strcmp(alarms->mountpoints[i].mountpoint, mountpoint))
			continue;
		for (int j = 0; j < alarms->mountpoints[i].alarm_types_count; j++)
			if (!strcmp(alarms->mountpoints[i].alarm_types[j], alarm_event_names[type]))
				return 1;
		return 0;
	}
	return 1;
}

static char *build_ruckus_payload(struct caster_state *caster, struct config_alarms *alarms,
		enum alarm_event_type type, const char *subject, const char *body) {
	json_object *j = json_object_new_object();
	json_object *jsmtp = json_object_new_object();

	json_object_object_add_ex(jsmtp, "host", json_object_new_string(alarms->smtp->host), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(jsmtp, "port", json_object_new_int(alarms->smtp->port), JSON_C_CONSTANT_NEW);

	const char *tls_str = alarms->smtp->tls == CONFIG_ALARMS_TLS_STARTTLS ? "starttls"
		: alarms->smtp->tls == CONFIG_ALARMS_TLS_SMTPS ? "smtps" : "none";
	json_object_object_add_ex(jsmtp, "tls", json_object_new_string(tls_str), JSON_C_CONSTANT_NEW);

	char from_buf[256];
	const char *from = NULL;
	struct auth_entry *auth_entries = NULL;

	if (alarms->smtp->auth_file) {
		auth_entries = auth_parse(caster, alarms->smtp->auth_file);
		struct auth_entry *e = auth_entries ? auth_lookup(auth_entries, alarms->smtp->host) : NULL;
		if (e) {
			json_object *jauth = json_object_new_object();
			json_object_object_add_ex(jauth, "username", json_object_new_string(e->user), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(jauth, "password", json_object_new_string(e->password), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(jsmtp, "auth", jauth, JSON_C_CONSTANT_NEW);
			snprintf(from_buf, sizeof from_buf, "Chilopod Alarms <%s>", e->user);
			from = from_buf;
		} else
			logfmt(&caster->flog, LOG_WARNING, "alarm: no entry for host %s in %s, sending unauthenticated", alarms->smtp->host, alarms->smtp->auth_file);
	}
	if (!from) {
		snprintf(from_buf, sizeof from_buf, "Chilopod Alarms <chilopod@%s>", caster->hostname);
		from = from_buf;
	}

	json_object_object_add_ex(j, "smtp", jsmtp, JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "from", json_object_new_string(from), JSON_C_CONSTANT_NEW);

	json_object *jto = json_object_new_array();
	for (int i = 0; i < alarms->recipients_count; i++) {
		if (!recipient_wants(&alarms->recipients[i], type))
			continue;
		json_object *r = json_object_new_object();
		json_object_object_add_ex(r, "name", json_object_new_string(alarms->recipients[i].name ? alarms->recipients[i].name : ""), JSON_C_CONSTANT_NEW);
		json_object_object_add_ex(r, "email", json_object_new_string(alarms->recipients[i].email), JSON_C_CONSTANT_NEW);
		json_object_array_add(jto, r);
	}
	json_object_object_add_ex(j, "to", jto, JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "subject", json_object_new_string(subject), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "body", json_object_new_string(body), JSON_C_CONSTANT_NEW);
	json_object_object_add_ex(j, "html", json_object_new_boolean(1), JSON_C_CONSTANT_NEW);

	char *s = mystrdup(json_object_to_json_string(j));
	json_object_put(j);
	if (auth_entries)
		auth_free(auth_entries);
	return s;
}

/* Read a whole file into a malloc'd, NUL-terminated buffer. NULL on any failure. */
static char *read_whole_file(const char *config_dir, const char *filename) {
	FILE *fp = fopen_absolute(config_dir, filename, "r");
	if (!fp)
		return NULL;
	if (fseek(fp, 0, SEEK_END) < 0) {
		fclose(fp);
		return NULL;
	}
	long size = ftell(fp);
	if (size <= 0 || fseek(fp, 0, SEEK_SET) < 0) {
		fclose(fp);
		return NULL;
	}
	char *buf = (char *)malloc(size + 1);
	if (!buf) {
		fclose(fp);
		return NULL;
	}
	size_t n = fread(buf, 1, size, fp);
	fclose(fp);
	buf[n] = '\0';
	return buf;
}

static char *str_replace_all(const char *haystack, const char *needle, const char *replacement) {
	size_t needle_len = strlen(needle);
	size_t replacement_len = strlen(replacement);
	size_t count = 0;

	for (const char *p = haystack; (p = strstr(p, needle)) != NULL; p += needle_len)
		count++;

	size_t out_len = strlen(haystack) + count * (replacement_len - needle_len);
	char *out = (char *)malloc(out_len + 1);
	if (!out)
		return NULL;

	char *dst = out;
	const char *src = haystack;
	const char *match;
	while ((match = strstr(src, needle)) != NULL) {
		size_t chunk = match - src;
		memcpy(dst, src, chunk);
		dst += chunk;
		memcpy(dst, replacement, replacement_len);
		dst += replacement_len;
		src = match + needle_len;
	}
	strcpy(dst, src);
	return out;
}

struct alarm_type_style {
	const char *label;
	const char *accent;
	const char *accent_bg;
};

static const struct alarm_type_style alarm_type_styles[ALARM_EVENT_TYPE_COUNT] = {
	[ALARM_STATION_OFFLINE] = { "Station Offline", "#ef4444", "#fef2f2" },
	[ALARM_STATION_ONLINE] = { "Back Online", "#22c55e", "#f0fdf4" },
	[ALARM_LOW_SV] = { "Low Satellite Count", "#d97706", "#fffbeb" },
	[ALARM_POSITION_DRIFT] = { "Position Drift", "#a855f7", "#faf5ff" },
};

static char *build_html_body(struct caster_state *caster, struct config_alarms *alarms,
		enum alarm_event_type type, const char *mountpoint, const char *summary) {
	char *tmpl = read_whole_file(caster->config_dir, alarms->email_template);
	if (!tmpl) {
		logfmt(&caster->flog, LOG_WARNING, "alarm: could not read email_template %s, sending plain summary", alarms->email_template);
		return NULL;
	}

	char time_buf[64];
	time_t now = time(NULL);
	struct tm tm_buf;
	localtime_r(&now, &tm_buf);
	strftime(time_buf, sizeof time_buf, "%b %-d, %Y %-I:%M %p %Z", &tm_buf);

	const struct alarm_type_style *style = &alarm_type_styles[type];

	char *steps[6];
	steps[0] = str_replace_all(tmpl, "{{ACCENT}}", style->accent);
	free(tmpl);
	steps[1] = str_replace_all(steps[0], "{{ACCENT_BG}}", style->accent_bg);
	free(steps[0]);
	steps[2] = str_replace_all(steps[1], "{{MOUNTPOINT}}", mountpoint);
	free(steps[1]);
	steps[3] = str_replace_all(steps[2], "{{TYPE_LABEL}}", style->label);
	free(steps[2]);
	steps[4] = str_replace_all(steps[3], "{{SUMMARY}}", summary);
	free(steps[3]);
	steps[5] = str_replace_all(steps[4], "{{TIME}}", time_buf);
	free(steps[4]);

	return steps[5];
}

static void alarm_send_stderr_cb(evutil_socket_t fd, short event, void *arg) {
	struct alarm_send *as = (struct alarm_send *)arg;
	char buf[512];
	ssize_t n = read(fd, buf, sizeof buf);
	if (n <= 0)
		return;
	int copy = (int)n;
	if (as->errlen + copy > (int)sizeof(as->errbuf) - 1)
		copy = (int)sizeof(as->errbuf) - 1 - as->errlen;
	if (copy > 0) {
		memcpy(as->errbuf + as->errlen, buf, copy);
		as->errlen += copy;
		as->errbuf[as->errlen] = '\0';
	}
}

/* Record one outcome into the ring buffer. Thread-safe. */
static void alarm_ring_add(struct caster_state *caster, const char *mountpoint, enum alarm_event_type type,
		const char *summary, int sent, int suppressed, int exitcode, const char *error) {
	struct alarms_state *as = caster->alarms;

	P_RWLOCK_WRLOCK(&as->ring_lock);
	struct alarm_ring_entry *e = &as->ring[as->ring_head];
	gettimeofday(&e->time, NULL);
	snprintf(e->mountpoint, sizeof e->mountpoint, "%s", mountpoint ? mountpoint : "");
	e->type = type;
	snprintf(e->summary, sizeof e->summary, "%s", summary ? summary : "");
	e->sent = sent;
	e->suppressed = suppressed;
	e->exitcode = exitcode;
	snprintf(e->error, sizeof e->error, "%s", error ? error : "");
	as->ring_head = (as->ring_head + 1) % ALARM_RING_SIZE;
	if (as->ring_count < ALARM_RING_SIZE)
		as->ring_count++;
	P_RWLOCK_UNLOCK(&as->ring_lock);
}

json_object *alarms_ring_json(struct caster_state *caster) {
	struct alarms_state *as = caster->alarms;
	json_object *jarr = json_object_new_array_ext(as ? as->ring_count : 0);
	if (!as)
		return jarr;

	P_RWLOCK_RDLOCK(&as->ring_lock);
	/* Most-recent-first: walk backward from the last-written slot */
	int idx = (as->ring_head - 1 + ALARM_RING_SIZE) % ALARM_RING_SIZE;
	for (int i = 0; i < as->ring_count; i++) {
		struct alarm_ring_entry *e = &as->ring[idx];
		json_object *j = json_object_new_object();
		json_object_object_add_ex(j, "mountpoint", json_object_new_string(e->mountpoint), JSON_C_CONSTANT_NEW);
		json_object_object_add_ex(j, "type", json_object_new_string(alarm_event_names[e->type]), JSON_C_CONSTANT_NEW);
		json_object_object_add_ex(j, "summary", json_object_new_string(e->summary), JSON_C_CONSTANT_NEW);
		json_object_object_add_ex(j, "sent", json_object_new_boolean(e->sent), JSON_C_CONSTANT_NEW);
		json_object_object_add_ex(j, "exit_code", json_object_new_int(e->exitcode), JSON_C_CONSTANT_NEW);
		if (!e->sent) {
			json_object_object_add_ex(j, "suppressed", json_object_new_boolean(e->suppressed), JSON_C_CONSTANT_NEW);
			json_object_object_add_ex(j, "error", json_object_new_string(e->error), JSON_C_CONSTANT_NEW);
		}
		timeval_to_json(&e->time, j, "time");
		json_object_array_add(jarr, j);
		idx = (idx - 1 + ALARM_RING_SIZE) % ALARM_RING_SIZE;
	}
	P_RWLOCK_UNLOCK(&as->ring_lock);
	return jarr;
}

static void alarms_sigchld_cb(evutil_socket_t fd, short event, void *arg) {
	struct caster_state *caster = (struct caster_state *)arg;
	int status;
	pid_t pid;

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		struct alarm_send *as, *tmp;
		TAILQ_FOREACH_SAFE(as, &caster->alarms->sends, next, tmp) {
			if (as->pid != pid)
				continue;

			/* Drain any final stderr output before logging the outcome */
			char buf[512];
			ssize_t n;
			while (as->errlen < (int)sizeof(as->errbuf) - 1
					&& (n = read(as->stderr_fd, buf, sizeof buf)) > 0) {
				int copy = (int)n;
				if (as->errlen + copy > (int)sizeof(as->errbuf) - 1)
					copy = (int)sizeof(as->errbuf) - 1 - as->errlen;
				memcpy(as->errbuf + as->errlen, buf, copy);
				as->errlen += copy;
			}

			int exitcode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
			int sent = (exitcode == 0);
			if (sent) {
				logfmt(&caster->flog, LOG_INFO, "alarm: sent %s for %s", alarm_event_names[as->type], as->mountpoint);
				struct alarm_mountpoint_state *state =
					(struct alarm_mountpoint_state *)hash_table_get(caster->alarms->mountpoints, as->mountpoint);
				if (state)
					gettimeofday(&state->last_sent[as->type], NULL);
			} else
				logfmt(&caster->flog, LOG_ERR, "alarm: ruckus failed (exit %d) for %s %s: %s",
					exitcode, alarm_event_names[as->type], as->mountpoint,
					as->errlen ? as->errbuf : "(no output)");

			alarm_ring_add(caster, as->mountpoint, as->type, as->summary, sent, 0, exitcode,
				as->errlen ? as->errbuf : "(no output)");

			event_free(as->stderr_event);
			close(as->stderr_fd);
			TAILQ_REMOVE(&caster->alarms->sends, as, next);
			free(as->mountpoint);
			free(as->summary);
			free(as);
			break;
		}
	}
}

static void spawn_ruckus(struct caster_state *caster, const char *ruckus_path,
		const char *mountpoint, enum alarm_event_type type, const char *payload, const char *summary) {
	int stdin_pipe[2], stderr_pipe[2];

	if (pipe(stdin_pipe) < 0) {
		logfmt(&caster->flog, LOG_ERR, "alarm: pipe() failed: %s", strerror(errno));
		alarm_ring_add(caster, mountpoint, type, summary, 0, 0, -1, strerror(errno));
		return;
	}
	if (pipe(stderr_pipe) < 0) {
		logfmt(&caster->flog, LOG_ERR, "alarm: pipe() failed: %s", strerror(errno));
		alarm_ring_add(caster, mountpoint, type, summary, 0, 0, -1, strerror(errno));
		close(stdin_pipe[0]); close(stdin_pipe[1]);
		return;
	}

	pid_t pid = fork();
	if (pid < 0) {
		logfmt(&caster->flog, LOG_ERR, "alarm: fork() failed: %s", strerror(errno));
		alarm_ring_add(caster, mountpoint, type, summary, 0, 0, -1, strerror(errno));
		close(stdin_pipe[0]); close(stdin_pipe[1]);
		close(stderr_pipe[0]); close(stderr_pipe[1]);
		return;
	}

	if (pid == 0) {
		/* Child: only async-signal-safe calls until execv() */
		dup2(stdin_pipe[0], STDIN_FILENO);
		dup2(stderr_pipe[1], STDERR_FILENO);
		close(stdin_pipe[0]); close(stdin_pipe[1]);
		close(stderr_pipe[0]); close(stderr_pipe[1]);
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, STDOUT_FILENO);
			close(devnull);
		}
		char *argv[] = { (char *)ruckus_path, NULL };
		execv(ruckus_path, argv);
		_exit(127);
	}

	/* Parent */
	close(stdin_pipe[0]);
	close(stderr_pipe[1]);

	size_t len = strlen(payload);
	size_t written = 0;
	while (written < len) {
		ssize_t n = write(stdin_pipe[1], payload + written, len - written);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			logfmt(&caster->flog, LOG_ERR, "alarm: write to ruckus stdin failed: %s", strerror(errno));
			break;
		}
		written += n;
	}
	close(stdin_pipe[1]);

	evutil_make_socket_nonblocking(stderr_pipe[0]);

	struct alarm_send *as = (struct alarm_send *)calloc(1, sizeof(*as));
	if (!as) {
		close(stderr_pipe[0]);
		return;
	}
	as->pid = pid;
	as->stderr_fd = stderr_pipe[0];
	as->mountpoint = mystrdup(mountpoint);
	as->type = type;
	as->summary = mystrdup(summary);
	as->stderr_event = event_new(caster->alarms->base, stderr_pipe[0], EV_READ|EV_PERSIST, alarm_send_stderr_cb, as);
	if (as->stderr_event)
		event_add(as->stderr_event, NULL);
	TAILQ_INSERT_TAIL(&caster->alarms->sends, as, next);

	logfmt(&caster->flog, LOG_INFO, "alarm: spawned ruckus pid %d for %s %s", pid, alarm_event_names[type], mountpoint);
}

/* Returns 1 if the outcome is already final and safe to rate-limit on, 0 if
 * a send is now in flight and the rate limit must wait for its confirmed
 * outcome (see alarms_sigchld_cb). */
static int fire_alarm(struct caster_state *caster, struct config_alarms *alarms,
		const char *mountpoint, enum alarm_event_type type, const char *summary, const char *body) {
	char subject[256];
	snprintf(subject, sizeof subject, "%s: %s - %s", alarms->subject, mountpoint, summary);

	int any_recipient = 0;
	for (int i = 0; i < alarms->recipients_count; i++)
		if (recipient_wants(&alarms->recipients[i], type)) { any_recipient = 1; break; }
	if (!any_recipient) {
		logfmt(&caster->flog, LOG_INFO, "alarm: no recipients subscribed to %s, skipping send for %s",
			alarm_event_names[type], mountpoint);
		alarm_ring_add(caster, mountpoint, type, summary, 0, 1, -1, "no recipients subscribed to this alarm type");
		return 1;
	}

	char *html_body = build_html_body(caster, alarms, type, mountpoint, body);
	const char *email_body = html_body ? html_body : body;

	char *payload = build_ruckus_payload(caster, alarms, type, subject, email_body);
	free(html_body);
	if (!payload) {
		logfmt(&caster->flog, LOG_ERR, "alarm: failed to build payload for %s %s", mountpoint, alarm_event_names[type]);
		alarm_ring_add(caster, mountpoint, type, summary, 0, 0, -1, "failed to build email payload");
		return 0;
	}
	spawn_ruckus(caster, alarms->ruckus_path, mountpoint, type, payload, body);
	strfree(payload);
	return 0;
}

static void alarms_check_one(struct caster_state *caster, struct config_alarms *alarms,
		const char *mountpoint, pos_t *declared_pos, struct timeval *now, json_object *sidecar_stats) {
	struct alarm_mountpoint_state *state =
		(struct alarm_mountpoint_state *)hash_table_get(caster->alarms->mountpoints, mountpoint);
	if (!state) {
		state = (struct alarm_mountpoint_state *)calloc(1, sizeof(*state));
		if (!state)
			return;
		state->was_live = -1;
		hash_table_add(caster->alarms->mountpoints, mountpoint, state);
	}

	int is_live = livesource_exists(caster, (char *)mountpoint, declared_pos);

	/* station_offline / station_online */
	if (is_live) {
		if (state->was_live == 0) {
			if (state->online_since.tv_sec == 0)
				state->online_since = *now;
			int online_enabled = alarms->station_online && mountpoint_wants(alarms, mountpoint, ALARM_STATION_ONLINE);
			double minutes_up = online_enabled ? timeval_diff_minutes(now, &state->online_since) : 0;
			if (!online_enabled || minutes_up >= alarms->station_online->after_minutes) {
				if (online_enabled && alarm_rate_ok(state, ALARM_STATION_ONLINE, now, alarms->min_interval_minutes)) {
					double minutes_outage = state->offline_since.tv_sec ? timeval_diff_minutes(now, &state->offline_since) : 0;
					char body[256];
					snprintf(body, sizeof body,
						"Station %s is back online after an outage of %.0f minutes, and has now been back online for %.0f minutes.",
						mountpoint, minutes_outage, minutes_up);
					if (fire_alarm(caster, alarms, mountpoint, ALARM_STATION_ONLINE, "back online", body))
						state->last_sent[ALARM_STATION_ONLINE] = *now;
				}
				state->was_live = 1;
			}
		}
		state->offline_since.tv_sec = 0;
	} else {
		state->online_since.tv_sec = 0;
		state->was_live = 0;
		if (state->offline_since.tv_sec == 0)
			state->offline_since = *now;
		if (alarms->station_offline && mountpoint_wants(alarms, mountpoint, ALARM_STATION_OFFLINE)) {
			double minutes_down = timeval_diff_minutes(now, &state->offline_since);
			if (minutes_down >= alarms->station_offline->after_minutes
					&& alarm_rate_ok(state, ALARM_STATION_OFFLINE, now, alarms->min_interval_minutes)) {
				char body[256];
				snprintf(body, sizeof body, "Station %s has been offline for %.0f minutes.", mountpoint, minutes_down);
				if (fire_alarm(caster, alarms, mountpoint, ALARM_STATION_OFFLINE, "offline", body))
					state->last_sent[ALARM_STATION_OFFLINE] = *now;
			}
		}
	}

	/* low_sv_count */
	if (alarms->low_sv_count && mountpoint_wants(alarms, mountpoint, ALARM_LOW_SV) && sidecar_stats) {
		json_object *sc, *jsv;
		if (json_object_object_get_ex(sidecar_stats, mountpoint, &sc)
				&& json_object_object_get_ex(sc, "satellite_count", &jsv)) {
			int sv = json_object_get_int(jsv);
			if (sv < alarms->low_sv_count->min_sats) {
				if (state->low_sv_since.tv_sec == 0)
					state->low_sv_since = *now;
				double minutes = timeval_diff_minutes(now, &state->low_sv_since);
				if (minutes >= alarms->low_sv_count->after_minutes
						&& alarm_rate_ok(state, ALARM_LOW_SV, now, alarms->min_interval_minutes)) {
					char constellations[96];
					build_constellation_summary(sc, constellations, sizeof constellations);

					char body[350];
					snprintf(body, sizeof body, "Station %s is tracking only %d satellites", mountpoint, sv);
					size_t len = strlen(body);
					if (constellations[0]) {
						snprintf(body + len, sizeof body - len, " (%s)", constellations);
						len = strlen(body);
					}
					snprintf(body + len, sizeof body - len, " (threshold %d) for %.0f minutes.", alarms->low_sv_count->min_sats, minutes);
					len = strlen(body);
					json_object *jlat;
					if (json_object_object_get_ex(sc, "latency_ms", &jlat))
						snprintf(body + len, sizeof body - len, " Sidecar latency %" PRId64 "ms.", (int64_t)json_object_get_int64(jlat));

					if (fire_alarm(caster, alarms, mountpoint, ALARM_LOW_SV, "low satellite count", body))
						state->last_sent[ALARM_LOW_SV] = *now;
				}
			} else
				state->low_sv_since.tv_sec = 0;
		}
	}

	/* position_drift */
	if (alarms->position_drift && mountpoint_wants(alarms, mountpoint, ALARM_POSITION_DRIFT)) {
		struct config_alarms_position_drift *pd = alarms->position_drift;
		int triggered = 0;
		char reason[256] = "";

		P_RWLOCK_RDLOCK(&caster->rtcm_lock);
		struct rtcm_info *rp = (struct rtcm_info *)hash_table_get(caster->rtcm_cache, mountpoint);
		if (rp) {
			if (pd->lat_mm > 0 && rp->has_lat_lon_drift && rp->avg_lat_drift_mm > pd->lat_mm) {
				triggered = 1;
				append_drift_reason(reason, sizeof reason, "latitude", rp->avg_lat_drift_mm, pd->lat_mm);
			}
			if (pd->lon_mm > 0 && rp->has_lat_lon_drift && rp->avg_lon_drift_mm > pd->lon_mm) {
				triggered = 1;
				append_drift_reason(reason, sizeof reason, "longitude", rp->avg_lon_drift_mm, pd->lon_mm);
			}
			if (pd->alt_mm > 0 && rp->has_baseline_alt && rp->avg_alt_drift_mm > pd->alt_mm) {
				triggered = 1;
				append_drift_reason(reason, sizeof reason, "altitude", rp->avg_alt_drift_mm, pd->alt_mm);
			}
		}
		P_RWLOCK_UNLOCK(&caster->rtcm_lock);

		if (triggered) {
			if (state->drift_since.tv_sec == 0)
				state->drift_since = *now;
			double minutes = timeval_diff_minutes(now, &state->drift_since);
			if (minutes >= pd->after_minutes
					&& alarm_rate_ok(state, ALARM_POSITION_DRIFT, now, alarms->min_interval_minutes)) {
				char body[350];
				snprintf(body, sizeof body, "Station %s position drift: %s. Ongoing for %.0f minutes.", mountpoint, reason, minutes);
				if (fire_alarm(caster, alarms, mountpoint, ALARM_POSITION_DRIFT, "position drift", body))
					state->last_sent[ALARM_POSITION_DRIFT] = *now;
			}
		} else
			state->drift_since.tv_sec = 0;
	}
}

struct alarm_candidate {
	char *mountpoint;
	pos_t pos;
};

static void alarms_tick_cb(evutil_socket_t fd, short event, void *arg) {
	struct caster_state *caster = (struct caster_state *)arg;
	struct config *config = caster_config_getref(caster);
	if (!config)
		return;
	if (!config->alarms) {
		config_decref(config);
		return;
	}

	struct timeval now;
	gettimeofday(&now, NULL);

	struct alarm_candidate *candidates = NULL;
	int ncandidates = 0;

	P_RWLOCK_RDLOCK(&caster->sourcetablestack.lock);
	struct sourcetable *s;
	TAILQ_FOREACH(s, &caster->sourcetablestack.list, next) {
		if (strcmp(s->caster, "LOCAL"))
			continue;
		P_RWLOCK_RDLOCK(&s->lock);
		int n = hash_len(s->key_val);
		candidates = (struct alarm_candidate *)malloc(sizeof(struct alarm_candidate) * (n > 0 ? n : 1));
		if (candidates) {
			struct hash_iterator hi;
			struct element *e;
			HASH_FOREACH(e, s->key_val, hi) {
				struct sourceline *sl = (struct sourceline *)e->value;
				if (sl->virtual)
					continue;
				candidates[ncandidates].mountpoint = mystrdup(sl->key);
				candidates[ncandidates].pos = sl->pos;
				ncandidates++;
			}
		}
		P_RWLOCK_UNLOCK(&s->lock);
		break;
	}
	P_RWLOCK_UNLOCK(&caster->sourcetablestack.lock);

	json_object *sidecar_doc = sidecar_stats_json(caster->config_dir, config);
	json_object *sidecar_stats = NULL;
	if (sidecar_doc)
		json_object_object_get_ex(sidecar_doc, "mountpoints", &sidecar_stats);

	for (int i = 0; i < ncandidates; i++) {
		alarms_check_one(caster, config->alarms, candidates[i].mountpoint, &candidates[i].pos, &now, sidecar_stats);
		strfree(candidates[i].mountpoint);
	}
	free(candidates);

	if (sidecar_doc)
		json_object_put(sidecar_doc);

	config_decref(config);
}

int alarms_init(struct caster_state *caster) {
	caster->alarms = (struct alarms_state *)calloc(1, sizeof(struct alarms_state));
	if (!caster->alarms)
		return -1;

	caster->alarms->mountpoints = hash_table_new(101, alarm_mountpoint_state_free);
	if (!caster->alarms->mountpoints) {
		free(caster->alarms);
		caster->alarms = NULL;
		return -1;
	}
	TAILQ_INIT(&caster->alarms->sends);
	P_RWLOCK_INIT(&caster->alarms->ring_lock, NULL);
	caster->alarms->base = caster->base[0];

	struct timeval interval = { ALARM_CHECK_INTERVAL_S, 0 };
	caster->alarms->tick_event = event_new(caster->alarms->base, -1, EV_PERSIST, alarms_tick_cb, caster);
	if (!caster->alarms->tick_event || event_add(caster->alarms->tick_event, &interval) < 0) {
		fprintf(stderr, "Could not create/add alarms tick event!\n");
		return -1;
	}

	caster->alarms->sigchld_event = evsignal_new(caster->alarms->base, SIGCHLD, alarms_sigchld_cb, caster);
	if (!caster->alarms->sigchld_event || event_add(caster->alarms->sigchld_event, NULL) < 0) {
		fprintf(stderr, "Could not create/add SIGCHLD event!\n");
		return -1;
	}

	return 0;
}

void alarms_free(struct caster_state *caster) {
	if (!caster->alarms)
		return;

	if (caster->alarms->tick_event) {
		event_del(caster->alarms->tick_event);
		event_free(caster->alarms->tick_event);
	}
	if (caster->alarms->sigchld_event) {
		event_del(caster->alarms->sigchld_event);
		event_free(caster->alarms->sigchld_event);
	}

	struct alarm_send *as, *tmp;
	TAILQ_FOREACH_SAFE(as, &caster->alarms->sends, next, tmp) {
		event_free(as->stderr_event);
		close(as->stderr_fd);
		TAILQ_REMOVE(&caster->alarms->sends, as, next);
		free(as->mountpoint);
		free(as->summary);
		free(as);
	}

	if (caster->alarms->mountpoints)
		hash_table_free(caster->alarms->mountpoints);

	P_RWLOCK_DESTROY(&caster->alarms->ring_lock);

	free(caster->alarms);
	caster->alarms = NULL;
}
