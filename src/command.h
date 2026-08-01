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
#include "scene.h"

/* -------------------------------------------------------
 * Error ids
 * ------------------------------------------------------- */

#define COMMAND_UNRCGNZDCMND              "UnrcgnzdCmnd"
#define COMMAND_UNRCGNZDCMND_MSG          "Unrecognized morphoview command."

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
#define COMMAND_INVLDUPDATE_MSG             "Unrecognized update target (expected S)."

#define COMMAND_INVLDDELETE               "InvldDel"
#define COMMAND_INVLDDELETE_MSG           "Unrecognized delete target (expected S)."

#define COMMAND_INVLDMATERIAL             "InvldMat"
#define COMMAND_INVLDMATERIAL_MSG         "Unrecognized material (expected shaded or flat)."

#define COMMAND_INVLDCOLOR                "InvldClr"
#define COMMAND_INVLDCOLOR_MSG            "Color data length must be RGB triples or RGBA quads."

#define COMMAND_INVLDLIGHT                "InvldLght"
#define COMMAND_INVLDLIGHT_MSG            "Unrecognized light (expected a or x y z [r g b])."

/* -------------------------------------------------------
 * Command IR — header + typed payloads
 * ------------------------------------------------------- */

typedef enum {
    MVCMD_SCENE_CREATE,   /**< Create scene + open window (`S`) */
    MVCMD_UPDATE_SCENE,   /**< Clear + select existing scene (`U S`) */
    MVCMD_CLOSE_SCENE,    /**< Close scene window (`X S`) */
    MVCMD_QUIT,           /**< Quit viewer (`Q`) */
    MVCMD_WINDOW_TITLE,   /**< Set window title (`W`) */
    MVCMD_BOUNDS,         /**< Set scene AABB (`B`) */
    MVCMD_LIGHT,          /**< Explicit light or auto (`L`) */
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

/** Close the window for an existing scene.
 *  Language: `X S <id>`
 *  @param id  Scene identifier (must already exist) */
typedef struct {
    mv_command cmd;
    int id;
} mv_cmd_close_scene;

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

/** Set an explicit model-space light, or clear it for AABB auto placement.
 *  Language: `L <x> <y> <z> [r g b]` | `L a`
 *  @param auto_mode  If true, clear explicit light
 *  @param has_color  If true (and not auto), also set light color
 *  @param pos        Model-space light position
 *  @param color      Light RGB when @p has_color is true */
typedef struct {
    mv_command cmd;
    bool auto_mode;
    bool has_color;
    float pos[3];
    float color[3];
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
 *  Language: `C <id>`
 *  @param id  Color identifier previously defined with `c` */
typedef struct {
    mv_command cmd;
    int id;
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
 *  Language: `d <id>` (matrix baked from preceding `i`/`m`/`r`/`s`/`t` at parse time)
 *  @param id          Object identifier
 *  @param has_matrix  Whether @p matrix is valid
 *  @param matrix      Column-major 4×4 model transform when @p has_matrix is true */
typedef struct {
    mv_command cmd;
    int id;
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
 *  Language: `T <fontid> "<string>"` (matrix baked from transforms at parse time)
 *  @param fontid      Font identifier previously loaded with `F`
 *  @param string      Owned text; transferred into the scene on apply
 *  @param has_matrix  Whether @p matrix is valid
 *  @param matrix      Column-major 4×4 model transform when @p has_matrix is true */
typedef struct {
    mv_command cmd;
    int fontid;
    char *string;
    bool has_matrix;
    float matrix[16];
} mv_cmd_text;

#define MVCMD_AS_SCENE(c)         ((mv_cmd_scene *) (c))
#define MVCMD_AS_UPDATE_SCENE(c)  ((mv_cmd_update_scene *) (c))
#define MVCMD_AS_CLOSE_SCENE(c)   ((mv_cmd_close_scene *) (c))
#define MVCMD_AS_WINDOW(c)        ((mv_cmd_window *) (c))
#define MVCMD_AS_BOUNDS(c)        ((mv_cmd_bounds *) (c))
#define MVCMD_AS_LIGHT(c)         ((mv_cmd_light *) (c))
#define MVCMD_AS_BACKGROUND(c)    ((mv_cmd_background *) (c))
#define MVCMD_AS_OBJECT(c)        ((mv_cmd_object *) (c))
#define MVCMD_AS_VERTICES(c)      ((mv_cmd_vertices *) (c))
#define MVCMD_AS_ELEMENT(c)       ((mv_cmd_element *) (c))
#define MVCMD_AS_COLOR(c)         ((mv_cmd_color *) (c))
#define MVCMD_AS_SELECT_COLOR(c)  ((mv_cmd_select_color *) (c))
#define MVCMD_AS_MATERIAL(c)      ((mv_cmd_material *) (c))
#define MVCMD_AS_DRAW(c)          ((mv_cmd_draw *) (c))
#define MVCMD_AS_FONT(c)          ((mv_cmd_font *) (c))
#define MVCMD_AS_TEXT(c)          ((mv_cmd_text *) (c))

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

bool command_parse(char *in);

void command_initialize(void);
void command_finalize(void);

#endif /* command_h */
