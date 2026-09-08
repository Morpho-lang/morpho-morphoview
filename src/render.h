/** @file render.h
 *  @author T J Atherton
 *
 *  @brief OpenGL rendering
 */

#ifndef render_h
#define render_h

#include <stdio.h>
#include <stdbool.h>
#include "varray.h"
#include "matrix3d.h"
#include "scene.h"

#define GL_SILENCE_DEPRECATION
#include <glad/glad.h>

DECLARE_VARRAY(GLuint, GLuint)

/** OpenGL VAO, VBO and element buffer for one object's vertices. */
typedef struct {
    char *format;
    GLuint array; /* Handle for vertex array object */
    GLuint buffer; /* Handle for vertex buffer object */
    GLuint element; /* Handle for element array buffer object */
    int vlength; /* Length of the vertex buffer */
    int elength; /* Length of the element array buffer */
} renderglbuffers;

DECLARE_VARRAY(renderglbuffers, renderglbuffers)

/** A scene object bound to a GL buffer (index, not a raw pointer — the varray reallocates). */
typedef struct {
    gobject *obj; /* The original object */
    int bufferindex; /* Index into renderer.glbuffers, or -1 if unset */
    int voffset; /* Offset into the vertex buffer */
    int eoffset; /* Offset into the element array buffer */
} renderobject;

DECLARE_VARRAY(renderobject, renderobject)

/** Font with an uploaded atlas texture. */
typedef struct {
    textfont *font;
    GLuint texture; /* Handle for atlas texture */
} renderfont;

DECLARE_VARRAY(renderfont, renderfont)

/** One packed render-list instruction. */
typedef struct {
    enum {
        RNOP,
        RMODEL, /* Set the model matrix */
        RARRAY, /* Bind a VAO */
        RTRIANGLES, /* Draw triangles */
        RLINES, /* Draw lines */
        RPOINTS, /* Draw points */
        RTEXT, /* Draw text */
        RCOLOR, /* Set the current color (uniform albedo for geometry / text) */
        RSHADE, /* Set shade mode + Phong coefficients */
    } instruction;
    
    union {
        struct {
            float *model;
        } model;
        
        struct {
            GLuint handle;
        } array;
        
        struct {
            int length;
            void *offset;
        } triangles;
        
        struct {
            char *txt;
            int rfontid; 
        } text;
        
        struct {
            float rgba[4];
            int use_uniform; /* 1 = geometry uses uColor; text always uses rgb */
        } color;

        struct {
            int mode; /* SCENE_SHADE_SHADED or SCENE_SHADE_FLAT */
            float ka;
            float kd;
            float ks;
            float shininess;
        } shade;
    } data;
    
    renderobject *obj;
} renderinstruction;

DECLARE_VARRAY(renderinstruction, renderinstruction)

/** Cached uniform locations for the geometry program. */
typedef struct {
    GLint model;
    GLint view;
    GLint proj;
    GLint normalMatrix;
    GLint nLights;
    GLint lightPos;
    GLint lightColor;
    GLint ambientColor;
    GLint uColor;
    GLint uUseUniform;
    GLint uFlat;
    GLint ka;
    GLint kd;
    GLint ks;
    GLint shininess;
} renderuniforms;

/** Cached uniform locations for the text program. */
typedef struct {
    GLint model;
    GLint view;
    GLint proj;
    GLint textColor;
} rendertextuniforms;

/** Transparent draw replayed after the opaque pass (sorted far to near). */
typedef struct {
    GLenum mode;
    int length;
    void *offset;
    GLuint vao;
    mat4x4 model;
    float rgba[4];
    int use_uniform;
    int uflat;
    float ka;
    float kd;
    float ks;
    float shininess;
    float depth; /* view-space z of object centroid; ascending = far → near */
} rendertdraw;

DECLARE_VARRAY(rendertdraw, rendertdraw)

/** Renderer object. */
typedef struct {
    GLuint shader;
    GLuint textshader;
    renderuniforms uniforms;
    rendertextuniforms textuniforms;
    varray_renderobject objects;
    varray_renderfont fonts;
    varray_renderglbuffers glbuffers;
    varray_renderinstruction renderlist;
    varray_rendertdraw tdraws; /* scratch: transparent draws (capacity retained) */
    GLuint fontvao;
    GLuint fontvbo;
    mat4x4 frameview; /* view matrix for the current frame (normalMatrix) */
} renderer;

bool render_init(renderer *r);
void render_reset(renderer *r); /**< Drop GL geometry; keep shaders */
void render_clear(renderer *r);

void render_preparescene(renderer *r, scene *s);
/** Upload same-length vertex data; false if no prepared buffer exists. */
bool render_updateobjectvertices(renderer *r, scene *s, int objectid);
void render_render(renderer *r, float aspectratio, mat4x4 view, float near, float far, scene *s);

#endif /* render_h */
