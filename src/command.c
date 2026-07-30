/** @file command.c
 *  @author T J Atherton
 *
 *  @brief Command language for morphoview
 */
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "morpho.h"
#include "parse.h"
#include "memory.h"
#include "varray.h"

#include "command.h"
#include "scene.h"
#include "display.h"
#include "matrix3d.h"

/* -------------------------------------------------------
 * Parse context
 * ------------------------------------------------------- */

typedef struct {
    scene *scene;
    display *display;
    mat4x4 model;
    bool modelchanged;
    gobject *cobject;
} commandcontext;

void commandcontext_init(commandcontext *ctx) {
    ctx->scene=NULL;
    ctx->display=NULL;
    mat3d_identity4x4(ctx->model);
    ctx->modelchanged=false;
    ctx->cobject=NULL;
}

/* **********************************************************************
 * Morphoview lexer
 * ********************************************************************** */

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
    MVTOKEN_WINDOW,
    MVTOKEN_FONT,
    MVTOKEN_TEXT,

    MVTOKEN_QUOTE,
    MVTOKEN_MINUS,

    MVTOKEN_EOF
};

bool command_lexstring(lexer *l, token *tok, error *err);
bool command_lexnumber(lexer *l, token *tok, error *err);

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

    { "\"",         MVTOKEN_QUOTE                 , command_lexstring },
    { "-",          MVTOKEN_MINUS                 , command_lexnumber },

    { "",           TOKEN_NONE                    , NULL }
};

/** Skip morphoview whitespace (spaces and newlines) */
bool command_lexwhitespace(lexer *l, token *tok, error *err) {
    for (;;) {
        char c = lex_peek(l);

        switch (c) {
            case '\n':
                lex_newline(l); /* intentional fallthrough */
            case ' ':
            case '\t':
            case '\r':
                lex_advance(l);
                break;
            default:
                return true;
        }
    }
    return true;
}

/** Record command-file strings as a token */
bool command_lexstring(lexer *l, token *tok, error *err) {
    unsigned int startline = l->line, startpsn = l->posn;

    while (lex_peek(l) != '"' && !lex_isatend(l)) {
        if (lex_peek(l)=='\n') lex_newline(l);
        if (lex_peek(l)=='\\') lex_advance(l);
        lex_advance(l);
    }

    if (lex_isatend(l)) {
        morpho_writeerrorwithid(err, LEXER_UNTERMINATEDSTRING, NULL, startline, startpsn);
        return false;
    }

    lex_advance(l); /* closing quote */
    lex_recordtoken(l, MVTOKEN_STRING, tok);
    return true;
}

/** Record numbers as tokens (morpho lex_number style, plus leading '-' via processfn) */
bool command_lexnumber(lexer *l, token *tok, error *err) {
    tokentype type = l->inttype;

    if (!lex_isdigit(lex_peek(l))) {
        morpho_writeerrorwithid(err, COMMAND_INVLDNMBR, NULL, tok->line, tok->posn);
        return false;
    }

    while (lex_isdigit(lex_peek(l))) lex_advance(l);

    /* Fractional part — allow trailing '.' as in morpho / existing scene files (e.g. 0.) */
    char next = '\0';
    if (lex_peek(l)!='\0') next=lex_peekahead(l, 1);
    if (lex_peek(l) == '.' && (lex_isdigit(next) || lex_isspace(next) || next=='\0')) {
        type = l->flttype;
        lex_advance(l);
        while (lex_isdigit(lex_peek(l))) lex_advance(l);
    }

    if (lex_peek(l)=='e' || lex_peek(l)=='E') {
        type = l->flttype;
        lex_advance(l);
        if (lex_peek(l)=='+' || lex_peek(l)=='-') lex_advance(l);
        if (!lex_isdigit(lex_peek(l))) {
            morpho_writeerrorwithid(err, COMMAND_INVLDNMBR, NULL, tok->line, tok->posn);
            return false;
        }
        while (lex_isdigit(lex_peek(l))) lex_advance(l);
    }

    lex_recordtoken(l, type, tok);
    return true;
}

