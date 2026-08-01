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
#include "listener.h"

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

/** Sticky apply context across command_process batches (follow-up chunks may omit `S`). */
static command_applyctx g_applyctx;
static bool g_applyctx_ready=false;

static command_applyctx *command_sticky_applyctx(void) {
    if (!g_applyctx_ready) {
        command_applyctx_init(&g_applyctx);
        g_applyctx_ready=true;
    }
    return &g_applyctx;
}

static void command_sticky_applyctx_reset(void) {
    command_applyctx_init(&g_applyctx);
    g_applyctx_ready=false;
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

/* **********************************************************************
 * Apply
 * ********************************************************************** */

/** Scene content / bounds changed — needs GL upload or camera fit. */
static void command_touchscene(command_applyctx *ctx) {
    if (ctx->scene) scene_markchanged(ctx->scene);
}

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
            scene_markchanged(s);

            ctx->display = display_findforscene(s);
            if (ctx->display && ctx->display->window) {
                glfwMakeContextCurrent(ctx->display->window);
                render_reset(&ctx->display->render);
            }

            ctx->scene = s;
            ctx->cobject = NULL;
            return true;
        }

        case MVCMD_CLOSE_SCENE: {
            mv_cmd_close_scene *c = MVCMD_AS_CLOSE_SCENE(cmd);
            scene *s = scene_find(c->id);
            if (!s) {
                fprintf(stderr, "morphoview: No scene with id '%i'.\n", c->id);
                return false;
            }

            display *d = display_findforscene(s);
            display_requestclose(d);

            if (ctx->scene == s) {
                ctx->scene = NULL;
                ctx->display = NULL;
                ctx->cobject = NULL;
            }
            return true;
        }

        case MVCMD_QUIT:
            display_requestcloseall();
            if (!display_anyopen() && listener_isactive()) {
                listener_reply(LISTENER_WINDOW_CLOSED);
                listener_stop();
            }
            command_sticky_applyctx_reset();
            return true;

        case MVCMD_CLEAR_DISPLAY: {
            /* Apply-time scene only — parse must not require has_scene (ok-before-apply). */
            if (!ctx->scene) {
                fprintf(stderr, "morphoview: No current scene for D.\n");
                return false;
            }
            scene_cleardisplaylist(ctx->scene);
            scene_markchanged(ctx->scene);
            if (ctx->display && ctx->display->window) {
                glfwMakeContextCurrent(ctx->display->window);
                render_reset(&ctx->display->render);
            }
            return true;
        }

        case MVCMD_WINDOW_TITLE: {
            mv_cmd_window *c = MVCMD_AS_WINDOW(cmd);
            if (ctx->display && c->title) {
                display_setwindowtitle(ctx->display, c->title);
            }
            return true;
        }

        case MVCMD_BOUNDS: {
            mv_cmd_bounds *c = MVCMD_AS_BOUNDS(cmd);
            if (!ctx->scene) return false;
            scene_setbbox(ctx->scene, c->bbox[0], c->bbox[1], c->bbox[2],
                          c->bbox[3], c->bbox[4], c->bbox[5]);
            command_touchscene(ctx);
            return true;
        }

        case MVCMD_LIGHT: {
            /* Lighting is sampled each frame from the scene; no GL rebuild. */
            mv_cmd_light *c = MVCMD_AS_LIGHT(cmd);
            if (!ctx->scene) return false;
            if (c->auto_mode) {
                scene_clearlight(ctx->scene);
            } else if (c->has_color) {
                scene_setlight(ctx->scene, c->pos[0], c->pos[1], c->pos[2],
                               c->color[0], c->color[1], c->color[2]);
            } else {
                scene_setlightpos(ctx->scene, c->pos[0], c->pos[1], c->pos[2]);
            }
            return true;
        }

        case MVCMD_BACKGROUND: {
            /* Background is sampled each frame from the scene; no GL rebuild. */
            mv_cmd_background *c = MVCMD_AS_BACKGROUND(cmd);
            if (!ctx->scene) return false;
            scene_setbackground(ctx->scene, c->rgb[0], c->rgb[1], c->rgb[2]);
            return true;
        }

        case MVCMD_OBJECT:
            if (!ctx->scene) return false;
            ctx->cobject=scene_addobject(ctx->scene, MVCMD_AS_OBJECT(cmd)->id);
            if (!ctx->cobject) return false;
            command_touchscene(ctx);
            return true;

        case MVCMD_VERTICES: {
            mv_cmd_vertices *c = MVCMD_AS_VERTICES(cmd);
            if (!ctx->scene || !ctx->cobject) return false;

            if (c->format) {
                if (ctx->cobject->vertexdata.format)
                    free(ctx->cobject->vertexdata.format);
                ctx->cobject->vertexdata.format=c->format;
                c->format=NULL; /* transferred */
            }

            if (c->length>0 && c->data) {
                int ret=scene_adddata_take(ctx->scene, &c->data, c->length);
                if (ret<0) return false;
                if (ctx->cobject->vertexdata.indx==SCENE_EMPTY) {
                    ctx->cobject->vertexdata.indx=ret;
                    ctx->cobject->vertexdata.length=0;
                }
                ctx->cobject->vertexdata.length += c->length;
            }
            command_touchscene(ctx);
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
                int ret=scene_addindex_take(ctx->scene, &c->indices, c->length);
                if (ret<0) return false;
                el.indx=ret;
                el.length=c->length;
            }

            scene_addelement(ctx->cobject, &el);
            command_touchscene(ctx);
            return true;
        }

        case MVCMD_COLOR: {
            mv_cmd_color *c = MVCMD_AS_COLOR(cmd);
            if (!ctx->scene) return false;

            if (c->length>0 && c->rgb) {
                int ncomp = (c->components==4) ? 4 : 3;
                int nfloats = c->length*ncomp;
                int indx=scene_adddata_take(ctx->scene, &c->rgb, nfloats);
                if (indx<0) return false;
                scene_addcolor(ctx->scene, c->id, c->length, ncomp, indx);
            }
            command_touchscene(ctx);
            return true;
        }

        case MVCMD_SELECT_COLOR:
            if (!ctx->scene) return false;
            scene_adddraw(ctx->scene, COLOR, MVCMD_AS_SELECT_COLOR(cmd)->id, -1);
            command_touchscene(ctx);
            return true;

        case MVCMD_MATERIAL: {
            mv_cmd_material *c = MVCMD_AS_MATERIAL(cmd);
            if (!ctx->scene) return false;
            float coeffs[4] = { c->ka, c->kd, c->ks, c->shininess };
            int matindx=scene_adddata(ctx->scene, coeffs, 4);
            scene_adddraw(ctx->scene, SHADE, c->mode, matindx);
            command_touchscene(ctx);
            return true;
        }

        case MVCMD_DRAW: {
            mv_cmd_draw *c = MVCMD_AS_DRAW(cmd);
            if (!ctx->scene) return false;
            /* Same object id already drawn: replace its matrix in place (pose update). */
            if (scene_setobjectdrawmatrix(ctx->scene, c->id,
                                          c->has_matrix ? c->matrix : NULL)) {
                command_touchscene(ctx);
                return true;
            }
            int matindx = SCENE_EMPTY;
            if (c->has_matrix) {
                matindx=scene_adddata(ctx->scene, c->matrix, 16);
            }
            scene_adddraw(ctx->scene, OBJECT, c->id, matindx);
            command_touchscene(ctx);
            return true;
        }

        case MVCMD_FONT: {
            mv_cmd_font *c = MVCMD_AS_FONT(cmd);
            if (!ctx->scene) return false;
            if (!scene_addfont(ctx->scene, c->id, c->path, c->size, NULL)) return false;
            command_touchscene(ctx);
            return true;
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
            command_touchscene(ctx);
            return true;
        }

        case MVCMD_PREPARE:
            display_prepareall();
            return true;
    }

    return false;
}

