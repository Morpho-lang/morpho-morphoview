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

int main(int argc, const char * argv[]) {
    morpho_initialize();
    command_initialize();
    scene_initialize();
    display_initialize();
    text_initialize();
    bool temp = false;
    bool parsed = false;
    bool listening = false;

    // Process arguments
    const char *file=NULL;
    for (unsigned int i=1; i<argc; i++) {
        const char *option = argv[i];
        if (argv[i] && option[0]=='-') {
            switch (option[1]) {
                case 't': /* Temporary file; delete after */
                    temp=true;
                    break;
                case 'b': /* Bind ZMQ PAIR to endpoint */
                    if (option[2]!='\0') {
                        listening = listener_bind(option+2);
                    } else if (i+1<argc) {
                        listening = listener_bind(argv[++i]);
                    } else {
                        fprintf(stderr, "morphoview: -b requires an endpoint.\n");
                    }
                    break;
                case 'c': /* Connect ZMQ PAIR to endpoint */
                    if (option[2]!='\0') {
                        listening = listener_connect(option+2);
                    } else if (i+1<argc) {
                        listening = listener_connect(argv[++i]);
                    } else {
                        fprintf(stderr, "morphoview: -c requires an endpoint.\n");
                    }
                    break;
            }
        } else {
            file = option;
        }
    }

    // Parse a command file if provided (enqueue only; apply on process)
    if (file) {
        char *buffer = NULL;
        printf("Loading %s\n", file);

        if (command_loadinput(file, &buffer)) {
            parsed=command_parse(buffer);
        }

        if (buffer) MORPHO_FREE(buffer);
    }

    if (parsed) command_process();

    if (parsed || listening) {
        display_loop();
    }

    listener_stop();

    text_finalize();
    display_finalize();
    scene_finalize();
    command_finalize();
    morpho_finalize();

    if (temp && file) command_removefile(file);
}
