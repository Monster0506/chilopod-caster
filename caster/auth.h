#ifndef __AUTH_H__
#define __AUTH_H__

/*
 * Entry for host (as a client) or source (as a server) authorization
 */
struct auth_entry {
	const char *key;		// host name or mountpoint, depending on the file
	const char *user;		// username, if relevant (ntrip 2)
	const char *password;		// password (ntrip 1 or 2)
};

struct caster_state;
struct auth_entry *auth_parse(struct caster_state *caster, const char *filename);
void auth_free(struct auth_entry *this);
struct auth_entry *auth_lookup(struct auth_entry *auth, const char *key);
struct auth_entry *auth_lookupi(struct auth_entry *auth, const char *key);

/*
 * Entry for a rover (NTRIP GET client) account: unlike auth_entry, a
 * disabled entry stays on file so it can be re-enabled without retyping.
 */
struct rover_auth_entry {
	const char *user;
	const char *password;
	int enabled;
};
struct rover_auth_entry *rover_auth_parse(struct caster_state *caster, const char *filename);
void rover_auth_free(struct rover_auth_entry *this);
struct rover_auth_entry *rover_auth_lookup(struct rover_auth_entry *auth, const char *user);

/*
 * Entry for a /adm console account: same "stays on file when disabled" shape
 * as rover_auth_entry, plus a role.
 */
struct user_auth_entry {
	const char *user;
	const char *password;
	const char *role;		// "admin" or "viewer"
	int enabled;
};
struct user_auth_entry *user_auth_parse(struct caster_state *caster, const char *filename);
void user_auth_free(struct user_auth_entry *this);
struct user_auth_entry *user_auth_lookup(struct user_auth_entry *auth, const char *user);

/*
 * Resolved access level for a /adm request: NONE means the credentials
 * didn't check out, VIEWER can read, ADMIN can also mutate.
 */
enum admin_role { ADMIN_ROLE_NONE = 0, ADMIN_ROLE_VIEWER, ADMIN_ROLE_ADMIN };
struct ntrip_state;
enum admin_role resolve_admin_role(struct ntrip_state *this, const char *user, const char *password);
const char *admin_role_name(enum admin_role role);

#endif
