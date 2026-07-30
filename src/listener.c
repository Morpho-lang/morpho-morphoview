/** @file listener.c
 *  @author T J Atherton
 *
 *  @brief ZeroMQ PAIR I/O thread — recv ASCII → command_parse; send replies
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <czmq.h>

#include "listener.h"
#include "command.h"

/* -------------------------------------------------------
 * Reply queue (main/GLFW → I/O thread)
 * ------------------------------------------------------- */

typedef struct reply_node {
    char *msg;
    struct reply_node *next;
} reply_node;

static reply_node *reply_head = NULL;
static reply_node *reply_tail = NULL;
static pthread_mutex_t reply_mutex = PTHREAD_MUTEX_INITIALIZER;

static bool reply_enqueue(const char *msg) {
    if (!msg) return false;
    reply_node *node = malloc(sizeof(reply_node));
    if (!node) return false;
    node->msg = strdup(msg);
    if (!node->msg) {
        free(node);
        return false;
    }
    node->next = NULL;

    pthread_mutex_lock(&reply_mutex);
    if (reply_tail) {
        reply_tail->next = node;
        reply_tail = node;
    } else {
        reply_head = reply_tail = node;
    }
    pthread_mutex_unlock(&reply_mutex);
    return true;
}

static char *reply_dequeue(void) {
    pthread_mutex_lock(&reply_mutex);
    reply_node *node = reply_head;
    if (node) {
        reply_head = node->next;
        if (!reply_head) reply_tail = NULL;
    }
    pthread_mutex_unlock(&reply_mutex);

    if (!node) return NULL;
    char *msg = node->msg;
    free(node);
    return msg;
}

static void reply_clear(void) {
    char *msg;
    while ((msg = reply_dequeue()) != NULL) free(msg);
}

/* -------------------------------------------------------
 * Listener state
 * ------------------------------------------------------- */

static pthread_t listener_thread;
static volatile bool listener_running = false;
static volatile bool listener_stop_requested = false;
static char *listener_endpoint = NULL;
static bool listener_do_bind = false; /* true = bind, false = connect */

bool listener_isactive(void) {
    return listener_running;
}

bool listener_reply(const char *msg) {
    return reply_enqueue(msg);
}

/* -------------------------------------------------------
 * I/O thread
 * ------------------------------------------------------- */

static void listener_process_replies(zsock_t *sock) {
    char *msg;
    while ((msg = reply_dequeue()) != NULL) {
        zstr_send(sock, msg);
        free(msg);
    }
}

static void *listener_thread_main(void *arg) {
    (void) arg;

    zsock_t *sock = zsock_new(ZMQ_PAIR);
    if (!sock) {
        fprintf(stderr, "morphoview: Could not create ZMQ PAIR socket.\n");
        listener_running = false;
        return NULL;
    }

    int rc;
    if (listener_do_bind) {
        rc = zsock_bind(sock, "%s", listener_endpoint);
    } else {
        rc = zsock_connect(sock, "%s", listener_endpoint);
    }

    if (rc == -1) {
        fprintf(stderr, "morphoview: Could not %s ZMQ endpoint '%s'.\n",
                listener_do_bind ? "bind" : "connect", listener_endpoint);
        zsock_destroy(&sock);
        listener_running = false;
        command_wake();
        return NULL;
    }

    zsock_set_rcvtimeo(sock, 100); /* ms — allows stop checks + reply processing */

    while (!listener_stop_requested) {
        listener_process_replies(sock);

        char *msg = zstr_recv(sock);
        if (!msg) {
            /* Timeout or interrupt; loop to check stop / process replies */
            continue;
        }

        bool ok = command_parse(msg);
        zstr_free(&msg);

        if (ok) {
            zstr_send(sock, LISTENER_OK);
        } else {
            zstr_sendf(sock, "%sparse failed", LISTENER_ERR_PREFIX);
            /* Failed parse cleared the queue; still wake in case UI should refresh */
            command_wake();
        }
    }

    listener_process_replies(sock);
    zsock_destroy(&sock);
    listener_running = false;
    command_wake();
    return NULL;
}

static bool listener_start(const char *endpoint, bool do_bind) {
    if (listener_running) {
        fprintf(stderr, "morphoview: Listener already active.\n");
        return false;
    }
    if (!endpoint || !*endpoint) {
        fprintf(stderr, "morphoview: Missing ZMQ endpoint.\n");
        return false;
    }

    free(listener_endpoint);
    listener_endpoint = strdup(endpoint);
    if (!listener_endpoint) return false;

    listener_do_bind = do_bind;
    listener_stop_requested = false;
    listener_running = true;

    if (pthread_create(&listener_thread, NULL, listener_thread_main, NULL) != 0) {
        fprintf(stderr, "morphoview: Could not start listener thread.\n");
        listener_running = false;
        free(listener_endpoint);
        listener_endpoint = NULL;
        return false;
    }

    return true;
}

bool listener_bind(const char *endpoint) {
    return listener_start(endpoint, true);
}

bool listener_connect(const char *endpoint) {
    return listener_start(endpoint, false);
}

void listener_stop(void) {
    if (!listener_running && !listener_stop_requested) {
        reply_clear();
        free(listener_endpoint);
        listener_endpoint = NULL;
        return;
    }

    listener_stop_requested = true;
    if (listener_running) {
        pthread_join(listener_thread, NULL);
    }
    listener_running = false;
    reply_clear();
    free(listener_endpoint);
    listener_endpoint = NULL;
}
