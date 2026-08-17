/** @file scene.c
 *  @author T J Atherton
 *
 *  @brief Scene descriptions
 */

#include <stdlib.h>
#include <string.h>
#include "scene.h"

/* -------------------------------------------------------
 * Global variables
 * ------------------------------------------------------- */

scene *openscenes;

/** Add to list of open scenes */
static void scene_add(scene *s) {
    s->next=openscenes;
    openscenes=s;
}

/** Remove from list of open scenes (does not free) */
static void scene_remove(scene *s) {
    if (openscenes==s) {
        openscenes=s->next;
    } else {
        scene *prev = NULL;
        for (scene *e = openscenes; e!=NULL; e=e->next) {
            if (s==e) {
                prev->next=s->next;
                return;
            }
            prev=e;
        }
    }
}

/* -------------------------------------------------------
 * Constructor/Destructor
 * ------------------------------------------------------- */

/** Reset bbox flags (used by scene_new / scene_clear). */
static void scene_resetbbox(scene *s) {
    for (int i=0; i<6; i++) s->bbox[i]=0.0f;
    s->bbox_valid=false;
    s->bbox_explicit=false;
    s->bbox_fit_pending=false;
}

/** Reset lighting to Neutral (used by scene_new / scene_clear). */
static void scene_resetlight(scene *s) {
    s->lighting=SCENE_LIGHT_NEUTRAL;
    s->nlights=0;
    memset(s->light_pos, 0, sizeof(s->light_pos));
    memset(s->light_color, 0, sizeof(s->light_color));
    s->ambient[0]=1.0f; s->ambient[1]=1.0f; s->ambient[2]=1.0f;
}

/** Reset clear color to the default dark gray (used by scene_new / scene_clear). */
static void scene_resetbackground(scene *s) {
    s->background[0]=SCENE_BACKGROUND_R_DEFAULT;
    s->background[1]=SCENE_BACKGROUND_G_DEFAULT;
    s->background[2]=SCENE_BACKGROUND_B_DEFAULT;
}

/** Create a new scene */
scene *scene_new(int id, int dim) {
    scene *new = malloc(sizeof(scene));
    if (new) {
        new->next=NULL;
        new->id=id;
        new->dim=dim;
        scene_resetbbox(new);
        scene_resetlight(new);
        scene_resetbackground(new);
        varray_gobjectinit(&new->objectlist);
        varray_gdrawinit(&new->displaylist);
        varray_gcolorinit(&new->colorlist);
        varray_gfontinit(&new->fontlist);
        varray_gtextinit(&new->textlist);
        varray_floatinit(&new->data);
        varray_intinit(&new->indx);
        new->changed = false;
        scene_add(new);
    }
    return new;
}

/** Mark scene for GL upload / camera fit on next display_prepareall. */
void scene_markchanged(scene *s) {
    if (s) s->changed = true;
}

/** Free scene contents but keep the scene struct, id, dim, and list link. */
void scene_clear(scene *s) {
    if (!s) return;

    for (unsigned int i=0; i<s->objectlist.count; i++) {
        gobject *obj = &s->objectlist.data[i];
        if (obj->vertexdata.format) {
            free(obj->vertexdata.format);
            obj->vertexdata.format=NULL;
        }
        varray_gelementclear(&obj->elements);
    }

    for (unsigned int i=0; i<s->fontlist.count; i++) {
        text_fontclear(&s->fontlist.data[i].font);
    }

    for (unsigned int i=0; i<s->textlist.count; i++) {
        free(s->textlist.data[i].text);
    }

    varray_gobjectclear(&s->objectlist);
    varray_gdrawclear(&s->displaylist);
    varray_gcolorclear(&s->colorlist);
    varray_gfontclear(&s->fontlist);
    varray_gtextclear(&s->textlist);
    varray_floatclear(&s->data);
    varray_intclear(&s->indx);

    varray_gobjectinit(&s->objectlist);
    varray_gdrawinit(&s->displaylist);
    varray_gcolorinit(&s->colorlist);
    varray_gfontinit(&s->fontlist);
    varray_gtextinit(&s->textlist);
    varray_floatinit(&s->data);
    varray_intinit(&s->indx);

    scene_resetbbox(s);
    scene_resetlight(s);
    scene_resetbackground(s);
}

