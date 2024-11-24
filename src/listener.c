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

/** Macro to check outcome of bool function */
#define ERR_CHECK(f) if (!(f)) return false;

enum {
    MVTOKEN_INTEGER,
    MVTOKEN_FLOAT,
    MVTOKEN_STRING,
    
    MVTOKEN_ID,
    MVTOKEN_BUFFER,
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
    MVTOKEN_UPDATE,
    MVTOKEN_VIEWDIRECTION,
    MVTOKEN_VIEWVERTICAL,
    MVTOKEN_WINDOWTITLE,
    MVTOKEN_FONT,
    MVTOKEN_TEXT,
    
    MVTOKEN_QUOTE,
    MVTOKEN_NEWLINE,
    
    MVTOKEN_EOF
};

bool xcommand_lexstring(lexer *l, token *tok, error *err);

tokendefn mvtokens[] = {
    { "\n",         MVTOKEN_NEWLINE               , NULL },
    { "\"",         MVTOKEN_QUOTE                 , xcommand_lexstring },
    { "#",          MVTOKEN_ID                    , NULL },
    { "b",          MVTOKEN_BUFFER                , NULL },
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
    { "U",          MVTOKEN_UPDATE                , NULL },
    { "v",          MVTOKEN_VERTICES              , NULL },
    { "W",          MVTOKEN_WINDOWTITLE           , NULL },
    { "",           TOKEN_NONE                    , NULL }
};

/* -------------------------------------------------------
 * Lexer functions
 * ------------------------------------------------------- */

/** Record command file strings as a token */
bool xcommand_lexstring(lexer *l, token *tok, error *err) {
    char c;
    
    while (lex_peek(l) != '"' && !lex_isatend(l)) {
        if (lex_peek(l)=='\\') lex_advance(l); // Detect an escaped character

        lex_advance(l);
    }
    
    if (lex_isatend(l)) {
        morpho_writeerrorwithid(err, LEXER_UNTERMINATEDSTRING, NULL, tok->line, tok->posn);
        return false;
    }
    
    lex_advance(l); // Advance over final quote
    lex_recordtoken(l, MVTOKEN_STRING, tok);
    
    return true;
}

/** Initializes a lexer to lex morphoview command files */
void xcommand_initializelexer(lexer *l, char *src) {
    lex_init(l, src, 0);
    lex_settokendefns(l, mvtokens);
    lex_setnumbertype(l, MVTOKEN_INTEGER, MVTOKEN_FLOAT, MVTOKEN_FLOAT);
    lex_seteof(l, MVTOKEN_EOF);
}

/* -------------------------------------------------------
 * Command language fundamental types
 * ------------------------------------------------------- */

/** Parses an integer */
bool xcommand_parseinteger(parser *p, int *out) {
    long f;
    ERR_CHECK(parse_tokentointeger(p, &f));
    *out = (int) f;
    return true;
}

/* -------------------------------------------------------
 * Parse functions
 * ------------------------------------------------------- */

/** Parses a window title element */
bool xcommand_parsewindowtitle(parser *p, void *out) {
    printf("Window title\n");
    
    if (parse_checktokenadvance(p, MVTOKEN_STRING)) {
        // Parse the string
    } else return false;
    
    return true;
}

/** Parses an object definition */
bool xcommand_parseobject(parser *p, void *out) {
    int id;
    if (parse_checktokenadvance(p, MVTOKEN_INTEGER)) {
        ERR_CHECK(xcommand_parseinteger(p, &id));
    } else return false;
    
    printf("Object %i\n", id);
    
    if (parse_checktokenadvance(p, MVTOKEN_VERTICES)) {
        
    } else if (parse_checktokenadvance(p, MVTOKEN_LINES)) {
        
    } else if (parse_checktokenadvance(p, MVTOKEN_FACETS)) {
        
    }
    
    return true;
}

/** Parses scene elements */
bool xcommand_parseelements(parser *p, void *out) {
    while (!parse_checktoken(p, MVTOKEN_EOF) &&
           !parse_checktoken(p, MVTOKEN_SCENE)) {
        ERR_CHECK(parse_advance(p));
        parserule *rule = parse_getrule(p, p->previous.type);
        if (rule) {
            ERR_CHECK(rule->prefix(p, out));
        } else return false; // Should raise error
    }
}

/** Parses a scene command */
bool xcommand_parsescene(parser *p, void *out) {
    int id, dim;
    if (parse_checktokenadvance(p, MVTOKEN_INTEGER)) {
        ERR_CHECK(xcommand_parseinteger(p, &id));
    } else return false;
    
    if (parse_checktokenadvance(p, MVTOKEN_INTEGER)) {
        ERR_CHECK(xcommand_parseinteger(p, &dim));
    } else return false;
    
    printf("Scene id: %i dim: %i\n", id, dim);
    xcommand_parseelements(p, out);
    
    return true;
}

/** Base parse function to parse a command file */
bool xcommand_parsecommandfile(parser *p, void *out) {
    if (parse_checktokenadvance(p, MVTOKEN_SCENE)) {
        return xcommand_parsescene(p, out);
    }
    return false;
}

parserule mv_parserules[] = {
    PARSERULE_PREFIX(MVTOKEN_WINDOWTITLE, xcommand_parsewindowtitle),
    PARSERULE_PREFIX(MVTOKEN_OBJECT, xcommand_parseobject),
    PARSERULE_UNUSED(TOKEN_NONE)
};

/** Initializes a parser to parse morphoview command files */
void xcommand_initializeparser(parser *p, lexer *l, error *err, void *out) {
    parse_init(p, l, err, out);
    parse_setparsetable(p, mv_parserules);
    parse_setskipnewline(p, true, MVTOKEN_NEWLINE);
    parse_setbaseparsefn(p, xcommand_parsecommandfile);
}

bool xcommand_parse(char *in, error *err, void *out) {
    lexer l;
    xcommand_initializelexer(&l, in);

    parser p;
    xcommand_initializeparser(&p, &l, err, NULL);
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
    printf("-----\n");
    
    error err;
    error_init(&err);
    xcommand_parse(str, &err, NULL);
}
