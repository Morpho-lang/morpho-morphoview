/** @file command.c
 *  @author T J Atherton
 *
 *  @brief Command language for morphoview
 */
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <pthread.h>

#include "morpho.h"
#include "parse.h"
#include "memory.h"
#include "varray.h"

#include "command.h"
#include "scene.h"
#include "display.h"
#include "render.h"
#include "matrix3d.h"

DEFINE_VARRAY(mv_commandptr, mv_command *);

/* -------------------------------------------------------
 * Parse / apply contexts
 * ------------------------------------------------------- */

typedef struct {
    mat4x4 model;
    bool modelchanged;
    bool has_scene;
    bool has_object;
} command_parsectx;

typedef struct {
    scene *scene;
    display *display;
    gobject *cobject;
} command_applyctx;

void command_parsectx_init(command_parsectx *ctx) {
    mat3d_identity4x4(ctx->model);
    ctx->modelchanged=false;
    ctx->has_scene=false;
    ctx->has_object=false;
}

void command_applyctx_init(command_applyctx *ctx) {
    ctx->scene=NULL;
    ctx->display=NULL;
    ctx->cobject=NULL;
}

/* -------------------------------------------------------
 * Allocation / free
 * ------------------------------------------------------- */

/** Allocate a typed command and set its header type. */
void *command_new(mv_command_type type, size_t size) {
    mv_command *cmd = calloc(1, size);
    if (cmd) cmd->type=type;
    return cmd;
}

void command_free(mv_command *cmd) {
    if (!cmd) return;

    switch (cmd->type) {
        case MVCMD_WINDOW_TITLE:
            free(MVCMD_AS_WINDOW(cmd)->title);
            break;
        case MVCMD_VERTICES:
            free(MVCMD_AS_VERTICES(cmd)->format);
            free(MVCMD_AS_VERTICES(cmd)->data);
            break;
        case MVCMD_ELEMENT:
            free(MVCMD_AS_ELEMENT(cmd)->indices);
            break;
        case MVCMD_COLOR:
            free(MVCMD_AS_COLOR(cmd)->rgb);
            break;
        case MVCMD_FONT:
            free(MVCMD_AS_FONT(cmd)->path);
            break;
        case MVCMD_TEXT:
            free(MVCMD_AS_TEXT(cmd)->string);
            break;
        default:
            break;
    }

    free(cmd);
}

/* -------------------------------------------------------
 * Command queue
 * ------------------------------------------------------- */

static varray_mv_commandptr command_queue;
static pthread_mutex_t command_queue_mutex = PTHREAD_MUTEX_INITIALIZER;

void command_queue_init(void) {
    varray_mv_commandptrinit(&command_queue);
}

void command_queue_clear(void) {
    pthread_mutex_lock(&command_queue_mutex);
    for (unsigned int i=0; i<command_queue.count; i++) {
        command_free(command_queue.data[i]);
    }
    varray_mv_commandptrclear(&command_queue);
    pthread_mutex_unlock(&command_queue_mutex);
}

void command_wake(void) {
    glfwPostEmptyEvent();
}

bool command_enqueue(mv_command *cmd) {
    pthread_mutex_lock(&command_queue_mutex);
    bool wasempty = (command_queue.count == 0);
    bool ok = varray_mv_commandptradd(&command_queue, &cmd, 1);
    pthread_mutex_unlock(&command_queue_mutex);
    if (!ok) return false;
    if (wasempty) command_wake();
    return true;
}

/* **********************************************************************
 * Apply
 * ********************************************************************** */