/** Lexer preprocess: leading digits start a number */
bool command_lexpreprocess(lexer *l, token *tok, error *err) {
    if (lex_isdigit(lex_peek(l))) return command_lexnumber(l, tok, err);
    return false;
}

/** Initialize a lexer for morphoview command files */
void command_initializelexer(lexer *l, char *src) {
    lex_init(l, src, 1);
    lex_settokendefns(l, mvtokens);
    lex_setnumbertype(l, MVTOKEN_INTEGER, MVTOKEN_FLOAT, MVTOKEN_FLOAT);
    lex_setprefn(l, command_lexpreprocess);
    lex_setwhitespacefn(l, command_lexwhitespace);
    lex_setstringinterpolation(l, false);
    lex_seteof(l, MVTOKEN_EOF);
    lex_setmatchkeywords(l, false);
}

/* **********************************************************************
 * Helpers
 * ********************************************************************** */

bool command_isnumerical(parser *p) {
    return parse_checktoken(p, MVTOKEN_INTEGER) ||
           parse_checktoken(p, MVTOKEN_FLOAT);
}

bool command_parseinteger(parser *p, int *out) {
    if (!parse_checktokenadvance(p, MVTOKEN_INTEGER)) {
        parse_error(p, false, COMMAND_EXPECTINTEGER);
        return false;
    }

    long f;
    PARSE_CHECK(parse_tokentointeger(p, &f));
    *out = (int) f;
    return true;
}

bool command_parsefloat(parser *p, float *out) {
    if (!command_isnumerical(p)) {
        parse_error(p, false, COMMAND_EXPECTNUMBER);
        return false;
    }

    PARSE_CHECK(parse_advance(p));

    double x;
    PARSE_CHECK(parse_tokentodouble(p, &x));
    *out = (float) x;
    return true;
}

bool command_parsestring(parser *p, char **out) {
    if (!parse_checktokenadvance(p, MVTOKEN_STRING)) {
        parse_error(p, false, COMMAND_EXPECTSTRING);
        return false;
    }

    int length = (int) p->previous.length - 2;
    if (length < 0) length = 0;

    char *str = malloc(sizeof(char)*(length+1));
    if (!str) {
        parse_error(p, true, ERROR_ALLOCATIONFAILED);
        return false;
    }

    strncpy(str, p->previous.start+1, length);
    str[length]='\0';
    *out = str;
    return true;
}

/* **********************************************************************
 * Command handlers
 * ********************************************************************** */

bool command_parsecolor(parser *p, void *out) {
    commandcontext *ctx = (commandcontext *) out;
    int id, indx=-1;
    int length=0;

    PARSE_CHECK(command_parseinteger(p, &id));

    while (command_isnumerical(p)) {
        float r[3];
        for (int i=0; i<3; i++) PARSE_CHECK(command_parsefloat(p, &r[i]));

        int ret=scene_adddata(ctx->scene, r, 3);
        if (indx<0) indx=ret;
        length++;
    }

    if (length>0) scene_addcolor(ctx->scene, id, length, indx);
    return true;
}

bool command_parseselectcolor(parser *p, void *out) {
    commandcontext *ctx = (commandcontext *) out;
    int id;

    PARSE_CHECK(command_parseinteger(p, &id));
    scene_adddraw(ctx->scene, COLOR, id, -1);
    return true;
}

bool command_parsedraw(parser *p, void *out) {
    commandcontext *ctx = (commandcontext *) out;
    int id, indx = SCENE_EMPTY;

    PARSE_CHECK(command_parseinteger(p, &id));

    if (ctx->modelchanged) {
        indx=scene_adddata(ctx->scene, ctx->model, 16);
        ctx->modelchanged=false;
    }

    scene_adddraw(ctx->scene, OBJECT, id, indx);
    return true;
}

bool command_parseobject(parser *p, void *out) {
    commandcontext *ctx = (commandcontext *) out;
    int id;

    PARSE_CHECK(command_parseinteger(p, &id));

    if (!ctx->scene) {
        parse_error(p, true, COMMAND_NOSCENE);
        return false;
    }

    ctx->cobject=scene_addobject(ctx->scene, id);
    return true;
}

