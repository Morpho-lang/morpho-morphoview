/** @file scene.h
 *  @author T J Atherton
 *
 *  @brief Scene descriptions
 */

#ifndef scene_h
#define scene_h

#include <stdio.h>
#include <stdbool.h>
#include "varray.h"
#include "text.h"

#define SCENE_EMPTY -1
DECLARE_VARRAY(float, float);

/* **********************
 * An element of a scene
 * ********************** */

typedef enum {
    POINTS,
    LINES,
    FACETS
} gelementtype;

typedef struct {
    gelementtype type; 
    int indx;
    int length; 
} gelement;

DECLARE_VARRAY(gelement, gelement);

/* **********************
 * Objects
 * ********************** */

typedef struct {
    int id;
    struct {
        char *format;
        int indx;
        int length;
    } vertexdata;
    varray_gelement elements;
    float centroid[3];   /**< Local-space AABB center of positions */
    bool centroid_valid; /**< False until computed; cleared on geometry change */
} gobject;

DECLARE_VARRAY(gobject, gobject);

/* **********************
 * Colors
 * ********************** */

typedef struct {
    int colorid;
    int indx;
    int length;      /**< Number of color entries */
    int components;  /**< 3 (RGB) or 4 (RGBA) */
} gcolor;

DECLARE_VARRAY(gcolor, gcolor);

/* **********************
 * Fonts
 * ********************** */

typedef struct {
    int id;
    textfont font;
} gfont;

DECLARE_VARRAY(gfont, gfont);

/* **********************
 * Text
 * ********************** */

typedef struct {
    int fontid;
    char *text;
} gtext;

DECLARE_VARRAY(gtext, gtext);

/* **********************
 * List of things to draw
 * ********************** */

typedef enum {
    OBJECT,
    TEXT,
    COLOR,
    SHADE
} gdrawtype;

/** Shade modes for SHADE draw entries (`M shaded|flat`). */
#define SCENE_SHADE_SHADED 0
#define SCENE_SHADE_FLAT   1

/** Default OpenGL/VTK Phong coeffs: Lambert (ks=0) for scientific viz. */
#define SCENE_MATERIAL_KA_DEFAULT  0.5f
#define SCENE_MATERIAL_KD_DEFAULT  0.5f
#define SCENE_MATERIAL_KS_DEFAULT  0.0f
#define SCENE_MATERIAL_SHININESS_DEFAULT 32.0f

/** Default clear color (dark bluish gray) when no `G` command is given. */
#define SCENE_BACKGROUND_R_DEFAULT  0.160784f
#define SCENE_BACKGROUND_G_DEFAULT  0.164706f
#define SCENE_BACKGROUND_B_DEFAULT  0.188235f

typedef struct {
    gdrawtype type;
    int id;       /**< Object/color/text id, or SCENE_SHADE_* for SHADE */
    int drawid;   /**< OBJECT/TEXT: draw-slot id (Graphics entry id); else unused */
    int colorid;  /**< OBJECT/TEXT: stamped uniform color id, or SCENE_EMPTY */
    int matindx;  /**< Model matrix, or material coeffs (ka kd ks n) for SHADE */
} gdraw;

DECLARE_VARRAY(gdraw, gdraw);

#define SCENE_MAX_LIGHTS 4

/** Named camera-relative rigs, or an explicit world-space point list. */
typedef enum {
    SCENE_LIGHT_NEUTRAL=0,    /**< Default: 3 view-space directionals + white ambient */
    SCENE_LIGHT_THREEPOINT,   /**< Key / fill / rim, view-space */
    SCENE_LIGHT_EXPLICIT      /**< nlights world-space point lights (nlights==0: ambient only) */
} scene_light_mode;

/* ***************************
 * The overall scene structure
 * *************************** */