bool command_apply(mv_command *cmd, command_applyctx *ctx) {
    switch (cmd->type) {
        case MVCMD_SCENE_CREATE: {
            mv_cmd_scene *c = MVCMD_AS_SCENE(cmd);
            bool created = false;
            scene *s = scene_find(c->id);

            if (!s) {
                s = scene_new(c->id, c->dim);
                if (!s) return false;
                created = true;
            }

            ctx->display = display_findforscene(s);
            if (!ctx->display) {
                ctx->display = display_open(s);
                if (!ctx->display) {
                    if (created) scene_free(s);
                    return false;
                }
            }

            ctx->scene = s;
            ctx->cobject = NULL;
            return true;
        }

        case MVCMD_UPDATE_SCENE: {
            mv_cmd_update_scene *c = MVCMD_AS_UPDATE_SCENE(cmd);
            scene *s = scene_find(c->id);
            if (!s) {
                fprintf(stderr, "morphoview: No scene with id '%i'.\n", c->id);
                return false;
            }

            scene_clear(s);

            ctx->display = display_findforscene(s);
            if (ctx->display && ctx->display->window) {
                glfwMakeContextCurrent(ctx->display->window);
                render_reset(&ctx->display->render);
            }

            ctx->scene = s;
            ctx->cobject = NULL;
            return true;
        }

        case MVCMD_WINDOW_TITLE: {
            mv_cmd_window *c = MVCMD_AS_WINDOW(cmd);
            if (ctx->display && c->title) {
                display_setwindowtitle(ctx->display, c->title);
            }
            return true;
        }

        case MVCMD_OBJECT:
            if (!ctx->scene) return false;
            ctx->cobject=scene_addobject(ctx->scene, MVCMD_AS_OBJECT(cmd)->id);
            return (ctx->cobject!=NULL);

        case MVCMD_VERTICES: {
            mv_cmd_vertices *c = MVCMD_AS_VERTICES(cmd);
            if (!ctx->scene || !ctx->cobject) return false;

            if (c->format) {
                ctx->cobject->vertexdata.format=c->format;
                c->format=NULL; /* transferred */
            }

            if (c->length>0 && c->data) {
                int ret=scene_adddata(ctx->scene, c->data, c->length);
                if (ctx->cobject->vertexdata.indx==SCENE_EMPTY) {
                    ctx->cobject->vertexdata.indx=ret;
                    ctx->cobject->vertexdata.length=0;
                }
                ctx->cobject->vertexdata.length += c->length;
            }
            return true;
        }

        case MVCMD_ELEMENT: {
            mv_cmd_element *c = MVCMD_AS_ELEMENT(cmd);
            if (!ctx->scene || !ctx->cobject) return false;

            gelement el = {
                .type = c->type,
                .indx = SCENE_EMPTY,
                .length = 0
            };

            if (c->length>0 && c->indices) {
                int ret=scene_addindex(ctx->scene, c->indices, c->length);
                el.indx=ret;
                el.length=c->length;
            }

            scene_addelement(ctx->cobject, &el);
            return true;
        }

        case MVCMD_COLOR: {
            mv_cmd_color *c = MVCMD_AS_COLOR(cmd);
            if (!ctx->scene) return false;

            if (c->length>0 && c->rgb) {
                int indx=scene_adddata(ctx->scene, c->rgb, c->length*3);
                scene_addcolor(ctx->scene, c->id, c->length, indx);
            }
            return true;
        }

        case MVCMD_SELECT_COLOR:
            if (!ctx->scene) return false;
            scene_adddraw(ctx->scene, COLOR, MVCMD_AS_SELECT_COLOR(cmd)->id, -1);
            return true;

        case MVCMD_DRAW: {
            mv_cmd_draw *c = MVCMD_AS_DRAW(cmd);
            if (!ctx->scene) return false;
            int matindx = SCENE_EMPTY;
            if (c->has_matrix) {
                matindx=scene_adddata(ctx->scene, c->matrix, 16);
            }
            scene_adddraw(ctx->scene, OBJECT, c->id, matindx);
            return true;
        }

        case MVCMD_FONT: {
            mv_cmd_font *c = MVCMD_AS_FONT(cmd);
            if (!ctx->scene) return false;
            return scene_addfont(ctx->scene, c->id, c->path, c->size, NULL);
        }

        case MVCMD_TEXT: {
            mv_cmd_text *c = MVCMD_AS_TEXT(cmd);
            if (!ctx->scene) return false;
            if (!scene_getfontfromid(ctx->scene, c->fontid)) {
                fprintf(stderr, "Font id '%i' not found.\n", c->fontid);
                return false;
            }

            int tid=scene_addtext(ctx->scene, c->fontid, c->string);
            c->string=NULL; /* transferred to scene */

            int matindx=SCENE_EMPTY;
            if (c->has_matrix) {
                matindx=scene_adddata(ctx->scene, c->matrix, 16);
            }
            scene_adddraw(ctx->scene, TEXT, tid, matindx);
            return true;
        }

        case MVCMD_PREPARE:
            display_prepareall();
            return true;
    }

    return false;
}

