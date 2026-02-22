#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <json-c/json_tokener.h>

#include "conf.h"
#include "adm.h"
#include "api.h"
#include "hash.h"
#include "livesource.h"
#include "ntripsrv.h"
#include "request.h"
#include "sourcetable.h"

static const char *
ui_mime_type(const char *path) {
	const char *dot = strrchr(path, '.');
	if (!dot) return "application/octet-stream";
	if (!strcmp(dot, ".html")) return "text/html";
	if (!strcmp(dot, ".js"))   return "application/javascript";
	if (!strcmp(dot, ".css"))  return "text/css";
	if (!strcmp(dot, ".json")) return "application/json";
	if (!strcmp(dot, ".svg"))  return "image/svg+xml";
	if (!strcmp(dot, ".ico"))  return "image/x-icon";
	return "application/octet-stream";
}

/*
 * Serve static files from ui_dir for GET /adm/ui/...
 * Returns 1 if handled (success or error), 0 if not a UI path.
 */
static int
ui_serve(struct ntrip_state *st, const char *uri, int *err) {
	const char *ui_dir = st->config->ui_dir;

	if (strcmp(uri, "/ui") && strncmp(uri, "/ui/", 4))
		return 0;

	if (ui_dir == NULL) {
		*err = 404;
		return 1;
	}

	/* Determine the relative path within ui_dir */
	const char *rel;
	if (!strcmp(uri, "/ui") || !strcmp(uri, "/ui/"))
		rel = "index.html";
	else
		rel = uri + 4;	/* skip "/ui/" */

	/* Path traversal check: forbid any ".." component */
	const char *p = rel;
	while (*p) {
		if (p[0] == '.' && p[1] == '.') {
			*err = 404;
			return 1;
		}
		while (*p && *p != '/') p++;
		if (*p == '/') p++;
	}

	/* Build absolute path: ui_dir "/" rel */
	size_t dlen = strlen(ui_dir);
	size_t rlen = strlen(rel);
	char *path = (char *)strmalloc(dlen + rlen + 2);
	if (path == NULL) {
		*err = 503;
		return 1;
	}
	memcpy(path, ui_dir, dlen);
	path[dlen] = '/';
	memcpy(path + dlen + 1, rel, rlen + 1);

	struct stat sb;
	int fd = open(path, O_RDONLY);
	strfree(path);
	if (fd < 0) {
		*err = 404;
		return 1;
	}

	if (fstat(fd, &sb) < 0 || (sb.st_mode & S_IFMT) != S_IFREG) {
		close(fd);
		*err = 404;
		return 1;
	}

	char *content = (char *)malloc(sb.st_size);
	if (content == NULL) {
		close(fd);
		*err = 503;
		return 1;
	}

	if (read(fd, content, sb.st_size) != sb.st_size) {
		free(content);
		close(fd);
		*err = 503;
		return 1;
	}
	close(fd);

	st->client_version = 0;
	struct evbuffer *output = bufferevent_get_output(st->bev);
	struct mime_content *m = mime_new(content, sb.st_size, ui_mime_type(rel), 0);
	ntripsrv_send_result_ok(st, output, m, NULL);
	ntrip_set_state(st, NTRIP_WAIT_CLOSE);
	return 1;
}