/** Clear the displaylist only (objects, colors, fonts, data pools kept). */
void scene_cleardisplaylist(scene *s) {
    if (!s) return;
    varray_gdrawclear(&s->displaylist);
    varray_gdrawinit(&s->displaylist);
}

/** Set an explicit scene AABB and request a camera refit. */
void scene_setbbox(scene *s, float xmin, float xmax, float ymin, float ymax, float zmin, float zmax) {
    if (!s) return;
    s->bbox[0]=xmin; s->bbox[1]=xmax;
    s->bbox[2]=ymin; s->bbox[3]=ymax;
    s->bbox[4]=zmin; s->bbox[5]=zmax;
    s->bbox_valid=true;
    s->bbox_explicit=true;
    s->bbox_fit_pending=true;
}

/** Select a named camera-relative rig (Neutral / ThreePoint). */
void scene_setlightmode(scene *s, scene_light_mode mode) {
    if (!s) return;
    s->lighting=mode;
    if (mode!=SCENE_LIGHT_EXPLICIT) s->nlights=0;
}

/** Replace the light list with n world-space point lights (n==0: ambient only). */
void scene_setexplicitlights(scene *s, int n, const float pos[][3], const float color[][3]) {
    if (!s) return;
    if (n<0) n=0;
    if (n>SCENE_MAX_LIGHTS) n=SCENE_MAX_LIGHTS;
    s->lighting=SCENE_LIGHT_EXPLICIT;
    s->nlights=n;
    memset(s->light_pos, 0, sizeof(s->light_pos));
    memset(s->light_color, 0, sizeof(s->light_color));
    for (int i=0; i<n; i++) {
        s->light_pos[i][0]=pos[i][0];
        s->light_pos[i][1]=pos[i][1];
        s->light_pos[i][2]=pos[i][2];
        s->light_color[i][0]=color[i][0];
        s->light_color[i][1]=color[i][1];
        s->light_color[i][2]=color[i][2];
    }
}

/** Set the scene clear / background color. */
void scene_setbackground(scene *s, float r, float g, float b) {
    if (!s) return;
    s->background[0]=r;
    s->background[1]=g;
    s->background[2]=b;
}

/** Vertex float stride from a format string (same rules as render_entrysizefromformat). */
static int scene_entrysizefromformat(scene *s, char *format) {
    int size = 0;
    for (char *c = format; *c != '\0'; c++) {
        switch (*c) {
            case 'x':
            case 'n': size+=s->dim; break;
            case 'c': size+=3; break;
            case 'a': size+=1; break;
            default: break;
        }
    }
    return size;
}

/** Expand AABB with a world-space point. */
static void scene_bbox_expand(float bbox[6], float x, float y, float z, bool *any) {
    if (!*any) {
        bbox[0]=bbox[1]=x;
        bbox[2]=bbox[3]=y;
        bbox[4]=bbox[5]=z;
        *any=true;
    } else {
        if (x<bbox[0]) bbox[0]=x; if (x>bbox[1]) bbox[1]=x;
        if (y<bbox[2]) bbox[2]=y; if (y>bbox[3]) bbox[3]=y;
        if (z<bbox[4]) bbox[4]=z; if (z>bbox[5]) bbox[5]=z;
    }
}

/** Expand AABB by a model-space point, optionally transformed by column-major M. */
static void scene_bbox_expand_model(float bbox[6], float *M, float x, float y, float z, bool *any) {
    if (M) {
        float wx=M[0]*x + M[4]*y + M[8]*z  + M[12];
        float wy=M[1]*x + M[5]*y + M[9]*z  + M[13];
        float wz=M[2]*x + M[6]*y + M[10]*z + M[14];
        x=wx; y=wy; z=wz;
    }
    scene_bbox_expand(bbox, x, y, z, any);
}

/** Local AABB of a string in the same layout as render_rendertext; false if no glyphs. */
static bool scene_text_local_bbox(textfont *font, const char *text, float out[6]) {
    float scale = TEXT_WORLD_SCALE;
    float x=0.0f, y=0.0f, z=0.0f;
    bool any=false;

    for (char *c = (char *) text, *next; c && *c!='\0'; c=next) {
        textglyph glyph;
        if (!text_findglyph(font, c, &glyph, &next)) break;

        float xpos = x + glyph.bearingx * scale;
        float ypos = y - (glyph.height - glyph.bearingy) * scale;
        float w = glyph.width * scale;
        float h = glyph.height * scale;

        scene_bbox_expand(out, xpos,     ypos,     z, &any);
        scene_bbox_expand(out, xpos + w, ypos + h, z, &any);

        x += (glyph.advance >> 6) * scale;
        z += 1e-4f;
    }
    return any;
}

