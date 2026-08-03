/** @file main.c
 *  @author T J Atherton
 *
 *  @brief Main entry point for morpho viewer application
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "morpho.h"
#include "command.h"
#include "display.h"
#include "text.h"
#include "listener.h"

/** Next argv token or embedded -xVALUE; advances *i when consuming argv[++]. */
static const char *main_optarg(const char *option, unsigned int *i, int argc, const char *argv[]) {
    if (option[2]!='\0') return option+2;
    if (*i+1<(unsigned int)argc) return argv[++(*i)];
    return NULL;
}

int main(int argc, const char * argv[]) {
    morpho_initialize();
    command_initialize();
    listener_initialize();
    scene_initialize();
    display_initialize();
    text_initialize();
    bool temp = false;
    bool parsed = false;
    bool listening = false;
    /* Defer ZMQ start until after file parse so listener I/O cannot race staging. */
    char listen_mode = 0; /* 'b' bind, 'c' connect, 0 none */
    const char *listen_ep = NULL;

    const char *file=NULL;
    for (unsigned int i=1; i<(unsigned int)argc; i++) {
        const char *option = argv[i];
        if (argv[i] && option[0]=='-') {
            switch (option[1]) {
                case 't': /* Temporary file; delete after */
                    temp=true;
                    break;
                case 'b': /* Bind ZMQ PAIR (deferred) */
                case 'c': /* Connect ZMQ PAIR (deferred) */
                    listen_ep = main_optarg(option, &i, argc, argv);
                    if (listen_ep) {
                        listen_mode = option[1];
                    } else {
                        fprintf(stderr, "morphoview: -%c requires an endpoint.\n", option[1]);
                    }
                    break;
            }
        } else {
            file = option;
        }
    }

    if (file) {
        char *buffer = NULL;
        printf("Loading %s\n", file);

        if (command_loadinput(file, &buffer)) {
            parsed=command_parse(buffer);
        }

        if (buffer) MORPHO_FREE(buffer);
    }

    if (parsed) command_process();

    /* Start listener after file parse/process so staging is not concurrent. */
    if (listen_mode == 'b' && listen_ep) {
        listening = listener_bind(listen_ep);
    } else if (listen_mode == 'c' && listen_ep) {
        listening = listener_connect(listen_ep);
    }

    if (parsed || listening) {
        display_loop();
    }

    listener_finalize();

    text_finalize();
    display_finalize();
    scene_finalize();
    command_finalize();
    morpho_finalize();

    if (temp && file) command_removefile(file);
}