bool command_parsevertices(parser *p, void *out) {
    commandcontext *ctx = (commandcontext *) out;

    if (!ctx->scene || !ctx->cobject) {
        parse_error(p, true, COMMAND_NOOBJECT);
        return false;
    }

    if (parse_checktoken(p, MVTOKEN_STRING)) {
        char *format=NULL;
        PARSE_CHECK(command_parsestring(p, &format));
        ctx->cobject->vertexdata.format=format;
    }

    while (command_isnumerical(p)) {
        float f;
        PARSE_CHECK(command_parsefloat(p, &f));

        int ret=scene_adddata(ctx->scene, &f, 1);
        if (ctx->cobject->vertexdata.indx==SCENE_EMPTY) {
            ctx->cobject->vertexdata.indx=ret;
            ctx->cobject->vertexdata.length=0;
        }
        ctx->cobject->vertexdata.length++;
    }

    return true;
}

bool command_parseindex(parser *p, void *out) {
    commandcontext *ctx = (commandcontext *) out;

    if (!ctx->scene || !ctx->cobject) {
        parse_error(p, true, COMMAND_NOOBJECT);
        return false;
    }

    gelement el = { .type = POINTS, .indx = SCENE_EMPTY, .length = 0 };

    if (p->previous.type==MVTOKEN_LINES) {
        el.type=LINES;
    } else if (p->previous.type==MVTOKEN_FACETS) {
        el.type=FACETS;
    }

    while (parse_checktoken(p, MVTOKEN_INTEGER)) {
        int i;
        PARSE_CHECK(command_parseinteger(p, &i));

        int ret=scene_addindex(ctx->scene, &i, 1);
        if (el.indx==SCENE_EMPTY) el.indx=ret;
        el.length++;
    }

    scene_addelement(ctx->cobject, &el);
    return true;
}

bool command_parseidentity(parser *p, void *out) {
    commandcontext *ctx = (commandcontext *) out;
    mat3d_identity4x4(ctx->model);
    ctx->modelchanged=true;
    return true;
}

bool command_parsematrix(parser *p, void *out) {
    commandcontext *ctx = (commandcontext *) out;
    mat4x4 x, m;

    for (int i=0; i<16; i++) {
        PARSE_CHECK(command_parsefloat(p, &x[i]));
    }

    mat3d_copy4x4(ctx->model, m);
    mat3d_mul4x4(m, x, ctx->model);
    ctx->modelchanged=true;
    return true;
}

bool command_parserotate(parser *p, void *out) {
    commandcontext *ctx = (commandcontext *) out;
    float phi, x[3];

    PARSE_CHECK(command_parsefloat(p, &phi));
    for (int i=0; i<3; i++) {
        PARSE_CHECK(command_parsefloat(p, &x[i]));
    }

    mat3d_rotate(ctx->model, x, phi, ctx->model);
    ctx->modelchanged=true;
    return true;
}

bool command_parsescale(parser *p, void *out) {
    commandcontext *ctx = (commandcontext *) out;
    float s;

    PARSE_CHECK(command_parsefloat(p, &s));
    mat3d_scale(ctx->model, s, ctx->model);
    ctx->modelchanged=true;
    return true;
}

bool command_parsetranslate(parser *p, void *out) {
    commandcontext *ctx = (commandcontext *) out;
    float x[3];

    for (int i=0; i<3; i++) {
        PARSE_CHECK(command_parsefloat(p, &x[i]));
    }

    mat3d_translate(ctx->model, x, ctx->model);
    ctx->modelchanged=true;
    return true;
}

bool command_parsescene(parser *p, void *out) {
    commandcontext *ctx = (commandcontext *) out;
    int id, dim;

    PARSE_CHECK(command_parseinteger(p, &id));
    PARSE_CHECK(command_parseinteger(p, &dim));

    ctx->scene = scene_new(id, dim);
    if (ctx->scene) ctx->display=display_open(ctx->scene);

    return (ctx->scene!=NULL);
}