/** Expand scene bbox by glyph quads for a TEXT draw (origin alone if empty). */
static void scene_bbox_expand_text(scene *s, gdraw *drw, float bbox[6], bool *any) {
    if (drw->id<0 || (unsigned) drw->id>=s->textlist.count) return;
    gtext *txt=&s->textlist.data[drw->id];
    if (!txt->text) return;

    textfont *font=scene_getfontfromid(s, txt->fontid);
    if (!font) return;

    float *M=NULL;
    if (drw->matindx!=SCENE_EMPTY) M=&s->data.data[drw->matindx];

    float local[6]={0};
    if (!scene_text_local_bbox(font, txt->text, local)) {
        /* No ink — still count the baseline origin. */
        scene_bbox_expand_model(bbox, M, 0.0f, 0.0f, 0.0f, any);
        return;
    }

    /* Transform the eight corners of the local text AABB. */
    for (int i=0; i<2; i++) {
        float x = (i==0) ? local[0] : local[1];
        for (int j=0; j<2; j++) {
            float y = (j==0) ? local[2] : local[3];
            for (int k=0; k<2; k++) {
                float z = (k==0) ? local[4] : local[5];
                scene_bbox_expand_model(bbox, M, x, y, z, any);
            }
        }
    }
}

/** Auto-compute AABB from drawn objects and text glyph extents (× draw matrix). */
bool scene_computebbox(scene *s) {
    if (!s) return false;

    float bbox[6]={0};
    bool any=false;

    for (unsigned int i=0; i<s->displaylist.count; i++) {
        gdraw *drw=&s->displaylist.data[i];

        if (drw->type==TEXT) {
            scene_bbox_expand_text(s, drw, bbox, &any);
            continue;
        }
        if (drw->type!=OBJECT) continue;

        gobject *obj=scene_getgobjectfromid(s, drw->id);
        if (!obj || !obj->vertexdata.format || !strchr(obj->vertexdata.format, 'x')) continue;
        if (obj->vertexdata.indx==SCENE_EMPTY || obj->vertexdata.length<=0) continue;

        int stride=scene_entrysizefromformat(s, obj->vertexdata.format);
        if (stride<=0) continue;

        int xpos=0;
        for (char *c=obj->vertexdata.format; *c!='\0' && *c!='x'; c++) {
            if (*c=='n') xpos+=s->dim;
            else if (*c=='c') xpos+=3;
            else if (*c=='a') xpos+=1;
        }

        float *M=NULL;
        if (drw->matindx!=SCENE_EMPTY) M=&s->data.data[drw->matindx];

        int nvert=obj->vertexdata.length/stride;
        for (int v=0; v<nvert; v++) {
            float *base=&s->data.data[obj->vertexdata.indx + v*stride + xpos];
            float x=base[0];
            float y=(s->dim>1) ? base[1] : 0.0f;
            float z=(s->dim>2) ? base[2] : 0.0f;
            scene_bbox_expand_model(bbox, M, x, y, z, &any);
        }
    }

    if (!any) {
        s->bbox_valid=false;
        return false;
    }

    for (int i=0; i<6; i++) s->bbox[i]=bbox[i];
    s->bbox_valid=true;
    return true;
}

/** Free a scene and associated data structures */
void scene_free(scene *s) {
    scene_remove(s);
    scene_clear(s);

    varray_gobjectclear(&s->objectlist);
    varray_gdrawclear(&s->displaylist);
    varray_gcolorclear(&s->colorlist);
    varray_gfontclear(&s->fontlist);
    varray_gtextclear(&s->textlist);
    varray_floatclear(&s->data);
    varray_intclear(&s->indx);
    free(s);
}

/** Find a scene from the id */
scene *scene_find(int id) {
    for (scene *s = openscenes; s!=NULL; s=s->next) {
        if (s->id==id) return s;
    }
    return NULL;
}

/* -------------------------------------------------------
 * Add
 * ------------------------------------------------------- */