int admsrv(struct ntrip_state *st, const char *method, const char *root_uri, const char *uri, int *err, struct evkeyvalq *headers) {
	/* Serve static UI files without authentication */
	if (!strcmp(method, "GET") && ui_serve(st, uri, err))
		return (*err) ? -1 : 0;

	struct evbuffer *output = bufferevent_get_output(st->bev);
	int json_post = 0;
	struct request *req = request_new();
	if (req == NULL) {
		*err = 503;
		return -1;
	}
	req->st = st;

	st->client_version = 0;		// force a straight HTTP reply regardless of client headers

	/*
	 * Look for key=value arguments in url-encoded form, either in the request (GET) or in the body (POST).
	 *
	 * Build a hash table from them.
	 */
	if (!strcmp(method, "POST")) {
		if (st->content_type
		    && (strcmp(st->content_type, "application/x-www-form-urlencoded") && strcmp(st->content_type, "application/json"))) {
			request_free(req);
			*err = 503;
			return -1;
		}
		if (!strcmp(st->content_type, "application/json"))
			json_post = 1;

		if (!st->content) {
			request_free(req);
			*err = 400;
			return -1;
		}
		if (!strcmp(st->content_type, "application/x-www-form-urlencoded")) {
			req->hash = hash_from_urlencoding(st->content);
			if (!req->hash) {
				request_free(req);
				*err = 503;
				return -1;
			}
		}
	} else if (!strcmp(method, "GET") && st->query_string) {
		req->hash = hash_from_urlencoding(st->query_string);
		if (!req->hash) {
			request_free(req);
			*err = 503;
			return -1;
		}
	}

	if (req->hash) {
		/*
		 * Found url-encoded key=value pairs in the request, process
		 */

		/*
		 * Check credentials
		 */
		char *user, *password;
		user = hash_table_get(req->hash, "user");
		password = hash_table_get(req->hash, "password");

		if (!user || !password || !check_password(st, st->config->admin_user, user, password)) {
			request_free(req);
			*err = 401;
			return -1;
		}

		struct uri_calls {
			const char *uri;
			const char *method;
			struct mime_content *(*content_cb)(struct caster_state *caster, struct request *req);
		};
		const struct uri_calls calls[] = {
			{"/api/v1/net",	"GET", api_ntrip_list_json},
			{"/api/v1/rtcm", "GET", api_rtcm_json},
			{"/api/v1/mem","GET", api_mem_json},
			{"/api/v1/nodes","GET", api_nodes_json},
			{"/api/v1/livesources", "GET", livesource_list_json},
			{"/api/v1/sourcetables", "GET", sourcetable_list_json},
			{"/api/v1/reload", "POST", api_reload_json},
			{"/api/v1/drop", "POST", api_drop_json},
			{NULL, NULL, NULL}
		};

		int i;
		for (i = 0; calls[i].uri; i++) {
			if (!strcmp(uri, calls[i].uri))
				break;
		}

		/* Check the URI */
		if (calls[i].uri == NULL) {
			request_free(req);
			*err = 404;
			return -1;
		}

		/* Check the method */
		if (strcmp(method, calls[i].method)) {
			request_free(req);
			*err = 405;
			return -1;
		}

		/* Execute */
		joblist_append_ntrip_unlocked_content(st->caster->joblist, ntripsrv_deferred_output, st, calls[i].content_cb, req);
		return 0;
	} else if (json_post) {
		req->json = st->content ? json_tokener_parse(st->content) : NULL;
		if (req->json == NULL) {
			*err = 400;
		} else if (!strcmp(uri, "/api/v1/sync") && !strcmp(method, "POST")) {
			if (st->config->syncer_auth == NULL
					|| st->password == NULL || st->scheme_basic || strcmp(st->config->syncer_auth, st->password)) {
				*err = 401;
			} else {
				ntripsrv_deferred_output(st, api_sync_json, req);
				return 0;
			}
		} else
			*err = 404;
		request_free(req);
		return -1;
	}

	/* Legacy access */

	if (!st->user || !check_password(st, st->config->admin_user, st->user, st->password)) {
		request_free(req);
		int www_auth_value_len = strlen(root_uri) + 15;
		char *www_auth_value = (char *)strmalloc(www_auth_value_len);

		if (!www_auth_value) {
			ntrip_log(st, LOG_CRIT, "ntripsrv: out of memory");
			*err = 500;
			evbuffer_add_reference(output, "Out of memory :(\n", 17, NULL, NULL);
			return -1;
		}

		snprintf(www_auth_value, www_auth_value_len, "Basic realm=\"%s\"", root_uri);
		*err = 401;
		evhttp_add_header(headers, "WWW-Authenticate", www_auth_value);
		strfree(www_auth_value);
		return 0;
	}

	if (!strcmp(uri, "/mem") || !strcmp(uri, "/mem.json")) {
		request_free(req);
		int len = strlen(uri);
		int json = (len >= 5 && !strcmp(uri+len-5, ".json"))?1:0;

		struct mime_content *m = malloc_stats_dump(json);
		if (m) {
			ntripsrv_send_result_ok(st, output, m, NULL);
		} else {
			ntrip_log(st, LOG_CRIT, "ntripsrv: out of memory");
			*err = 500;
			evbuffer_add_reference(output, "Out of memory :(\n", 17, NULL, NULL);
			return -1;
		}
		ntrip_set_state(st, NTRIP_WAIT_CLOSE);
		return 0;
	} else if (!strcmp(uri, "/net")) {
		joblist_append_ntrip_unlocked_content(st->caster->joblist, ntripsrv_deferred_output, st, api_ntrip_list_json, req);
		return 0;
	} else {
		request_free(req);
		*err = 404;
		return -1;
	}
}
