/** @file listener.h
 *  @author T J Atherton
 *
 *  @brief ZeroMQ PAIR I/O thread for morphoview
 */

#ifndef listener_h
#define listener_h

#include <stdbool.h>

/** Reply / event strings sent to Morpho */
#define LISTENER_OK              "ok"
#define LISTENER_ERR_PREFIX      "err "
#define LISTENER_WINDOW_CLOSED   "window.closed"

/** Bind a PAIR socket and start the I/O thread. */
bool listener_bind(const char *endpoint);

/** Connect a PAIR socket and start the I/O thread. */
bool listener_connect(const char *endpoint);

/** True while the I/O thread is running. */
bool listener_isactive(void);

/** Queue a reply/event string for the I/O thread to send (thread-safe). */
bool listener_reply(const char *msg);

/** Ask the I/O thread to stop and join it. */
void listener_stop(void);

#endif /* listener_h */
