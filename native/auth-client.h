#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A single exec-isolated conversation. This layer never invokes a consumer
 * callback: a caller can handle an event by destroying/replacing its owner. */
struct auth_client;
enum auth_result { AUTH_SUCCESS, AUTH_FAILED, AUTH_ERROR, AUTH_MAX_TRIES };
enum auth_event { AUTH_WAIT, AUTH_MESSAGE, AUTH_COMPLETE, AUTH_BROKEN };

struct auth_client *auth_client_create(void);
int auth_client_start(struct auth_client *, const char *helper, const char *service,
                      const char *user, const char *directory);
int auth_client_fd(const struct auth_client *);
int auth_client_wants_read(const struct auth_client *);
int auth_client_wants_write(const struct auth_client *);
enum auth_event auth_client_step(struct auth_client *);
int auth_client_respond(struct auth_client *, const char *bytes, size_t length);
const char *auth_client_message(const struct auth_client *);
int auth_client_response_required(const struct auth_client *);
int auth_client_response_visible(const struct auth_client *);
int auth_client_message_is_error(const struct auth_client *);
enum auth_result auth_client_result(const struct auth_client *);
int auth_client_memory_locked(const struct auth_client *);

/* Abort closes/wipes transport and kills only our unreaped worker. Repeated
 * reap calls are nonblocking. Destroy refuses to free a still-owned worker;
 * callers retain a retired client until reap returns true, with no callbacks
 * into a replacement conversation. */
void auth_client_abort(struct auth_client *);
int auth_client_reap(struct auth_client *);
int auth_client_destroy(struct auth_client *);

#ifdef __cplusplus
}
#endif
