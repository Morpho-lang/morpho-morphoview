/** @file listener.h
 *  @author T J Atherton
 *
 *  @brief Interface with external programs using CZMQ
 */

void listener(const char *endpoint);

#define LISTENER_UNRCGNZDCMMD         "UnrcgnzdCmmd"
#define LISTENER_UNRCGNZDCMMD_MSG     "Unrecognized command"

#define LISTENER_INVLDNMBR            "InvldNmbr"
#define LISTENER_INVLDNMBR_MSG        "Invalid number"
