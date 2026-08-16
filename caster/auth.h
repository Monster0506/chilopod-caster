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
 * disabled entry stays on file so it can be re-enabled without retyping
 * the password.
 */
struct rover_auth_entry {
	const char *user;
	const char *password;
	int enabled;
};
struct rover_auth_entry *rover_auth_parse(struct caster_state *caster, const char *filename);
void rover_auth_free(struct rover_auth_entry *this);
struct rover_auth_entry *rover_auth_lookup(struct rover_auth_entry *auth, const char *user);

#endif
