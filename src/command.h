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
#include "varray.h"
#include "scene.h"

/* -------------------------------------------------------
 * Error ids
 * ------------------------------------------------------- */

#define COMMAND_UNRCGNZDCMND              "UnrcgnzdCmnd"
#define COMMAND_UNRCGNZDCMND_MSG          "Unrecognized token: '%s'."

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

#define COMMAND_INVLDUPDATE                 "InvldUpd"
#define COMMAND_INVLDUPDATE_MSG             "Unrecognized update target (expected S, O, or V)."

#define COMMAND_INVLDDELETE               "InvldDel"
#define COMMAND_INVLDDELETE_MSG           "Unrecognized delete target (expected S, O, or D)."

#define COMMAND_INVLDVERTICES             "InvldVerts"
#define COMMAND_INVLDVERTICES_MSG         "Vertex replace requires same length as existing object data."

#define COMMAND_INVLDMATERIAL             "InvldMat"
#define COMMAND_INVLDMATERIAL_MSG         "Unrecognized material (expected shaded or flat)."

#define COMMAND_INVLDCOLOR                "InvldClr"
#define COMMAND_INVLDCOLOR_MSG            "Color data length must be RGB triples or RGBA quads."

#define COMMAND_INVLDLIGHT                "InvldLght"
#define COMMAND_INVLDLIGHT_MSG            "Unrecognized light (expected \"neutral\", \"threepoint\", \"auto\", or <n> \"x\"|\"xc\" ...)."

/* -------------------------------------------------------
 * Command IR — header + typed payloads
 * ------------------------------------------------------- */

typedef enum {
    MVCMD_SCENE_CREATE,   /**< Create scene + open window (`S`) */
    MVCMD_UPDATE_SCENE,   /**< Clear + select existing scene (`U S`) */
    MVCMD_UPDATE_OBJECT,  /**< Clear one object for redefine (`U O`) */
    MVCMD_UPDATE_VERTICES,/**< Same-length vertex replace (`U V`) */
    MVCMD_CLOSE_SCENE,    /**< Close scene window (`X S`) */
    MVCMD_DELETE_OBJECT,  /**< Delete object + its draws (`X O`) */
    MVCMD_DELETE_DRAW,    /**< Delete one draw-slot (`X D`) */
    MVCMD_QUIT,           /**< Quit viewer (`Q`) */
    MVCMD_WINDOW_TITLE,   /**< Set window title (`W`) */
    MVCMD_BOUNDS,         /**< Set scene AABB (`B`) */
    MVCMD_LIGHT,          /**< Named rig or explicit world point list (`L`) */
    MVCMD_BACKGROUND,     /**< Set clear / background color (`G`) */
    MVCMD_OBJECT,         /**< Select/create current object (`o`) */
    MVCMD_VERTICES,       /**< Append vertex data (`v`) */
    MVCMD_ELEMENT,        /**< Append points/lines/facets (`p`/`l`/`f`) */
    MVCMD_COLOR,          /**< Define color table entry (`c`) */
    MVCMD_SELECT_COLOR,   /**< Select active color (`C`) */
    MVCMD_MATERIAL,       /**< Shade mode + Phong coeffs (`M`) */
    MVCMD_DRAW,           /**< Draw object, optional matrix (`d`) */
    MVCMD_CLEAR_DISPLAY,  /**< Clear displaylist only (`D`); keep objects/colors/fonts */
    MVCMD_FONT,           /**< Load font (`F`) */
    MVCMD_TEXT,           /**< Add/draw text, optional matrix (`T`) */
    MVCMD_PREPARE         /**< Upload scene to GL (header only; no typed payload) */
} mv_command_type;

/** Common header; typed commands embed this as their first field. */
typedef struct {
    mv_command_type type;
} mv_command;

/** Create a scene and open its display window.
 *  Language: `S <id> <dim>`
 *  @param id   Scene identifier
 *  @param dim  Dimension (2 or 3) */
typedef struct {
    mv_command cmd;
    int id;
    int dim;
} mv_cmd_scene;

/** Clear an existing scene in place and select it as current (keep window).
 *  Language: `U S <id>`
 *  @param id  Scene identifier (must already exist) */
typedef struct {
    mv_command cmd;
    int id;
} mv_cmd_update_scene;

/** Clear one object's geometry for redefine; select it as current.
 *  Language: `U O <id>`
 *  @param id  Object identifier */
typedef struct {
    mv_command cmd;
    int id;
} mv_cmd_update_object;

/** Same-length vertex replace for an object.
 *  Language: `U V <id> ["format"] <floats...>`
 *  @param id      Object identifier
 *  @param format  Optional format string (transferred on apply; may be NULL)
 *  @param data    Owned float blob (freed after apply)
 *  @param length  Number of floats (must match existing vertexdata.length) */