/* Forward decl — used by scene_addobject before the Find section. */
gobject *scene_getgobjectfromid(scene *s, int id);

/** Adds an object to a scene, or returns the existing object with this id. */
gobject *scene_addobject(scene *s, int id) {
    gobject *existing = scene_getgobjectfromid(s, id);
    if (existing) return existing;

    gobject obj;
    obj.id=id;
    obj.vertexdata.format=NULL;
    obj.vertexdata.indx=SCENE_EMPTY;
    obj.vertexdata.length=SCENE_EMPTY;
    varray_gelementinit(&obj.elements);
    obj.centroid[0]=obj.centroid[1]=obj.centroid[2]=0.0f;
    obj.centroid_valid=false;

    varray_gobjectadd(&s->objectlist, &obj, 1);
    return &s->objectlist.data[s->objectlist.count-1];
}

/** Clear one object's geometry; keep id and any draws that reference it. */
bool scene_clearobject(scene *s, int id) {
    gobject *obj = scene_getgobjectfromid(s, id);
    if (!s || !obj) return false;
    if (obj->vertexdata.format) {
        free(obj->vertexdata.format);
        obj->vertexdata.format = NULL;
    }
    varray_gelementclear(&obj->elements);
    varray_gelementinit(&obj->elements);
    obj->vertexdata.indx = SCENE_EMPTY;
    obj->vertexdata.length = SCENE_EMPTY;
    obj->centroid_valid = false;
    return true;
}

/** Remove OBJECT draws whose object id matches. */
static void scene_purgedrawsforobject(scene *s, int objectid) {
    unsigned int w = 0;
    for (unsigned int i = 0; i < s->displaylist.count; i++) {
        gdraw *drw = &s->displaylist.data[i];
        if (drw->type == OBJECT && drw->id == objectid) continue;
        if (w != i) s->displaylist.data[w] = *drw;
        w++;
    }
    s->displaylist.count = w;
}

/** Remove object and all OBJECT draws that reference it. */
bool scene_deleteobject(scene *s, int id) {
    if (!s) return false;
    unsigned int i;
    for (i = 0; i < s->objectlist.count; i++) {
        if (s->objectlist.data[i].id == id) break;
    }
    if (i >= s->objectlist.count) return false;

    gobject *obj = &s->objectlist.data[i];
    if (obj->vertexdata.format) {
        free(obj->vertexdata.format);
        obj->vertexdata.format = NULL;
    }
    varray_gelementclear(&obj->elements);

    scene_purgedrawsforobject(s, id);

    for (unsigned int j = i + 1; j < s->objectlist.count; j++) {
        s->objectlist.data[j - 1] = s->objectlist.data[j];
    }
    s->objectlist.count--;
    return true;
}

/** Remove one OBJECT or TEXT draw-slot by drawid. */
bool scene_deletedraw(scene *s, int drawid) {
    if (!s) return false;
    unsigned int i;
    for (i = 0; i < s->displaylist.count; i++) {
        gdraw *drw = &s->displaylist.data[i];
        if ((drw->type == OBJECT || drw->type == TEXT) &&
            drw->drawid == drawid) break;
    }
    if (i >= s->displaylist.count) return false;

    for (unsigned int j = i + 1; j < s->displaylist.count; j++) {
        s->displaylist.data[j - 1] = s->displaylist.data[j];
    }
    s->displaylist.count--;
    return true;
}

/** Overwrite vertex floats in place; requires same length. */
bool scene_replacevertices(scene *s, int id, const float *data, int n) {
    gobject *obj = scene_getgobjectfromid(s, id);
    if (!s || !obj || !data || n <= 0) return false;
    if (obj->vertexdata.indx == SCENE_EMPTY || obj->vertexdata.length != n)
        return false;
    memcpy(&s->data.data[obj->vertexdata.indx], data, sizeof(float) * (size_t) n);
    obj->centroid_valid = false;
    return true;
}

/** Add vertex data to a scene; returns the starting index of the data */
int scene_adddata(scene *s, float *data, int count) {
    int ret = s->data.count;
    varray_floatadd(&s->data, data, count);
    return ret;
}

/** Add index data to a scene; returns the starting index of the data  */
int scene_addindex(scene *s, int *data, int count) {
    int ret=s->indx.count;
    varray_intadd(&s->indx, data, count);
    return ret;
}