bool command_parsewindow(parser *p, void *out) {
    commandcontext *ctx = (commandcontext *) out;
    char *name=NULL;

    PARSE_CHECK(command_parsestring(p, &name));
    if (name) {
        display_setwindowtitle(ctx->display, name);
        free(name);
    }
    return true;
}

bool command_parsefont(parser *p, void *out) {
    commandcontext *ctx = (commandcontext *) out;
    int id;
    char *file=NULL;
    float size;

    PARSE_CHECK(command_parseinteger(p, &id));
    PARSE_CHECK(command_parsestring(p, &file));
    PARSE_CHECK(command_parsefloat(p, &size));

    return scene_addfont(ctx->scene, id, file, size, NULL);
}

bool command_parsetext(parser *p, void *out) {
    commandcontext *ctx = (commandcontext *) out;
    int fontid;
    char *string=NULL;

    PARSE_CHECK(command_parseinteger(p, &fontid));
    PARSE_CHECK(command_parsestring(p, &string));

    int matindx=SCENE_EMPTY;
    int tid=scene_addtext(ctx->scene, fontid, string);

    if (ctx->modelchanged) {
        matindx=scene_adddata(ctx->scene, ctx->model, 16);
        ctx->modelchanged=false;
    }

    scene_adddraw(ctx->scene, TEXT, tid, matindx);
    return true;
}

/* **********************************************************************
 * Parser driver
 * ********************************************************************** */

bool command_parseelements(parser *p, void *out) {
    while (!parse_checktoken(p, MVTOKEN_EOF)) {
        PARSE_CHECK(parse_advance(p));

        parserule *rule = parse_getrule(p, p->previous.type);
        if (!rule || !rule->prefix) {
            parse_error(p, true, COMMAND_UNRCGNZDCMND);
            return false;
        }

        PARSE_CHECK(rule->prefix(p, out));
    }
    return true;
}

parserule mv_parserules[] = {
    PARSERULE_PREFIX(MVTOKEN_COLOR, command_parsecolor),
    PARSERULE_PREFIX(MVTOKEN_SELECTCOLOR, command_parseselectcolor),
    PARSERULE_PREFIX(MVTOKEN_DRAW, command_parsedraw),
    PARSERULE_PREFIX(MVTOKEN_OBJECT, command_parseobject),
    PARSERULE_PREFIX(MVTOKEN_VERTICES, command_parsevertices),
    PARSERULE_PREFIX(MVTOKEN_POINTS, command_parseindex),
    PARSERULE_PREFIX(MVTOKEN_LINES, command_parseindex),
    PARSERULE_PREFIX(MVTOKEN_FACETS, command_parseindex),
    PARSERULE_PREFIX(MVTOKEN_IDENTITY, command_parseidentity),
    PARSERULE_PREFIX(MVTOKEN_MATRIX, command_parsematrix),
    PARSERULE_PREFIX(MVTOKEN_ROTATE, command_parserotate),
    PARSERULE_PREFIX(MVTOKEN_SCALE, command_parsescale),
    PARSERULE_PREFIX(MVTOKEN_SCENE, command_parsescene),
    PARSERULE_PREFIX(MVTOKEN_TRANSLATE, command_parsetranslate),
    PARSERULE_PREFIX(MVTOKEN_WINDOW, command_parsewindow),
    PARSERULE_PREFIX(MVTOKEN_FONT, command_parsefont),
    PARSERULE_PREFIX(MVTOKEN_TEXT, command_parsetext),
    PARSERULE_UNUSED(TOKEN_NONE)
};

void command_initializeparser(parser *p, lexer *l, error *err, void *out) {
    parse_init(p, l, err, out);
    parse_setbaseparsefn(p, command_parseelements);
    parse_setparsetable(p, mv_parserules);
    parse_setskipnewline(p, false, TOKEN_NONE);
}

