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
#include "listener.h"

/** Macro to check outcome of bool function */
#define ERR_CHECK(f) if (!(f)) return false;

enum {
    MVTOKEN_INTEGER,
    MVTOKEN_FLOAT,
    MVTOKEN_STRING,
    
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
    
    MVTOKEN_ID,
    MVTOKEN_QUOTE,
    MVTOKEN_MINUS,
    MVTOKEN_NEWLINE,
    
    MVTOKEN_EOF
};

bool xcommand_lexstring(lexer *l, token *tok, error *err);
bool xcommand_lexnumber(lexer *l, token *tok, error *err);

tokendefn mvtokens[] = {
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
    
    { "#",          MVTOKEN_ID                    , NULL },
    { "\"",         MVTOKEN_QUOTE                 , xcommand_lexstring },
    { "-",          MVTOKEN_MINUS                 , xcommand_lexnumber },
    { "\n",         MVTOKEN_NEWLINE               , NULL },
    
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

/** Record numbers as a token */
bool xcommand_lexnumber(lexer *l, token *tok, error *err) {
    bool hasexp=false;
    tokentype type = MVTOKEN_INTEGER;
    
    // Detect if we are missing digits (ie an isolated '-')
    char c = lex_peek(l);
    if (c=='0') {
        lex_advance(l);
        if (lex_isdigit(lex_peek(l))) goto xcommand_lexnumberinvld; // Cannot follow '0' by digits.
    }
    
    if (lex_isdigit(c)) {
        // Advance through initial digits
        while (lex_isdigit(lex_peek(l)) && !lex_isatend(l)) lex_advance(l);
    } else goto xcommand_lexnumberinvld;
    
    // Detect fractional separator
    if (lex_peek(l)=='.') {
        lex_advance(l);
        type = MVTOKEN_FLOAT;
        
        while (lex_isdigit(lex_peek(l)) && !lex_isatend(l)) lex_advance(l);
    };
    
    if (lex_peek(l)=='e' || lex_peek(l)=='E') {
        lex_advance(l); hasexp=true; type = MVTOKEN_FLOAT;
    }
    if (lex_peek(l)=='+' || lex_peek(l)=='-') {
        if (hasexp) lex_advance(l); else goto xcommand_lexnumberinvld; // Only allow +/- after exp
    }
    
    // Digits are required after exponent
    if (hasexp && !lex_isdigit(lex_peek(l))) goto xcommand_lexnumberinvld;
    while (lex_isdigit(lex_peek(l)) && !lex_isatend(l)) lex_advance(l);
    
    lex_recordtoken(l, type, tok);
    
    return true;

xcommand_lexnumberinvld:
    morpho_writeerrorwithid(err, LISTENER_INVLDNMBR, NULL, tok->line, tok->posn);
    return false;
}

/** Lexer token preprocessor function */
bool xcommand_lexpreprocess(lexer *l, token *tok, error *err) {
    char c = lex_peek(l);
    if (lex_isdigit(c)) return xcommand_lexnumber(l, tok, err);
    return false;
}

/** Initializes a lexer to lex morphoview command files */
void xcommand_initializelexer(lexer *l, char *src) {
    lex_init(l, src, 0);
    lex_settokendefns(l, mvtokens);
    lex_setnumbertype(l, MVTOKEN_INTEGER, MVTOKEN_FLOAT, MVTOKEN_FLOAT);
    lex_setprefn(l, xcommand_lexpreprocess);
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

/** Parses a string */
bool xcommand_parsestring(parser *p) {
    return true;
}

/* -------------------------------------------------------
 * Parse various elements
 * ------------------------------------------------------- */

/** Parses a draw command */
bool xcommand_parsedraw(parser *p, void *out) {
    int id=-1;
    if (parse_checktokenadvance(p, MVTOKEN_INTEGER)) {
        ERR_CHECK(xcommand_parseinteger(p, &id));
    }
    
    printf("Draw object %i\n", id);
    return true;
}

/** Parses facet definitions */
bool xcommand_parsefacets(parser *p, void *out) {
    printf("-Facets\n");
    while (parse_checktoken(p, MVTOKEN_INTEGER)) {
        parse_advance(p);
    }
    return true;
}

/** Parses set identity operation */
bool xcommand_parseidentity(parser *p, void *out) {
    printf("-Identity\n");
    return true;
}

/** Parses line definitions */
bool xcommand_parselines(parser *p, void *out) {
    printf("-Lines\n");
    while (parse_checktoken(p, MVTOKEN_INTEGER)) {
        parse_advance(p);
    }
    return true;
}

/** Parses an object definition */
bool xcommand_parseobject(parser *p, void *out) {
    int id;
    if (parse_checktokenadvance(p, MVTOKEN_INTEGER)) {
        ERR_CHECK(xcommand_parseinteger(p, &id));
    } else return false;
    
    printf("Object %i\n", id);
    return true;
}

/** Parses a scale command */
bool xcommand_parsescale(parser *p, void *out) {
    if (parse_checktokenadvance(p, MVTOKEN_FLOAT)) {
        
    }
    
    printf("Scale \n");
    return true;
}

/** Parses a scene definition */
bool xcommand_parsescene(parser *p, void *out) {
    int id, dim;
    if (parse_checktokenadvance(p, MVTOKEN_INTEGER)) {
        ERR_CHECK(xcommand_parseinteger(p, &id));
    } else return false;
    
    if (parse_checktokenadvance(p, MVTOKEN_INTEGER)) {
        ERR_CHECK(xcommand_parseinteger(p, &dim));
    } else return false;
    
    printf("Scene id: %i dim: %i\n", id, dim);
    return true;
}

/** Parses a translate command */
bool xcommand_parsetranslate(parser *p, void *out) {
    for (int i=0; i<3; i++) {
        parse_checktokenadvance(p, MVTOKEN_INTEGER);
        parse_checktokenadvance(p, MVTOKEN_FLOAT);
    }
    
    printf("Translate \n");
    return true;
}

/** Parses vertex definitions */
bool xcommand_parsevertices(parser *p, void *out) {
    printf("-Vertices\n");
    if (parse_checktokenadvance(p, MVTOKEN_STRING)) {
        xcommand_parsestring(p);
    }
    
    while (parse_checktokenadvance(p, MVTOKEN_FLOAT)) {
        
    }
    return true;
}

/** Parses a window title element */
bool xcommand_parsewindowtitle(parser *p, void *out) {
    printf("Window title\n");
    
    if (parse_checktokenadvance(p, MVTOKEN_STRING)) {
        xcommand_parsestring(p);
    } else return false;
    
    return true;
}

/* -------------------------------------------------------
 * Parser
 * ------------------------------------------------------- */

/** Parses scene elements */
bool xcommand_parseelements(parser *p, void *out) {
    while (!parse_checktoken(p, MVTOKEN_EOF)) {
        ERR_CHECK(parse_advance(p));
        parserule *rule = parse_getrule(p, p->previous.type);
        if (rule) {
            ERR_CHECK(rule->prefix(p, out));
        } else {
            parse_error(p, true, LISTENER_UNRCGNZDCMMD);
            return false; // Should raise error
        }
    }
}

parserule mv_parserules[] = {
    PARSERULE_PREFIX(MVTOKEN_DRAW, xcommand_parsedraw),
    PARSERULE_PREFIX(MVTOKEN_FACETS, xcommand_parsefacets),
    PARSERULE_PREFIX(MVTOKEN_IDENTITY, xcommand_parseidentity),
    PARSERULE_PREFIX(MVTOKEN_LINES, xcommand_parselines),
    PARSERULE_PREFIX(MVTOKEN_OBJECT, xcommand_parseobject),
    PARSERULE_PREFIX(MVTOKEN_SCALE, xcommand_parsescale),
    PARSERULE_PREFIX(MVTOKEN_SCENE, xcommand_parsescene),
    PARSERULE_PREFIX(MVTOKEN_TRANSLATE, xcommand_parsetranslate),
    PARSERULE_PREFIX(MVTOKEN_VERTICES, xcommand_parsevertices),
    PARSERULE_PREFIX(MVTOKEN_WINDOWTITLE, xcommand_parsewindowtitle),
    PARSERULE_UNUSED(TOKEN_NONE)
};

/** Initializes a parser to parse morphoview command files */
void xcommand_initializeparser(parser *p, lexer *l, error *err, void *out) {
    parse_init(p, l, err, out);
    parse_setparsetable(p, mv_parserules);
    parse_setskipnewline(p, true, MVTOKEN_NEWLINE);
    parse_setbaseparsefn(p, xcommand_parseelements);
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
 * Initialization
 * ********************************************************************** */

void listener_init(void) {
    morpho_defineerror(LISTENER_UNRCGNZDCMMD, ERROR_HALT, LISTENER_UNRCGNZDCMMD_MSG);
    morpho_defineerror(LISTENER_INVLDNMBR, ERROR_HALT, LISTENER_INVLDNMBR_MSG);
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
    
    listener_init();
    xcommand_parse(str, &err, NULL);
    if (morpho_checkerror(&err)) {
        printf("Error [%s]: %s\n", err.id, err.msg);
    }
}