int command_process(void) {
    command_applyctx ctx;
    command_applyctx_init(&ctx);

    /* Steal the queue under the lock so apply (GL) does not block the I/O thread. */
    varray_mv_commandptr batch;
    pthread_mutex_lock(&command_queue_mutex);
    batch = command_queue;
    varray_mv_commandptrinit(&command_queue);
    pthread_mutex_unlock(&command_queue_mutex);

    int applied=0;
    for (unsigned int i=0; i<batch.count; i++) {
        mv_command *cmd = batch.data[i];
        if (!command_apply(cmd, &ctx)) {
            for (unsigned int j=i; j<batch.count; j++) {
                command_free(batch.data[j]);
            }
            batch.count=0;
            varray_mv_commandptrclear(&batch);
            return applied;
        }
        command_free(cmd);
        applied++;
    }

    batch.count=0;
    varray_mv_commandptrclear(&batch);
    return applied;
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
    MVTOKEN_UPDATE,
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
    { "U",          MVTOKEN_UPDATE                , NULL },
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

bool command_enqueue_owned(parser *p, mv_command *cmd) {
    if (!cmd) {
        parse_error(p, true, ERROR_ALLOCATIONFAILED);
        return false;
    }
    if (command_enqueue(cmd)) return true;
    command_free(cmd);
    parse_error(p, true, ERROR_ALLOCATIONFAILED);
    return false;
}

/* **********************************************************************
 * Parse handlers (emit only)
 * ********************************************************************** */

bool command_parsecolor(parser *p, void *out) {
    (void) out;
    int id;
    varray_float rgb;

    PARSE_CHECK(command_parseinteger(p, &id));

    varray_floatinit(&rgb);
    while (command_isnumerical(p)) {
        float r[3];
        for (int i=0; i<3; i++) {
            if (!command_parsefloat(p, &r[i])) {
                varray_floatclear(&rgb);
                return false;
            }
        }
        if (!varray_floatadd(&rgb, r, 3)) {
            varray_floatclear(&rgb);
            parse_error(p, true, ERROR_ALLOCATIONFAILED);
            return false;
        }
    }

    mv_cmd_color *cmd = command_new(MVCMD_COLOR, sizeof(mv_cmd_color));
    if (!cmd) {
        varray_floatclear(&rgb);
        parse_error(p, true, ERROR_ALLOCATIONFAILED);
        return false;
    }
    cmd->id=id;
    cmd->length=rgb.count/3;

    if (rgb.count>0) {
        cmd->rgb=malloc(sizeof(float)*rgb.count);
        if (!cmd->rgb) {
            varray_floatclear(&rgb);
            command_free(&cmd->cmd);
            parse_error(p, true, ERROR_ALLOCATIONFAILED);
            return false;
        }
        memcpy(cmd->rgb, rgb.data, sizeof(float)*rgb.count);
    }
    varray_floatclear(&rgb);

    return command_enqueue_owned(p, &cmd->cmd);
}

bool command_parseselectcolor(parser *p, void *out) {
    (void) out;
    int id;

    PARSE_CHECK(command_parseinteger(p, &id));

    mv_cmd_select_color *cmd = command_new(MVCMD_SELECT_COLOR, sizeof(mv_cmd_select_color));
    if (!cmd) {
        parse_error(p, true, ERROR_ALLOCATIONFAILED);
        return false;
    }
    cmd->id=id;
    return command_enqueue_owned(p, &cmd->cmd);
}

bool command_parsedraw(parser *p, void *out) {
    command_parsectx *ctx = (command_parsectx *) out;
    int id;

    PARSE_CHECK(command_parseinteger(p, &id));

    mv_cmd_draw *cmd = command_new(MVCMD_DRAW, sizeof(mv_cmd_draw));
    if (!cmd) {
        parse_error(p, true, ERROR_ALLOCATIONFAILED);
        return false;
    }
    cmd->id=id;
    cmd->has_matrix=ctx->modelchanged;
    if (ctx->modelchanged) {
        memcpy(cmd->matrix, ctx->model, sizeof(float)*16);
        ctx->modelchanged=false;
    }

    return command_enqueue_owned(p, &cmd->cmd);
}

bool command_parseobject(parser *p, void *out) {
    command_parsectx *ctx = (command_parsectx *) out;
    int id;

    PARSE_CHECK(command_parseinteger(p, &id));

    if (!ctx->has_scene) {
        parse_error(p, true, COMMAND_NOSCENE);
        return false;
    }

    mv_cmd_object *cmd = command_new(MVCMD_OBJECT, sizeof(mv_cmd_object));
    if (!cmd) {
        parse_error(p, true, ERROR_ALLOCATIONFAILED);
        return false;
    }
    cmd->id=id;
    ctx->has_object=true;
    return command_enqueue_owned(p, &cmd->cmd);
}

bool command_parsevertices(parser *p, void *out) {
    command_parsectx *ctx = (command_parsectx *) out;
    char *format=NULL;
    varray_float data;

    if (!ctx->has_scene || !ctx->has_object) {
        parse_error(p, true, COMMAND_NOOBJECT);
        return false;
    }

    if (parse_checktoken(p, MVTOKEN_STRING)) {
        PARSE_CHECK(command_parsestring(p, &format));
    }

    varray_floatinit(&data);
    while (command_isnumerical(p)) {
        float f;
        if (!command_parsefloat(p, &f)) {
            free(format);
            varray_floatclear(&data);
            return false;
        }
        if (!varray_floatadd(&data, &f, 1)) {
            free(format);
            varray_floatclear(&data);
            parse_error(p, true, ERROR_ALLOCATIONFAILED);
            return false;
        }
    }

    mv_cmd_vertices *cmd = command_new(MVCMD_VERTICES, sizeof(mv_cmd_vertices));
    if (!cmd) {
        free(format);
        varray_floatclear(&data);
        parse_error(p, true, ERROR_ALLOCATIONFAILED);
        return false;
    }
    cmd->format=format;
    cmd->length=data.count;

    if (data.count>0) {
        cmd->data=malloc(sizeof(float)*data.count);
        if (!cmd->data) {
            varray_floatclear(&data);
            command_free(&cmd->cmd);
            parse_error(p, true, ERROR_ALLOCATIONFAILED);
            return false;
        }
        memcpy(cmd->data, data.data, sizeof(float)*data.count);
    }
    varray_floatclear(&data);

    return command_enqueue_owned(p, &cmd->cmd);
}

bool command_parseindex(parser *p, void *out) {
    command_parsectx *ctx = (command_parsectx *) out;
    varray_int indices;

    if (!ctx->has_scene || !ctx->has_object) {
        parse_error(p, true, COMMAND_NOOBJECT);
        return false;
    }

    gelementtype etype = POINTS;
    if (p->previous.type==MVTOKEN_LINES) {
        etype=LINES;
    } else if (p->previous.type==MVTOKEN_FACETS) {
        etype=FACETS;
    }

    varray_intinit(&indices);
    while (parse_checktoken(p, MVTOKEN_INTEGER)) {
        int i;
        if (!command_parseinteger(p, &i)) {
            varray_intclear(&indices);
            return false;
        }
        if (!varray_intadd(&indices, &i, 1)) {
            varray_intclear(&indices);
            parse_error(p, true, ERROR_ALLOCATIONFAILED);
            return false;
        }
    }

    mv_cmd_element *cmd = command_new(MVCMD_ELEMENT, sizeof(mv_cmd_element));
    if (!cmd) {
        varray_intclear(&indices);
        parse_error(p, true, ERROR_ALLOCATIONFAILED);
        return false;
    }
    cmd->type=etype;
    cmd->length=indices.count;

    if (indices.count>0) {
        cmd->indices=malloc(sizeof(int)*indices.count);
        if (!cmd->indices) {
            varray_intclear(&indices);
            command_free(&cmd->cmd);
            parse_error(p, true, ERROR_ALLOCATIONFAILED);
            return false;
        }
        memcpy(cmd->indices, indices.data, sizeof(int)*indices.count);
    }
    varray_intclear(&indices);

    return command_enqueue_owned(p, &cmd->cmd);
}

bool command_parseidentity(parser *p, void *out) {
    (void) p;
    command_parsectx *ctx = (command_parsectx *) out;
    mat3d_identity4x4(ctx->model);
    ctx->modelchanged=true;
    return true;
}

bool command_parsematrix(parser *p, void *out) {
    command_parsectx *ctx = (command_parsectx *) out;
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
    command_parsectx *ctx = (command_parsectx *) out;
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
    command_parsectx *ctx = (command_parsectx *) out;
    float s;

    PARSE_CHECK(command_parsefloat(p, &s));
    mat3d_scale(ctx->model, s, ctx->model);
    ctx->modelchanged=true;
    return true;
}

bool command_parsetranslate(parser *p, void *out) {
    command_parsectx *ctx = (command_parsectx *) out;
    float x[3];

    for (int i=0; i<3; i++) {
        PARSE_CHECK(command_parsefloat(p, &x[i]));
    }

    mat3d_translate(ctx->model, x, ctx->model);
    ctx->modelchanged=true;
    return true;
}

bool command_parsescene(parser *p, void *out) {
    command_parsectx *ctx = (command_parsectx *) out;
    int id, dim;

    PARSE_CHECK(command_parseinteger(p, &id));
    PARSE_CHECK(command_parseinteger(p, &dim));

    mv_cmd_scene *cmd = command_new(MVCMD_SCENE_CREATE, sizeof(mv_cmd_scene));
    if (!cmd) {
        parse_error(p, true, ERROR_ALLOCATIONFAILED);
        return false;
    }
    cmd->id=id;
    cmd->dim=dim;
    ctx->has_scene=true;
    ctx->has_object=false;

    return command_enqueue_owned(p, &cmd->cmd);
}

/** `U S <id>` — clear and select an existing scene. */
bool command_parseupdate(parser *p, void *out) {
    command_parsectx *ctx = (command_parsectx *) out;
    int id;

    if (!parse_checktokenadvance(p, MVTOKEN_SCENE)) {
        parse_error(p, true, COMMAND_INVLDUPDATE);
        return false;
    }

    PARSE_CHECK(command_parseinteger(p, &id));

    mv_cmd_update_scene *cmd = command_new(MVCMD_UPDATE_SCENE, sizeof(mv_cmd_update_scene));
    if (!cmd) {
        parse_error(p, true, ERROR_ALLOCATIONFAILED);
        return false;
    }
    cmd->id=id;
    ctx->has_scene=true;
    ctx->has_object=false;

    return command_enqueue_owned(p, &cmd->cmd);
}

bool command_parsewindow(parser *p, void *out) {
    (void) out;
    char *name=NULL;

    PARSE_CHECK(command_parsestring(p, &name));

    mv_cmd_window *cmd = command_new(MVCMD_WINDOW_TITLE, sizeof(mv_cmd_window));
    if (!cmd) {
        free(name);
        parse_error(p, true, ERROR_ALLOCATIONFAILED);
        return false;
    }
    cmd->title=name;
    return command_enqueue_owned(p, &cmd->cmd);
}

bool command_parsefont(parser *p, void *out) {
    (void) out;
    int id;
    char *file=NULL;
    float size;

    PARSE_CHECK(command_parseinteger(p, &id));
    PARSE_CHECK(command_parsestring(p, &file));
    if (!command_parsefloat(p, &size)) {
        free(file);
        return false;
    }

    mv_cmd_font *cmd = command_new(MVCMD_FONT, sizeof(mv_cmd_font));
    if (!cmd) {
        free(file);
        parse_error(p, true, ERROR_ALLOCATIONFAILED);
        return false;
    }
    cmd->id=id;
    cmd->path=file;
    cmd->size=size;
    return command_enqueue_owned(p, &cmd->cmd);
}

bool command_parsetext(parser *p, void *out) {
    command_parsectx *ctx = (command_parsectx *) out;
    int fontid;
    char *string=NULL;

    PARSE_CHECK(command_parseinteger(p, &fontid));
    PARSE_CHECK(command_parsestring(p, &string));

    mv_cmd_text *cmd = command_new(MVCMD_TEXT, sizeof(mv_cmd_text));
    if (!cmd) {
        free(string);
        parse_error(p, true, ERROR_ALLOCATIONFAILED);
        return false;
    }
    cmd->fontid=fontid;
    cmd->string=string;
    cmd->has_matrix=ctx->modelchanged;
    if (ctx->modelchanged) {
        memcpy(cmd->matrix, ctx->model, sizeof(float)*16);
        ctx->modelchanged=false;
    }

    return command_enqueue_owned(p, &cmd->cmd);
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
    PARSERULE_PREFIX(MVTOKEN_UPDATE, command_parseupdate),
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

/** @brief Parses a command sequence into the shared queue (does not apply). */
bool command_parse(char *in) {
    command_parsectx ctx;
    command_parsectx_init(&ctx);

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
        command_queue_clear();
        return false;
    }

    mv_command *prep = command_new(MVCMD_PREPARE, sizeof(mv_command));
    if (!prep || !command_enqueue(prep)) {
        command_free(prep);
        command_queue_clear();
        return false;
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
    if (remove(in) != 0) {
        printf("Warning: failed to remove temporary file '%s'.\n", in);
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
    command_queue_init();

    morpho_defineerror(COMMAND_UNRCGNZDCMND, ERROR_PARSE, COMMAND_UNRCGNZDCMND_MSG);
    morpho_defineerror(COMMAND_INVLDNMBR, ERROR_LEX, COMMAND_INVLDNMBR_MSG);
    morpho_defineerror(COMMAND_NOSCENE, ERROR_PARSE, COMMAND_NOSCENE_MSG);
    morpho_defineerror(COMMAND_NOOBJECT, ERROR_PARSE, COMMAND_NOOBJECT_MSG);
    morpho_defineerror(COMMAND_EXPECTINTEGER, ERROR_PARSE, COMMAND_EXPECTINTEGER_MSG);
    morpho_defineerror(COMMAND_EXPECTNUMBER, ERROR_PARSE, COMMAND_EXPECTNUMBER_MSG);
    morpho_defineerror(COMMAND_EXPECTSTRING, ERROR_PARSE, COMMAND_EXPECTSTRING_MSG);
    morpho_defineerror(COMMAND_INVLDUPDATE, ERROR_PARSE, COMMAND_INVLDUPDATE_MSG);
}

void command_finalize(void) {
    command_queue_clear();
}