/** @brief Parses a command sequence */
bool command_parse(char *in) {
    commandcontext ctx;
    commandcontext_init(&ctx);

    error err;
    error_init(&err);

    lexer l;
    command_initializelexer(&l, in);

    parser p;
    command_initializeparser(&p, &l, &err, &ctx);

    bool success=parse(&p);

    parse_clear(&p);
    lex_clear(&l);

    if (!success || ERROR_FAILED(err)) {
        fprintf(stderr, "morphoview: Error [%s] at line %i: %s\n",
                err.id, err.line, err.msg);
        return false;
    }

    if (ctx.scene && ctx.display) {
        render_preparescene(&ctx.display->render, ctx.scene);
    }

    return true;
}

/* **********************************************************************
 * File I/O
 * ********************************************************************** */

/** Get the size of an open file
 *  @param[in] f file handle
 *  @param[out] s The file size */
bool command_getfilesize(FILE *f, size_t *s) {
    long int curr, size;
    curr=ftell(f);
    if (fseek(f, 0L, SEEK_END)!=0) return false;
    size = ftell(f);
    if (fseek(f, curr, SEEK_SET)!=0) return false;
    if (s) *s = size;
    return true;
}

/** Removes a command file (for temporary files)
 *  @param[in] in file name */
void command_removefile(const char *in) {
    size_t len = strlen(in) + 16;
    char remove[len];

#ifdef _WIN32
    sprintf(remove, "del \"%s\"", in);
    for (char *c = remove; *c != '\0'; c++) if (*c=='/') *c='\\';
#else
    sprintf(remove, "rm %s", in);
#endif

    int systemRet = system(remove);
    if(systemRet == -1){
        printf("Warning: the system method to remove a temporary file has failed.");
    }
}

/** Returns the contents of a file as a string
 *  @param[in] in file name
 *  @param[out] out a string with the contents of the file. Call MORPHO_FREE on this once done.
 *  @returns bool indicating success. */
bool command_loadinput(const char *in, char **out) {
    FILE *f=NULL;
    varray_char buffer;

    varray_charinit(&buffer);

    f=fopen(in, "r");
    if (!f) {
        fprintf(stderr, "morphoview: Couldn't open input file %s.\n", in);
        goto loadinput_cleanup;
    }

    size_t size;
    if (!command_getfilesize(f, &size)) goto loadinput_cleanup;

    if (size) {
        if (!varray_charresize(&buffer, (int) size+1)) {
            fprintf(stderr, "morphoview: Couldn't allocate buffer to load input file.\n");
            goto loadinput_cleanup;
        }

        for (char *c=buffer.data; !feof(f); c=c+strlen(c)) {
            if (!fgets(c, (int) (buffer.data+buffer.capacity-c), f)) { c[0]='\0'; break; }
        }
    }
    *out = buffer.data;
    fclose(f);
    return true;

loadinput_cleanup:
    if (f) fclose(f);
    varray_charclear(&buffer);
    return false;
}

/* **********************************************************************
 * Initialization
 * ********************************************************************** */

void command_initialize(void) {
    morpho_defineerror(COMMAND_UNRCGNZDCMND, ERROR_PARSE, COMMAND_UNRCGNZDCMND_MSG);
    morpho_defineerror(COMMAND_INVLDNMBR, ERROR_LEX, COMMAND_INVLDNMBR_MSG);
    morpho_defineerror(COMMAND_NOSCENE, ERROR_PARSE, COMMAND_NOSCENE_MSG);
    morpho_defineerror(COMMAND_NOOBJECT, ERROR_PARSE, COMMAND_NOOBJECT_MSG);
    morpho_defineerror(COMMAND_EXPECTINTEGER, ERROR_PARSE, COMMAND_EXPECTINTEGER_MSG);
    morpho_defineerror(COMMAND_EXPECTNUMBER, ERROR_PARSE, COMMAND_EXPECTNUMBER_MSG);
    morpho_defineerror(COMMAND_EXPECTSTRING, ERROR_PARSE, COMMAND_EXPECTSTRING_MSG);
}

void command_finalize(void) {
}
