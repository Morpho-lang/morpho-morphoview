/** @file main.c
 *  @author T J Atherton
 *
 *  @brief Main entry point for morpho viewer application
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <morpho.h>

#include "command.h"
#include "display.h"
#include "text.h"
#include "listener.h"

int main(int argc, const char * argv[]) {
    morpho_initialize();
    
    scene_initialize();
    display_initialize();
    text_initialize();
    bool temp = false;
    bool parsed = false;
    bool bound = false;
    
    // Process arguments
    const char *file=NULL;
    for (unsigned int i=1; i<argc; i++) {
        const char *option = argv[i];
        if (argv[i] && option[0]=='-') {
            switch (option[1]) {
                case 'b': /* Bind to target */
                    bound=listener(option+2);
                    break;
                case 't': /* Temporary file; delete after */
                    temp=true;
                    break;
            }
        } else {
            file = option;
        }
    }
    
    // Parse a command file if provided
    if (file) {
        char *buffer = NULL;
        if (command_loadinput(file, &buffer)) {
            parsed=command_parse(buffer);
        }
        
        free(buffer);
    }
    
    if (parsed || bound) display_loop();
    
    text_finalize();
    display_finalize();
    scene_finalize();
    
    if (temp && file) command_removefile(file);
    
    morpho_finalize();
}