typedef struct {
    mv_command cmd;
    int id;
    char *format;
    float *data;
    int length;
} mv_cmd_update_vertices;

/** Close the window for an existing scene.
 *  Language: `X S <id>`
 *  @param id  Scene identifier (must already exist) */
typedef struct {
    mv_command cmd;
    int id;
} mv_cmd_close_scene;

/** Delete one object and all OBJECT draws that reference it.
 *  Language: `X O <id>`
 *  @param id  Object identifier */
typedef struct {
    mv_command cmd;
    int id;
} mv_cmd_delete_object;

/** Delete one draw-slot by draw id; leave the object.
 *  Language: `X D <drawId>`
 *  @param id  Draw-slot identifier */
typedef struct {
    mv_command cmd;
    int id;
} mv_cmd_delete_draw;

/** Set the current display window title.
 *  Language: `W "<title>"`
 *  @param title  Owned UTF-8 string (freed after apply) */
typedef struct {
    mv_command cmd;
    char *title;
} mv_cmd_window;

/** Set an explicit scene axis-aligned bounding box.
 *  Language: `B <xmin> <xmax> <ymin> <ymax> <zmin> <zmax>`
 *  @param bbox  xmin,xmax,ymin,ymax,zmin,zmax */
typedef struct {
    mv_command cmd;
    float bbox[6];
} mv_cmd_bounds;

/** Set scene lighting: a named camera-relative rig, or n world-space point lights.
 *  Language: `L "neutral"|"threepoint"|"auto"` | `L <n> "x"|"xc" ...` | `L 0`
 *  @param mode     Neutral / ThreePoint / Explicit
 *  @param nlights  Explicit count (0 = ambient only); ignored for named rigs
 *  @param pos      World-space xyz + w (1 = point) per light
 *  @param color    RGB per light */
typedef struct {
    mv_command cmd;
    scene_light_mode mode;
    int nlights;
    float pos[SCENE_MAX_LIGHTS][4];
    float color[SCENE_MAX_LIGHTS][3];
} mv_cmd_light;

/** Set the scene clear / background color.
 *  Language: `G <r> <g> <b>`
 *  @param rgb  Clear color RGB */
typedef struct {
    mv_command cmd;
    float rgb[3];
} mv_cmd_background;

/** Select/create the current geometry object in the active scene.
 *  Language: `o <id>`
 *  @param id  Object identifier */
typedef struct {
    mv_command cmd;
    int id;
} mv_cmd_object;

/** Append vertex (and optional attribute) data to the current object.
 *  Language: `v ["format"] <floats...>`
 *  @param format  Optional vertex format string; transferred to the object on apply (may be NULL)
 *  @param data    Owned float blob (copied into scene data, then freed)
 *  @param length  Number of floats in @p data */
typedef struct {
    mv_command cmd;
    char *format;
    float *data;
    int length;
} mv_cmd_vertices;

/** Append an indexed element (points, lines, or facets) to the current object.
 *  Language: `p|l|f <indices...>`
 *  @param type     POINTS, LINES, or FACETS
 *  @param indices  Owned index blob (copied into scene, then freed)
 *  @param length   Number of indices */
typedef struct {
    mv_command cmd;
    gelementtype type;
    int *indices;
    int length;
} mv_cmd_element;

/** Define a named color table entry from RGB or RGBA values.
 *  Language: `c <id> <r g b>` or `c <id> <r g b a>` (multi-entry: RGB triples or RGBA quads)
 *  @param id          Color identifier
 *  @param rgb         Owned float blob of length*components (r,g,b[,a] per entry)
 *  @param length      Number of color entries
 *  @param components  3 (RGB) or 4 (RGBA) */
typedef struct {
    mv_command cmd;
    int id;
    float *rgb;
    int length;
    int components;
} mv_cmd_color;

/** Select the active color for subsequent draws.
 *  Language: `C <id>` or bare `C`
 *  @param id     Color identifier previously defined with `c` (unused if clear)
 *  @param clear  Bare `C`: clear the draw-slot override (restore vertex colors) */
typedef struct {
    mv_command cmd;
    int id;
    bool clear;
} mv_cmd_select_color;

/** Set shading mode and optional Phong coefficients for subsequent draws.
 *  Language: `M flat` | `M shaded` | `M shaded <ka> <kd> [<ks> [<n>]]`
 *  @param mode       SCENE_SHADE_SHADED or SCENE_SHADE_FLAT
 *  @param ka,kd,ks,n Ambient/diffuse/specular/shininess (defaults if omitted) */
typedef struct {
    mv_command cmd;
    int mode;
    float ka;
    float kd;
    float ks;
    float shininess;
} mv_cmd_material;

