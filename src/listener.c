/** @file listener.c
 *  @author T J Atherton
 *
 *  @brief Interface with external programs using CZMQ
 */

#include <czmq.h>

/* **********************************************************************
 * Morphoview lexer
 * ********************************************************************** */

#include "parse.h"

enum {
    MVTOKEN_INTEGER,
    MVTOKEN_FLOAT,
    MVTOKEN_STRING,
    
    MVTOKEN_COLOR,
    MVTOKEN_SELECTCOLOR,
    MVTOKEN_DRAW,
    MVTOKEN_OBJECT,
    MVTOKEN_VERTICES,
    MVTOKEN_POINTS,
    MVTOKEN_LINES,
    MVTOKEN_FACETS,
    MVTOKEN_IDENTITY,
    MVTOKEN_MATRIX,
    MVTOKEN_ROTATE,
    MVTOKEN_SCALE,
    MVTOKEN_SCENE,
    MVTOKEN_TRANSLATE,
    MVTOKEN_VIEWDIRECTION,
    MVTOKEN_VIEWVERTICAL,
    MVTOKEN_WINDOW,
    MVTOKEN_FONT,
    MVTOKEN_TEXT,
    MVTOKEN_EOF
};

tokendefn mvtokens[] = {
    { "c",          MVTOKEN_COLOR                 , NULL },
    { "C",          MVTOKEN_SELECTCOLOR           , NULL },
    { "d",          MVTOKEN_DRAW                  , NULL },
    { "o",          MVTOKEN_OBJECT                , NULL },
    { "p",          MVTOKEN_POINTS                , NULL },
    { "l",          MVTOKEN_LINES                 , NULL },
    { "f",          MVTOKEN_FACETS                , NULL },
    { "F",          MVTOKEN_FONT                  , NULL },
    { "i",          MVTOKEN_IDENTITY              , NULL },
    { "m",          MVTOKEN_MATRIX                , NULL },
    { "r",          MVTOKEN_ROTATE                , NULL },
    { "s",          MVTOKEN_SCALE                 , NULL },
    { "S",          MVTOKEN_SCENE                 , NULL },
    { "t",          MVTOKEN_TRANSLATE             , NULL },
    { "T",          MVTOKEN_TEXT                  , NULL },
    { "v",          MVTOKEN_VERTICES              , NULL },
    { "W",          MVTOKEN_WINDOW                , NULL },
    { "",           TOKEN_NONE                    , NULL }
};

/* -------------------------------------------------------
 * Lexer functions
 * ------------------------------------------------------- */

/** Initializes a lexer to lex morphoview command files */
void command_initializelexer(lexer *l, char *src) {
    lex_init(l, src, 0);
    lex_settokendefns(l, mvtokens);
    //lex_setprefn(l, json_lexpreprocess);
    lex_seteof(l, MVTOKEN_EOF);
}

/* -------------------------------------------------------
 * Parse functions
 * ------------------------------------------------------- */

/** Parses a scene command */
bool command_parsescene(parser *p, void *out) {
    int id, dim;
    /*ERRCHK(command_parseinteger(p, &id));
    ERRCHK(command_parseinteger(p, &dim));
    
#ifdef DEBUG_PARSER
    printf("Scene id: %i dim: %i\n", id, dim);
#endif
    
    p->scene = scene_new(id, dim);
    if (p->scene) p->display=display_open(p->scene);
    
    return (p->scene!=NULL);*/
}

/** Parses a window command */
bool command_parsewindow(parser *p, void *out) {
    char *name;
    
    /*if (command_parsestring(p, &name)) {
#ifdef DEBUG_PARSER
        printf("Window '%s'\n", name);
#endif
        if (name) {
            display_setwindowtitle(p->display, name);
            free(name);
        }
        return true;
    }*/
    
    return false;
}

parserule mv_parserules[] = {
    PARSERULE_PREFIX(MVTOKEN_SCENE, command_parsescene),
    PARSERULE_PREFIX(MVTOKEN_WINDOW, command_parsewindow),
    PARSERULE_UNUSED(TOKEN_NONE)
};

/** Initializes a parser to parse morphoview command files */
void command_initializeparser(parser *p, lexer *l, error *err, void *out) {
    parse_init(p, l, err, out);
    //parse_setbaseparsefn(p, json_parseelement);
    parse_setparsetable(p, mv_parserules);
    parse_setskipnewline(p, false, TOKEN_NONE);
}

bool xcommand_parse(char *in, error *err, value *out) {
    lexer l;
    command_initializelexer(&l, in);

    parser p;
    command_initializeparser(&p, &l, err, NULL);
    
    bool success=parse(&p);
    
    parse_clear(&p);
    lex_clear(&l);
    
    return success;
}

/* **********************************************************************
 * Listener
 * ********************************************************************** */

zsock_t *sock;

void listener(const char *endpoint) {
    sock=zsock_new_rep(endpoint);
    
    char *str=zstr_recv(sock);
    printf("Received: '%s'\n",str);
    zstr_send(sock, "ok");
    
    zsock_destroy(&sock);
}