typedef struct sscene {
    struct sscene *next; /** Linked list */
    
    int id; /** The scene ID */
    int dim; /** Number of dimensions; 2 or 3 */

    float bbox[6]; /** xmin,xmax,ymin,ymax,zmin,zmax */
    bool bbox_valid;
    bool bbox_explicit;
    bool bbox_fit_pending; /** set by B; cleared after display_fit */

    scene_light_mode lighting;
    int nlights;              /** explicit count; ignored for named rigs */
    float light_pos[SCENE_MAX_LIGHTS][4];  /** xyz + w (1 = point) */
    float light_color[SCENE_MAX_LIGHTS][3];
    float ambient[3];         /** white scene ambient; independent of lamps */

    float background[3]; /** Clear color RGB (glClearColor) */
    
    varray_float data;
    varray_int indx;
    varray_gobject objectlist;
    varray_gcolor colorlist;
    varray_gfont fontlist;
    varray_gtext textlist;
    
    varray_gdraw displaylist;

    bool changed; /** true if scene needs GL upload / camera fit on next prepare */
} scene;

scene *scene_new(int id, int dim);
scene *scene_find(int id);
void scene_clear(scene *s); /**< Free contents; keep id/dim and list link */
void scene_cleardisplaylist(scene *s); /**< Clear draws only; keep objects/colors/fonts/pools */
void scene_free(scene *s);
void scene_markchanged(scene *s);

void scene_setbbox(scene *s, float xmin, float xmax, float ymin, float ymax, float zmin, float zmax);
bool scene_computebbox(scene *s);

void scene_setlightmode(scene *s, scene_light_mode mode);
void scene_setexplicitlights(scene *s, int n, const float pos[][4], const float color[][3]);
void scene_clearlight(scene *s); /**< Reset to Neutral */

void scene_setbackground(scene *s, float r, float g, float b);

gobject *scene_addobject(scene *s, int id);
/** Clear one object's geometry (format/elements/vertexdata); keep id and draws. */
bool scene_clearobject(scene *s, int id);
/** Remove object and all OBJECT draws with matching object id. */
bool scene_deleteobject(scene *s, int id);
/** Remove one OBJECT or TEXT draw-slot by drawid; leave the object/text pool. */
bool scene_deletedraw(scene *s, int drawid);
/** Overwrite vertex floats in place; requires n == vertexdata.length. */
bool scene_replacevertices(scene *s, int id, const float *data, int n);
int scene_adddata(scene *s, float *data, int count);
int scene_addindex(scene *s, int *data, int count);
/** Take ownership of *datap (malloc/realloc); nulls *datap. Adopts without
 *  copy when the scene pool is empty; otherwise appends then frees. */
int scene_adddata_take(scene *s, float **datap, int count);
int scene_addindex_take(scene *s, int **datap, int count);
int scene_addelement(gobject *obj, gelement *el);
bool scene_addfont(scene *s, int id, char *file, float size, int *fontindx);
textfont *scene_getfontfromid(scene *s, int fontid);
int scene_addtext(scene *s, int fontid, char *text);
int scene_addcolor(scene *s, int colorid, int length, int components, int indx);
void scene_adddraw(scene *scene, gdrawtype type, int id, int matindx);
/** OBJECT or TEXT draw with @p drawid, or NULL. */
gdraw *scene_finddrawbydrawid(scene *s, int drawid);
/** First OBJECT draw with object id @p objectid (legacy 5b), or NULL. */
gdraw *scene_findobjectdraw(scene *s, int objectid);
/** Create OBJECT draw slot @p drawid referencing @p objectid.
 *  @p matrix may be NULL (identity). @p colorid may be SCENE_EMPTY. */
gdraw *scene_addobjectdraw(scene *s, int drawid, int objectid,
                           const float *matrix, int colorid);
/** Create TEXT draw slot @p drawid referencing textlist index @p textindex. */
gdraw *scene_addtextdraw(scene *s, int drawid, int textindex,
                         const float *matrix, int colorid);
/** Update matrix and/or color on an existing OBJECT or TEXT draw.
 *  If @p has_matrix, replace matrix; otherwise leave matrix unchanged.
 *  If @p stamp_color, set colorid (SCENE_EMPTY clears the uniform override).
 *  Returns false if draw missing. */
bool scene_updateobjectdraw(scene *s, gdraw *drw, bool has_matrix,
                            const float *matrix, bool stamp_color, int colorid);
/** Rebind object id on an OBJECT draw (instancing / two-arg `d`). */
void scene_setobjectdrawobject(gdraw *drw, int objectid);

gobject *scene_getgobjectfromid(scene *s, int id);
gcolor *scene_getcolorfromid(scene *s, int id);

void scene_initialize(void);
void scene_finalize(void);

#endif /* scene_h */
