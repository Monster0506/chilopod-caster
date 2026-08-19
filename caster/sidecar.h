#ifndef __SIDECAR_H__
#define __SIDECAR_H__

#include <json-c/json_object.h>

struct config;

/*
 * Read and parse the rtcm-go sidecar's stats file (cmd/sidecar in rtcm-go).
 * Returns NULL if unconfigured, missing, or unparseable; caller must json_object_put() it.
 */
json_object *sidecar_stats_json(const char *config_dir, struct config *config);

#endif
