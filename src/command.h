/** @file command.h
 *  @author T J Atherton
 *
 *  @brief Command language for morphoview
 */

#ifndef command_h
#define command_h

#include <stdio.h>
#include <stdbool.h>

#include "parse.h"

/* -------------------------------------------------------
 * Error ids
 * ------------------------------------------------------- */

#define COMMAND_UNRCGNZDCMND              "UnrcgnzdCmnd"
#define COMMAND_UNRCGNZDCMND_MSG          "Unrecognized morphoview command."

#define COMMAND_INVLDNMBR                 "InvldNmbr"
#define COMMAND_INVLDNMBR_MSG             "Improperly formatted number."

#define COMMAND_NOSCENE                   "NoScene"
#define COMMAND_NOSCENE_MSG               "No scene defined."

#define COMMAND_NOOBJECT                  "NoObject"
#define COMMAND_NOOBJECT_MSG              "No object defined."

#define COMMAND_EXPECTINTEGER             "ExpctInt"
#define COMMAND_EXPECTINTEGER_MSG         "Expected an integer."

#define COMMAND_EXPECTNUMBER              "ExpctNmbr"
#define COMMAND_EXPECTNUMBER_MSG          "Expected a number."

#define COMMAND_EXPECTSTRING              "ExpctStr"
#define COMMAND_EXPECTSTRING_MSG          "Expected a string."

/* -------------------------------------------------------
 * Prototypes
 * ------------------------------------------------------- */

bool command_getfilesize(FILE *f, size_t *s);
bool command_loadinput(const char *in, char **out);
void command_removefile(const char *in);

bool command_parse(char *in);

void command_initialize(void);
void command_finalize(void);

#endif /* command_h */
