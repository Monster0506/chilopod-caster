#ifndef __API_H__
#define __API_H__

struct mime_content *api_ntrip_list_json(struct caster_state *caster, struct request *req);
struct mime_content *api_rtcm_json(struct caster_state *caster, struct request *req);
struct mime_content *api_mem_json(struct caster_state *caster, struct request *req);
struct mime_content *api_nodes_json(struct caster_state *caster, struct request *req);
struct mime_content *api_reload_json(struct caster_state *caster, struct request *req);
struct mime_content *api_drop_json(struct caster_state *caster, struct request *req);
struct mime_content *api_auth_list_json(struct caster_state *caster, struct request *req);
struct mime_content *api_auth_set_json(struct caster_state *caster, struct request *req);
struct mime_content *api_settings_get_json(struct caster_state *caster, struct request *req);
struct mime_content *api_settings_set_json(struct caster_state *caster, struct request *req);
struct mime_content *api_sync_json(struct caster_state *caster, struct request *req);
struct mime_content *api_add_source_json(struct caster_state *caster, struct request *req);
struct mime_content *api_remove_source_json(struct caster_state *caster, struct request *req);
struct mime_content *api_detect_source_json(struct caster_state *caster, struct request *req);

#endif