/** Adopt *datap into the float pool, or append+free. Always nulls *datap. */
int scene_adddata_take(scene *s, float **datap, int count) {
    if (!datap || !*datap || count<=0) {
        if (datap && *datap) {
            free(*datap);
            *datap=NULL;
        }
        return s ? (int) s->data.count : 0;
    }

    int ret = (int) s->data.count;
    if (ret==0) {
        /* Pool empty: adopt the buffer (compatible with morpho realloc/free). */
        if (s->data.data) morpho_allocate(s->data.data, 0, 0);
        s->data.data = *datap;
        s->data.count = (unsigned int) count;
        s->data.capacity = (unsigned int) count;
        *datap = NULL;
        return 0;
    }

    if (!varray_floatadd(&s->data, *datap, count)) {
        free(*datap);
        *datap = NULL;
        return -1;
    }
    free(*datap);
    *datap = NULL;
    return ret;
}

/** Adopt *datap into the index pool, or append+free. Always nulls *datap. */
int scene_addindex_take(scene *s, int **datap, int count) {
    if (!datap || !*datap || count<=0) {
        if (datap && *datap) {
            free(*datap);
            *datap=NULL;
        }
        return s ? (int) s->indx.count : 0;
    }

    int ret = (int) s->indx.count;
    if (ret==0) {
        if (s->indx.data) morpho_allocate(s->indx.data, 0, 0);
        s->indx.data = *datap;
        s->indx.count = (unsigned int) count;
        s->indx.capacity = (unsigned int) count;
        *datap = NULL;
        return 0;
    }

    if (!varray_intadd(&s->indx, *datap, count)) {
        free(*datap);
        *datap = NULL;
        return -1;
    }
    free(*datap);
    *datap = NULL;
    return ret;
}

/** Adds element data to an object */
int scene_addelement(gobject *obj, gelement *el) {
    varray_gelementadd(&obj->elements, el, 1);
    return obj->elements.count-1;
}

/** Adds a font to a scene
 @param[in] s - The scene
 @param[in] id - font id
 @param[in] file - font file to open
 @param[in] size - in points
 @param[out] fontindx - index to refer to this */
bool scene_addfont(scene *s, int id, char *file, float size, int *fontindx) {
    gfont font;
    
    font.id=id;
    text_fontinit(&font.font, TEXT_DEFAULTWIDTH);
    
    int sizepx = (int) (size / 72.0 * 720.0) /* in pts / points per inch * DPI */;
    
    if (text_openfont(file, sizepx, &font.font)) {
        varray_gfontwrite(&s->fontlist, font);
        if (fontindx) *fontindx = s->fontlist.count-1;
        return true;
    }

    return false;
}

/** Find the textfont object corresponding to a given fontid */
textfont *scene_getfontfromid(scene *s, int fontid) {
    for (int i=0; i<s->fontlist.count; i++) {
        if (s->fontlist.data[i].id==fontid) return &s->fontlist.data[i].font;
    }
    return NULL;
}

/** Adds text to a scene */
int scene_addtext(scene *s, int fontid, char *text) {
    textfont *font = scene_getfontfromid(s, fontid);

    if (!font) {
        fprintf(stderr, "Font id '%i' not found.\n", fontid);
        return false;
    }
    
    text_prepare(font, text);
    
    gtext txt;
    txt.fontid=fontid;
    txt.text=text;
    
    return varray_gtextwrite(&s->textlist, txt);;
}

/** Adds a color to a scene */
int scene_addcolor(scene *s, int colorid, int length, int components, int indx) {
    gcolor color = { .colorid = colorid,
                     .length = length,
                     .components = components,
                     .indx = indx
    };
    
    return varray_gcolorwrite(&s->colorlist, color);
}

void scene_adddraw(scene *scene, gdrawtype type, int id, int matindx) {
    gdraw d = { .type = type, .id = id, .drawid = SCENE_EMPTY,
                .colorid = SCENE_EMPTY, .matindx = matindx };
    varray_gdrawwrite(&scene->displaylist, d);
}

/** OBJECT or TEXT draw with drawid, or NULL. */
gdraw *scene_finddrawbydrawid(scene *s, int drawid) {
    if (!s) return NULL;
    for (unsigned int i = 0; i < s->displaylist.count; i++) {
        gdraw *drw = &s->displaylist.data[i];
        if ((drw->type == OBJECT || drw->type == TEXT) &&
            drw->drawid == drawid) return drw;
    }
    return NULL;
}