int command_process(void) {
    command_applyctx *ctx = command_sticky_applyctx();

    /* Steal the queue under the lock so apply (GL) does not block the I/O thread. */
    varray_mv_commandptr batch;
    pthread_mutex_lock(&command_queue_mutex);
    batch = command_queue;
    varray_mv_commandptrinit(&command_queue);
    pthread_mutex_unlock(&command_queue_mutex);

    int applied=0;
    for (unsigned int i=0; i<batch.count; i++) {
        mv_command *cmd = batch.data[i];
        if (!command_apply(cmd, ctx)) {
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
    MVTOKEN_CLEAR_DISPLAY,
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
    MVTOKEN_DELETE,
    MVTOKEN_QUIT,
    MVTOKEN_TRANSLATE,
    MVTOKEN_WINDOW,
    MVTOKEN_BOUNDS,
    MVTOKEN_LIGHT,
    MVTOKEN_BACKGROUND,
    MVTOKEN_AUTO,
    MVTOKEN_FONT,
    MVTOKEN_TEXT,
    MVTOKEN_MATERIAL,
    MVTOKEN_SHADED,
    MVTOKEN_FLAT,

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
    { "D",          MVTOKEN_CLEAR_DISPLAY         , NULL },
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
    { "X",          MVTOKEN_DELETE                , NULL },
    { "Q",          MVTOKEN_QUIT                  , NULL },
    { "t",          MVTOKEN_TRANSLATE             , NULL },
    { "T",          MVTOKEN_TEXT                  , NULL },
    { "v",          MVTOKEN_VERTICES              , NULL },
    { "W",          MVTOKEN_WINDOW                , NULL },
    { "B",          MVTOKEN_BOUNDS                , NULL },
    { "L",          MVTOKEN_LIGHT                 , NULL },
    { "G",          MVTOKEN_BACKGROUND            , NULL },
    { "a",          MVTOKEN_AUTO                  , NULL },
    { "M",          MVTOKEN_MATERIAL              , NULL },
    { "shaded",     MVTOKEN_SHADED                , NULL },
    { "flat",       MVTOKEN_FLAT                  , NULL },

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
 * Bulk number parse (count → one alloc → fill)
 * ********************************************************************** */

static const char *command_skipws(const char *s) {
    while (*s==' ' || *s=='\t' || *s=='\r' || *s=='\n') s++;
    return s;
}

/** Advance *sp past one number matching the morphoview lexer; return false if none. */
static bool command_scannumber(const char **sp, bool integers_only) {
    const char *p = *sp;

    if (*p=='-') {
        if (!isdigit((unsigned char) p[1])) return false;
        p++;
    }
    if (!isdigit((unsigned char) *p)) return false;
    while (isdigit((unsigned char) *p)) p++;

    bool isfloat = false;
    if (*p=='.') {
        char next = p[1];
        if (isdigit((unsigned char) next) || next==' ' || next=='\t' ||
            next=='\r' || next=='\n' || next=='\0') {
            isfloat = true;
            p++;
            while (isdigit((unsigned char) *p)) p++;
        }
    }

    if (*p=='e' || *p=='E') {
        const char *e = p + 1;
        if (*e=='+' || *e=='-') e++;
        if (!isdigit((unsigned char) *e)) {
            /* Incomplete exponent — not a valid number token. */
            return false;
        }
        isfloat = true;
        p = e;
        while (isdigit((unsigned char) *p)) p++;
    }

    if (integers_only && isfloat) return false;

    *sp = p;
    return true;
}

/** Count consecutive number tokens starting at the current (unconsumed) token. */
static unsigned int command_countnumbersahead(parser *p, bool integers_only) {
    if (integers_only) {
        if (!parse_checktoken(p, MVTOKEN_INTEGER)) return 0;
    } else if (!command_isnumerical(p)) {
        return 0;
    }

    const char *s = p->current.start;
    unsigned int n = 0;
    for (;;) {
        s = command_skipws(s);
        if (!command_scannumber(&s, integers_only)) break;
        n++;
    }
    return n;
}

/** Parse n floats into a pre-sized buffer (stops early if fewer numbers remain). */
static bool command_parsefloatsinto(parser *p, float *out, unsigned int n, unsigned int *written) {
    unsigned int i = 0;
    while (i<n && command_isnumerical(p)) {
        if (!command_parsefloat(p, &out[i])) return false;
        i++;
    }
    *written = i;
    return true;
}

/** Parse n integers into a pre-sized buffer. */
static bool command_parseintsinto(parser *p, int *out, unsigned int n, unsigned int *written) {
    unsigned int i = 0;
    while (i<n && parse_checktoken(p, MVTOKEN_INTEGER)) {
        if (!command_parseinteger(p, &out[i])) return false;
        i++;
    }
    *written = i;
    return true;
}

/* **********************************************************************
 * Parse handlers (emit only)
 * ********************************************************************** */

bool command_parsecolor(parser *p, void *out) {
    (void) out;
    int id;

    PARSE_CHECK(command_parseinteger(p, &id));

    unsigned int n = command_countnumbersahead(p, false);
    float *rgb = NULL;
    unsigned int written = 0;

    if (n>0) {
        rgb = malloc(sizeof(float)*n);
        if (!rgb) {
            parse_error(p, true, ERROR_ALLOCATIONFAILED);
            return false;
        }
        if (!command_parsefloatsinto(p, rgb, n, &written)) {
            free(rgb);
            return false;
        }
    }

    int components=3;
    int length=0;
    if (written==0) {
        components=3;
        length=0;
    } else if (written==4) {
        components=4;
        length=1;
    } else if (written%3==0) {
        components=3;
        length=(int) (written/3);
    } else if (written%4==0) {
        components=4;
        length=(int) (written/4);
    } else {
        free(rgb);
        parse_error(p, false, COMMAND_INVLDCOLOR);
        return false;
    }

    mv_cmd_color *cmd = command_new(MVCMD_COLOR, sizeof(mv_cmd_color));
    if (!cmd) {
        free(rgb);
        parse_error(p, true, ERROR_ALLOCATIONFAILED);
        return false;
    }
    cmd->id=id;
    cmd->length=length;
    cmd->components=components;
    cmd->rgb=rgb;

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

bool command_parsematerial(parser *p, void *out) {
    (void) out;

    int shademode;
    if (parse_checktokenadvance(p, MVTOKEN_SHADED)) {
        shademode=SCENE_SHADE_SHADED;
    } else if (parse_checktokenadvance(p, MVTOKEN_FLAT)) {
        shademode=SCENE_SHADE_FLAT;
    } else {
        parse_error(p, false, COMMAND_INVLDMATERIAL);
        return false;
    }

    float ka=SCENE_MATERIAL_KA_DEFAULT;
    float kd=SCENE_MATERIAL_KD_DEFAULT;
    float ks=SCENE_MATERIAL_KS_DEFAULT;
    float shininess=SCENE_MATERIAL_SHININESS_DEFAULT;

    if (shademode==SCENE_SHADE_SHADED && command_isnumerical(p)) {
        PARSE_CHECK(command_parsefloat(p, &ka));
        if (!command_isnumerical(p)) {
            parse_error(p, false, COMMAND_EXPECTNUMBER);
            return false;
        }
        PARSE_CHECK(command_parsefloat(p, &kd));
        if (command_isnumerical(p)) {
            PARSE_CHECK(command_parsefloat(p, &ks));
            if (command_isnumerical(p)) {
                PARSE_CHECK(command_parsefloat(p, &shininess));
            }
        }
    }

    mv_cmd_material *cmd = command_new(MVCMD_MATERIAL, sizeof(mv_cmd_material));
    if (!cmd) {
        parse_error(p, true, ERROR_ALLOCATIONFAILED);
        return false;
    }
    cmd->mode=shademode;
    cmd->ka=ka;
    cmd->kd=kd;
    cmd->ks=ks;
    cmd->shininess=shininess;
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

/** `D` — clear displaylist only (no parse-time has_scene; sticky apply supplies scene). */
bool command_parsecleardisplay(parser *p, void *out) {
    (void) p;
    (void) out;

    mv_command *cmd = command_new(MVCMD_CLEAR_DISPLAY, sizeof(mv_command));
    if (!cmd) {
        parse_error(p, true, ERROR_ALLOCATIONFAILED);
        return false;
    }
    return command_enqueue_owned(p, cmd);
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

    if (!ctx->has_scene || !ctx->has_object) {
        parse_error(p, true, COMMAND_NOOBJECT);
        return false;
    }

    if (parse_checktoken(p, MVTOKEN_STRING)) {
        PARSE_CHECK(command_parsestring(p, &format));
    }

    unsigned int n = command_countnumbersahead(p, false);
    float *data = NULL;
    unsigned int written = 0;

    if (n>0) {
        data = malloc(sizeof(float)*n);
        if (!data) {
            free(format);
            parse_error(p, true, ERROR_ALLOCATIONFAILED);
            return false;
        }
        if (!command_parsefloatsinto(p, data, n, &written)) {
            free(format);
            free(data);
            return false;
        }
    }

    mv_cmd_vertices *cmd = command_new(MVCMD_VERTICES, sizeof(mv_cmd_vertices));
    if (!cmd) {
        free(format);
        free(data);
        parse_error(p, true, ERROR_ALLOCATIONFAILED);
        return false;
    }
    cmd->format=format;
    cmd->length=(int) written;
    cmd->data=data;

    return command_enqueue_owned(p, &cmd->cmd);
}

bool command_parseindex(parser *p, void *out) {
    command_parsectx *ctx = (command_parsectx *) out;

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

    unsigned int n = command_countnumbersahead(p, true);
    int *indices = NULL;
    unsigned int written = 0;

    if (n>0) {
        indices = malloc(sizeof(int)*n);
        if (!indices) {
            parse_error(p, true, ERROR_ALLOCATIONFAILED);
            return false;
        }
        if (!command_parseintsinto(p, indices, n, &written)) {
            free(indices);
            return false;
        }
    }

    mv_cmd_element *cmd = command_new(MVCMD_ELEMENT, sizeof(mv_cmd_element));
    if (!cmd) {
        free(indices);
        parse_error(p, true, ERROR_ALLOCATIONFAILED);
        return false;
    }
    cmd->type=etype;
    cmd->length=(int) written;
    cmd->indices=indices;

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

/** `X S <id>` — close the window for an existing scene. */
bool command_parsedelete(parser *p, void *out) {
    (void) out;
    int id;

    if (!parse_checktokenadvance(p, MVTOKEN_SCENE)) {
        parse_error(p, true, COMMAND_INVLDDELETE);
        return false;
    }

    PARSE_CHECK(command_parseinteger(p, &id));

    mv_cmd_close_scene *cmd = command_new(MVCMD_CLOSE_SCENE, sizeof(mv_cmd_close_scene));
    if (!cmd) {
        parse_error(p, true, ERROR_ALLOCATIONFAILED);
        return false;
    }
    cmd->id=id;

    return command_enqueue_owned(p, &cmd->cmd);
}

/** `Q` — quit the viewer. */
bool command_parsequit(parser *p, void *out) {
    (void) p;
    (void) out;

    mv_command *cmd = command_new(MVCMD_QUIT, sizeof(mv_command));
    if (!cmd) {
        parse_error(p, true, ERROR_ALLOCATIONFAILED);
        return false;
    }

    return command_enqueue_owned(p, cmd);
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

/** `B <xmin> <xmax> <ymin> <ymax> <zmin> <zmax>` — explicit scene AABB. */
bool command_parsebounds(parser *p, void *out) {
    command_parsectx *ctx = (command_parsectx *) out;
    float bbox[6];

    for (int i=0; i<6; i++) {
        PARSE_CHECK(command_parsefloat(p, &bbox[i]));
    }

    if (!ctx->has_scene) {
        parse_error(p, true, COMMAND_NOSCENE);
        return false;
    }

    mv_cmd_bounds *cmd = command_new(MVCMD_BOUNDS, sizeof(mv_cmd_bounds));
    if (!cmd) {
        parse_error(p, true, ERROR_ALLOCATIONFAILED);
        return false;
    }
    for (int i=0; i<6; i++) cmd->bbox[i]=bbox[i];
    return command_enqueue_owned(p, &cmd->cmd);
}

/** `L <x> <y> <z> [r g b]` | `L a` — explicit light or AABB auto. */
bool command_parselight(parser *p, void *out) {
    command_parsectx *ctx = (command_parsectx *) out;

    bool auto_mode=false;
    bool has_color=false;
    float pos[3]={0.0f, 0.0f, 0.0f};
    float color[3]={1.0f, 1.0f, 1.0f};

    if (parse_checktokenadvance(p, MVTOKEN_AUTO)) {
        auto_mode=true;
    } else if (command_isnumerical(p)) {
        for (int i=0; i<3; i++) {
            PARSE_CHECK(command_parsefloat(p, &pos[i]));
        }
        if (command_isnumerical(p)) {
            for (int i=0; i<3; i++) {
                PARSE_CHECK(command_parsefloat(p, &color[i]));
            }
            has_color=true;
        }
    } else {
        parse_error(p, false, COMMAND_INVLDLIGHT);
        return false;
    }

    if (!ctx->has_scene) {
        parse_error(p, true, COMMAND_NOSCENE);
        return false;
    }

    mv_cmd_light *cmd = command_new(MVCMD_LIGHT, sizeof(mv_cmd_light));
    if (!cmd) {
        parse_error(p, true, ERROR_ALLOCATIONFAILED);
        return false;
    }
    cmd->auto_mode=auto_mode;
    cmd->has_color=has_color;
    for (int i=0; i<3; i++) {
        cmd->pos[i]=pos[i];
        cmd->color[i]=color[i];
    }
    return command_enqueue_owned(p, &cmd->cmd);
}

/** `G <r> <g> <b>` — scene clear / background color. */
bool command_parsebackground(parser *p, void *out) {
    command_parsectx *ctx = (command_parsectx *) out;
    float rgb[3];

    for (int i=0; i<3; i++) {
        PARSE_CHECK(command_parsefloat(p, &rgb[i]));
    }

    if (!ctx->has_scene) {
        parse_error(p, true, COMMAND_NOSCENE);
        return false;
    }

    mv_cmd_background *cmd = command_new(MVCMD_BACKGROUND, sizeof(mv_cmd_background));
    if (!cmd) {
        parse_error(p, true, ERROR_ALLOCATIONFAILED);
        return false;
    }
    for (int i=0; i<3; i++) cmd->rgb[i]=rgb[i];
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
    PARSERULE_PREFIX(MVTOKEN_CLEAR_DISPLAY, command_parsecleardisplay),
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
    PARSERULE_PREFIX(MVTOKEN_DELETE, command_parsedelete),
    PARSERULE_PREFIX(MVTOKEN_QUIT, command_parsequit),
    PARSERULE_PREFIX(MVTOKEN_TRANSLATE, command_parsetranslate),
    PARSERULE_PREFIX(MVTOKEN_WINDOW, command_parsewindow),
    PARSERULE_PREFIX(MVTOKEN_BOUNDS, command_parsebounds),
    PARSERULE_PREFIX(MVTOKEN_LIGHT, command_parselight),
    PARSERULE_PREFIX(MVTOKEN_BACKGROUND, command_parsebackground),
    PARSERULE_PREFIX(MVTOKEN_FONT, command_parsefont),
    PARSERULE_PREFIX(MVTOKEN_TEXT, command_parsetext),
    PARSERULE_PREFIX(MVTOKEN_MATERIAL, command_parsematerial),
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
    morpho_defineerror(COMMAND_INVLDDELETE, ERROR_PARSE, COMMAND_INVLDDELETE_MSG);
    morpho_defineerror(COMMAND_INVLDMATERIAL, ERROR_PARSE, COMMAND_INVLDMATERIAL_MSG);
    morpho_defineerror(COMMAND_INVLDCOLOR, ERROR_PARSE, COMMAND_INVLDCOLOR_MSG);
    morpho_defineerror(COMMAND_INVLDLIGHT, ERROR_PARSE, COMMAND_INVLDLIGHT_MSG);
}

void command_finalize(void) {
    command_queue_clear();
    command_sticky_applyctx_reset();
}
