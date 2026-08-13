#include <stdio.h>
#include <stdlib.h>

#include <json-c/json_object.h>
#include <json-c/json_tokener.h>

#include "config.h"
#include "sidecar.h"
#include "util.h"

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
	return doc;
}