/** First OBJECT draw with object id (legacy), or NULL. */
gdraw *scene_findobjectdraw(scene *s, int objectid) {
    if (!s) return NULL;
    for (unsigned int i = 0; i < s->displaylist.count; i++) {
        gdraw *drw = &s->displaylist.data[i];
        if (drw->type == OBJECT && drw->id == objectid) return drw;
    }
    return NULL;
}

void scene_setobjectdrawobject(gdraw *drw, int objectid) {
    if (drw) drw->id = objectid;
}

/** Create OBJECT draw slot. */
gdraw *scene_addobjectdraw(scene *s, int drawid, int objectid,
                           const float *matrix, int colorid) {
    if (!s) return NULL;
    int matindx = SCENE_EMPTY;
    if (matrix) {
        float tmp[16];
        memcpy(tmp, matrix, sizeof(tmp));
        matindx = scene_adddata(s, tmp, 16);
    }
    gdraw d = { .type = OBJECT, .id = objectid, .drawid = drawid,
                .colorid = colorid, .matindx = matindx };
    varray_gdrawwrite(&s->displaylist, d);
    return &s->displaylist.data[s->displaylist.count - 1];
}

/** Create TEXT draw slot. */
gdraw *scene_addtextdraw(scene *s, int drawid, int textindex,
                         const float *matrix, int colorid) {
    if (!s) return NULL;
    int matindx = SCENE_EMPTY;
    if (matrix) {
        float tmp[16];
        memcpy(tmp, matrix, sizeof(tmp));
        matindx = scene_adddata(s, tmp, 16);
    }
    gdraw d = { .type = TEXT, .id = textindex, .drawid = drawid,
                .colorid = colorid, .matindx = matindx };
    varray_gdrawwrite(&s->displaylist, d);
    return &s->displaylist.data[s->displaylist.count - 1];
}

/** Update matrix and/or color; leave matrix alone when !has_matrix.
 *  When @p stamp_color, set colorid (SCENE_EMPTY clears the uniform override). */
bool scene_updateobjectdraw(scene *s, gdraw *drw, bool has_matrix,
                            const float *matrix, bool stamp_color, int colorid) {
    if (!s || !drw || (drw->type != OBJECT && drw->type != TEXT)) return false;
    if (has_matrix) {
        if (!matrix) {
            drw->matindx = SCENE_EMPTY;
        } else if (drw->matindx != SCENE_EMPTY) {
            memcpy(&s->data.data[drw->matindx], matrix, sizeof(float) * 16);
        } else {
            float tmp[16];
            memcpy(tmp, matrix, sizeof(tmp));
            drw->matindx = scene_adddata(s, tmp, 16);
        }
    }
    if (stamp_color) drw->colorid = colorid;
    return true;
}

/* -------------------------------------------------------
 * Find
 * ------------------------------------------------------- */

/** Gets a gobject structure given an id */
gobject *scene_getgobjectfromid(scene *s, int id) {
    for (unsigned int i=0; i<s->objectlist.count; i++) {
        if (s->objectlist.data[i].id==id) return &s->objectlist.data[i];
    }
    return NULL;
}

/** Gets a gcolor structure given an id */
gcolor *scene_getcolorfromid(scene *s, int id) {
    for (unsigned int i=0; i<s->colorlist.count; i++) {
        if (s->colorlist.data[i].colorid==id) return &s->colorlist.data[i];
    }
    return NULL;
}

/* -------------------------------------------------------
 * Varrays
 * ------------------------------------------------------- */

DEFINE_VARRAY(gobject, gobject);
DEFINE_VARRAY(gelement, gelement);
DEFINE_VARRAY(gcolor, gcolor);
DEFINE_VARRAY(gfont, gfont);
DEFINE_VARRAY(gdraw, gdraw);
DEFINE_VARRAY(gtext, gtext);
DEFINE_VARRAY(float, float);

/* -------------------------------------------------------
 * Initialize/Finalize
 * ------------------------------------------------------- */

void scene_initialize(void) {
    openscenes=NULL;
}

void scene_finalize(void) {
    while (openscenes!=NULL) {
        scene_free(openscenes);
    }
}
