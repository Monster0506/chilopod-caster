#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include <json-c/json_object.h>
#include <json-c/json_tokener.h>

#include "config.h"
#include "sidecar.h"
#include "util.h"

/*
 * Add a "latency_ms" field to each mountpoint's stats object, computed from
 * its "last_updated" timestamp at read time (not when the sidecar wrote the
 * file), so it reflects how stale the data actually is right now.
 */
static void sidecar_add_latency(json_object *mountpoints) {
	if (!mountpoints)
		return;

	struct timeval now;
	gettimeofday(&now, NULL);

	json_object_object_foreach(mountpoints, key, stats) {
		(void)key;
		json_object *jlast;
		if (!json_object_object_get_ex(stats, "last_updated", &jlast))
			continue;

		struct timeval last;
		if (!timeval_from_iso_date(&last, json_object_get_string(jlast)))
			continue;

		long long ms = (long long)(now.tv_sec - last.tv_sec) * 1000
			+ (now.tv_usec - last.tv_usec) / 1000;
		if (ms < 0)
			ms = 0;
		json_object_object_add_ex(stats, "latency_ms", json_object_new_int64(ms), JSON_C_CONSTANT_NEW);
	}
}

json_object *sidecar_stats_json(const char *config_dir, struct config *config) {
	if (!config->sidecar_stats_filename)
		return NULL;

	FILE *fp = fopen_absolute(config_dir, config->sidecar_stats_filename, "r");
	if (fp == NULL)
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
	if (buf == NULL) {
		fclose(fp);
		return NULL;
	}
	size_t n = fread(buf, 1, size, fp);
	fclose(fp);
	buf[n] = '\0';

	json_object *doc = json_tokener_parse(buf);
	free(buf);

	if (doc) {
		json_object *mountpoints;
		if (json_object_object_get_ex(doc, "mountpoints", &mountpoints))
			sidecar_add_latency(mountpoints);
	}
	return doc;
}
