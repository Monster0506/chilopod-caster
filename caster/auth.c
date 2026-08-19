#include "conf.h"
#include "auth.h"
#include "caster.h"
#include "ntrip_common.h"
#include "ntripsrv.h"
#include "util.h"

/*
 * Read user authentication file for the NTRIP server.
 */
struct auth_entry *auth_parse(struct caster_state *caster, const char *filename) {
	struct parsed_file *p;
	p = file_parse(caster->config_dir, filename, 3, ":", 0, &caster->flog);

	if (p == NULL) {
		logfmt(&caster->flog, LOG_ERR, "Can't read or parse %s", filename);
		return NULL;
	}
	struct auth_entry *auth = (struct auth_entry *)malloc(sizeof(struct auth_entry)*(p->nlines+1));

	if (auth == NULL) {
		file_free(p);
		return NULL;
	}

	int n;
	for (n = 0; n < p->nlines; n++) {
		auth[n].key = mystrdup(p->pls[n][0]);
		auth[n].user = mystrdup(p->pls[n][1]);
		auth[n].password = mystrdup(p->pls[n][2]);
	}
	auth[n].key = NULL;
	auth[n].user = NULL;
	auth[n].password = NULL;
	file_free(p);
	return auth;
}

void auth_free(struct auth_entry *this) {
	struct auth_entry *p = this;
	if (this == NULL)
		return;
	while (p->key || p->user || p->password) {
		strfree((char *)p->key);
		strfree((char *)p->user);
		strfree((char *)p->password);
		p++;
	}
	free(this);
}

static struct auth_entry *_auth_lookup(struct auth_entry *auth, const char *key,
		int case_insensitive) {
	while (auth->user != NULL) {
		if (!(case_insensitive?strcasecmp:strcmp)(auth->key, key))
			return auth;
		auth++;
	}
	return NULL;
}

/* Case-sensitive lookup for a key */
struct auth_entry *auth_lookup(struct auth_entry *auth, const char *key) {
	return _auth_lookup(auth, key, 0);
}

/* Case-insensitive lookup for a key */
struct auth_entry *auth_lookupi(struct auth_entry *auth, const char *key) {
	return _auth_lookup(auth, key, 1);
}

/*
 * Read the rover (NTRIP GET client) account file: "user:password:Y/N"
 * lines, third field case-insensitive, anything but Y/1/yes/true disables it.
 */
struct rover_auth_entry *rover_auth_parse(struct caster_state *caster, const char *filename) {
	struct parsed_file *p;
	p = file_parse(caster->config_dir, filename, 3, ":", 0, &caster->flog);

	if (p == NULL) {
		logfmt(&caster->flog, LOG_ERR, "Can't read or parse %s", filename);
		return NULL;
	}
	struct rover_auth_entry *auth = (struct rover_auth_entry *)malloc(sizeof(struct rover_auth_entry)*(p->nlines+1));

	if (auth == NULL) {
		file_free(p);
		return NULL;
	}

	int n;
	for (n = 0; n < p->nlines; n++) {
		auth[n].user = mystrdup(p->pls[n][0]);
		auth[n].password = mystrdup(p->pls[n][1]);
		char *e = p->pls[n][2];
		auth[n].enabled = !strcasecmp(e, "Y") || !strcasecmp(e, "yes") || !strcasecmp(e, "true") || !strcmp(e, "1");
	}
	auth[n].user = NULL;
	auth[n].password = NULL;
	auth[n].enabled = 0;
	file_free(p);
	return auth;
}

void rover_auth_free(struct rover_auth_entry *this) {
	struct rover_auth_entry *p = this;
	if (this == NULL)
		return;
	while (p->user) {
		strfree((char *)p->user);
		strfree((char *)p->password);
		p++;
	}
	free(this);
}

struct rover_auth_entry *rover_auth_lookup(struct rover_auth_entry *auth, const char *user) {
	while (auth->user != NULL) {
		if (!strcmp(auth->user, user))
			return auth;
		auth++;
	}
	return NULL;
}

/*
 * Read the /adm console account file: "user:password:role:Y" or
 * "user:password:role:N" lines, same enabled convention as rover_auth_parse.
 */
struct user_auth_entry *user_auth_parse(struct caster_state *caster, const char *filename) {
	struct parsed_file *p;
	p = file_parse(caster->config_dir, filename, 4, ":", 0, &caster->flog);

	if (p == NULL) {
		logfmt(&caster->flog, LOG_ERR, "Can't read or parse %s", filename);
		return NULL;
	}
	struct user_auth_entry *auth = (struct user_auth_entry *)malloc(sizeof(struct user_auth_entry)*(p->nlines+1));

	if (auth == NULL) {
		file_free(p);
		return NULL;
	}

	int n;
	for (n = 0; n < p->nlines; n++) {
		auth[n].user = mystrdup(p->pls[n][0]);
		auth[n].password = mystrdup(p->pls[n][1]);
		auth[n].role = mystrdup(p->pls[n][2]);
		char *e = p->pls[n][3];
		auth[n].enabled = !strcasecmp(e, "Y") || !strcasecmp(e, "yes") || !strcasecmp(e, "true") || !strcmp(e, "1");
	}
	auth[n].user = NULL;
	auth[n].password = NULL;
	auth[n].role = NULL;
	auth[n].enabled = 0;
	file_free(p);
	return auth;
}

void user_auth_free(struct user_auth_entry *this) {
	struct user_auth_entry *p = this;
	if (this == NULL)
		return;
	while (p->user) {
		strfree((char *)p->user);
		strfree((char *)p->password);
		strfree((char *)p->role);
		p++;
	}
	free(this);
}

struct user_auth_entry *user_auth_lookup(struct user_auth_entry *auth, const char *user) {
	while (auth->user != NULL) {
		if (!strcmp(auth->user, user))
			return auth;
		auth++;
	}
	return NULL;
}

/*
 * admin_user/source_auth_file is checked first and is always full admin,
 * so it can't be locked out by user_auth_file, which is consulted only after.
 */
enum admin_role resolve_admin_role(struct ntrip_state *this, const char *user, const char *password) {
	if (user && password && check_password(this, this->config->admin_user, user, password) != CHECKPW_MOUNTPOINT_INVALID)
		return ADMIN_ROLE_ADMIN;

	if (user && password && this->config->user_auth) {
		struct user_auth_entry *e = user_auth_lookup(this->config->user_auth, user);
		if (e && e->enabled && !strcmp(e->password, password))
			return !strcmp(e->role, "admin") ? ADMIN_ROLE_ADMIN : ADMIN_ROLE_VIEWER;
	}

	return ADMIN_ROLE_NONE;
}

const char *admin_role_name(enum admin_role role) {
	return role == ADMIN_ROLE_ADMIN ? "admin" : role == ADMIN_ROLE_VIEWER ? "viewer" : "none";
}