/** Draw an object, optionally with a model matrix.
 *  Language: `d <drawId>` | `d <drawId> <objectId>`
 *  Matrix baked from preceding `i`/`m`/`r`/`s`/`t` at parse time.
 *  @param drawid        Draw-slot id (Graphics entry id for Morpho path)
 *  @param has_objectid  Whether @p objectid was provided (two-arg form)
 *  @param objectid      Object to draw when creating/rebinding
 *  @param has_matrix    Whether @p matrix is valid
 *  @param matrix        Column-major 4×4 model transform when @p has_matrix is true */
typedef struct {
    mv_command cmd;
    int drawid;
    bool has_objectid;
    int objectid;
    bool has_matrix;
    float matrix[16];
} mv_cmd_draw;

/** Load a font into the active scene.
 *  Language: `F <id> "<path>" <size>`
 *  @param id    Font identifier
 *  @param path  Owned filesystem path (used then freed; scene does not keep it)
 *  @param size  Point size */
typedef struct {
    mv_command cmd;
    int id;
    char *path;
    float size;
} mv_cmd_font;

/** Add a text string and queue it for drawing, optionally with a model matrix.
 *  Language: `T <fontid> "<string>"` (legacy append) or
 *  `T <drawId> <fontid> "<string>"` (text draw-slot; create or update).
 *  Matrix baked from transforms at parse time.
 *  @param drawid      Draw-slot id, or SCENE_EMPTY for legacy append-only
 *  @param fontid      Font identifier previously loaded with `F`
 *  @param string      Owned text; transferred into the scene on apply
 *  @param has_matrix  Whether @p matrix is valid
 *  @param matrix      Column-major 4×4 model transform when @p has_matrix is true */
typedef struct {
    mv_command cmd;
    int drawid;
    int fontid;
    char *string;
    bool has_matrix;
    float matrix[16];
} mv_cmd_text;

#define MVCMD_AS_SCENE(c)            ((mv_cmd_scene *) (c))
#define MVCMD_AS_UPDATE_SCENE(c)     ((mv_cmd_update_scene *) (c))
#define MVCMD_AS_UPDATE_OBJECT(c)    ((mv_cmd_update_object *) (c))
#define MVCMD_AS_UPDATE_VERTICES(c)  ((mv_cmd_update_vertices *) (c))
#define MVCMD_AS_CLOSE_SCENE(c)      ((mv_cmd_close_scene *) (c))
#define MVCMD_AS_DELETE_OBJECT(c)    ((mv_cmd_delete_object *) (c))
#define MVCMD_AS_DELETE_DRAW(c)      ((mv_cmd_delete_draw *) (c))
#define MVCMD_AS_WINDOW(c)           ((mv_cmd_window *) (c))
#define MVCMD_AS_BOUNDS(c)           ((mv_cmd_bounds *) (c))
#define MVCMD_AS_LIGHT(c)            ((mv_cmd_light *) (c))
#define MVCMD_AS_BACKGROUND(c)       ((mv_cmd_background *) (c))
#define MVCMD_AS_OBJECT(c)           ((mv_cmd_object *) (c))
#define MVCMD_AS_VERTICES(c)         ((mv_cmd_vertices *) (c))
#define MVCMD_AS_ELEMENT(c)          ((mv_cmd_element *) (c))
#define MVCMD_AS_COLOR(c)            ((mv_cmd_color *) (c))
#define MVCMD_AS_SELECT_COLOR(c)     ((mv_cmd_select_color *) (c))
#define MVCMD_AS_MATERIAL(c)         ((mv_cmd_material *) (c))
#define MVCMD_AS_DRAW(c)             ((mv_cmd_draw *) (c))
#define MVCMD_AS_FONT(c)             ((mv_cmd_font *) (c))
#define MVCMD_AS_TEXT(c)             ((mv_cmd_text *) (c))

DECLARE_VARRAY(mv_commandptr, mv_command *);

/** Free owned payloads and the command itself. */
void command_free(mv_command *cmd);

/* -------------------------------------------------------
 * Command queue
 * ------------------------------------------------------- */

/* Queue is guarded by a mutex for the I/O thread; command_wake posts glfwPostEmptyEvent. */

void command_queue_init(void);
void command_queue_clear(void);
bool command_enqueue(mv_command *cmd); /* takes ownership of cmd */
void command_wake(void);
int command_process(void);

/* -------------------------------------------------------
 * Prototypes
 * ------------------------------------------------------- */

bool command_getfilesize(FILE *f, size_t *s);
bool command_loadinput(const char *in, char **out);
void command_removefile(const char *in);

/** Format a user-reportable parse error into @p out (no protocol prefix).
 *  Caller inits/clears @p out; result is null-terminated in out->data. */
void command_formaterror(const error *err, varray_char *out);

/** Parse ASCII into the shared queue. Fills @p err on failure; does not print. */
bool command_parse(char *in, error *err);

void command_initialize(void);
void command_finalize(void);

/** Clear sticky apply context if it still points at @p s (e.g. Escape teardown). */
void command_invalidate_scene(scene *s);

#endif /* command_h */
