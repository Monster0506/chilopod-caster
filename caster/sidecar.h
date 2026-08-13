#ifndef __SIDECAR_H__
#define __SIDECAR_H__

#include <json-c/json_object.h>

struct config;

/*
 * Read and parse the rtcm-go sidecar's stats file (cmd/sidecar in the
 * rtcm-go repo), returning the parsed document as-is: a "mountpoints"
 * object keyed by mountpoint, and a "type_names" object mapping RTCM
 * message type numbers (as strings) to human-readable names.
 *
 * Returns NULL if no sidecar_stats_file is configured, the file doesn't
 * exist yet (the sidecar may simply not be running), or it fails to
 * parse -- all treated as "no sidecar data available" rather than an
 * error, since the sidecar is an optional, independently-run process.
 *
 * The caller owns the returned reference and must release it with
 * json_object_put().
 */
json_object *sidecar_stats_json(const char *config_dir, struct config *config);

#endif
