/** @file render.c
 *  @author T J Atherton
 *
 *  @brief OpenGL rendering
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "render.h"

/* -------------------------------------------------------
 * Varrays
 * ------------------------------------------------------- */

DEFINE_VARRAY(renderobject, renderobject)

DEFINE_VARRAY(renderfont, renderfont)

DEFINE_VARRAY(renderglbuffers, renderglbuffers)

DEFINE_VARRAY(renderinstruction, renderinstruction)

DEFINE_VARRAY(rendertdraw, rendertdraw)

/* -------------------------------------------------------
 * Shaders
 * ------------------------------------------------------- */

/* Geometry: OpenGL/VTK Phong when uFlat==0 (Lambert when ks=0); unlit albedo when uFlat!=0.
 * Lighting and normals in view space; normalMatrix = inverseTranspose(view*model). */

#define MV_STR(x) MV_XSTR(x)
#define MV_XSTR(x) #x

const char *vertexshader =
    "#version 330 core\n"
    "layout (location = 0) in vec3 vPos;\n"
    "layout (location = 1) in vec3 vColor;\n"
    "layout (location = 2) in vec3 vNormal;\n"
    "layout (location = 3) in float vAlpha;\n"
    "out vec3 fragColor;\n"
    "out vec3 fragPos;\n"
    "out vec3 normal;\n"
    "out float fragAlpha;\n"
    "uniform mat4 model;\n"
    "uniform mat4 view;\n"
    "uniform mat4 proj;\n"
    "uniform mat3 normalMatrix;\n"
    "\n"
    "void main() {\n"
    "   vec4 viewPos4 = view * model * vec4(vPos, 1.0);\n"
    "   gl_Position = proj * viewPos4;\n"
    "   fragColor = vColor;\n"
    "   fragPos = viewPos4.xyz;\n"
    "   normal = normalMatrix * vNormal;\n"
    "   fragAlpha = vAlpha;\n"
    "}\n";

const char *fragmentshader =
    "#version 330 core\n"
    "#define MAX_LIGHTS " MV_STR(SCENE_MAX_LIGHTS) "\n"
    "out vec4 FragColor;\n"
    "in vec3 fragColor;\n"
    "in vec3 fragPos;\n"
    "in vec3 normal;\n"
    "in float fragAlpha;\n"
    "uniform int nLights;\n"
    "uniform vec4 lightPos[MAX_LIGHTS];\n"
    "uniform vec4 lightColor[MAX_LIGHTS];\n"
    "uniform vec3 ambientColor;\n"
    "uniform vec4 uColor;\n"
    "uniform int uUseUniform;\n"
    "uniform int uFlat;\n"
    "uniform float ka;\n"
    "uniform float kd;\n"
    "uniform float ks;\n"
    "uniform float shininess;\n"
    "\n"
    "void main() {\n"
    "   vec3 albedo;\n"
    "   float alpha;\n"
    "   if (uUseUniform != 0) {\n"
    "       albedo = uColor.rgb;\n"
    "       alpha = uColor.a;\n"
    "   } else {\n"
    "       albedo = fragColor;\n"
    "       alpha = fragAlpha;\n"
    "   }\n"
    "   if (uFlat != 0) {\n"
    "       FragColor = vec4(albedo, alpha);\n"
    "       return;\n"
    "   }\n"
    "   vec3 norm = normalize(normal);\n"
    "   if (!gl_FrontFacing) norm = -norm;\n"
    "   vec3 viewDir = vec3(0.0, 0.0, 1.0);\n"
    "   vec3 lit = ka * ambientColor;\n"
    "   int n = nLights;\n"
    "   if (n > MAX_LIGHTS) n = MAX_LIGHTS;\n"
    "   for (int i = 0; i < MAX_LIGHTS; i++) {\n"
    "       if (i >= n) break;\n"
    "       vec4 lp = lightPos[i];\n"
    "       vec3 Lvec = (lp.w < 0.5) ? lp.xyz : (lp.xyz - fragPos);\n"
    "       float llen = length(Lvec);\n"
    "       if (llen < 1e-8) continue;\n"
    "       vec3 lightDir = Lvec / llen;\n"
    "       float NdotL = max(dot(norm, lightDir), 0.0);\n"
    "       vec3 C = lightColor[i].rgb;\n"
    "       vec3 diffuse = kd * NdotL * C;\n"
    "       vec3 reflectDir = reflect(-lightDir, norm);\n"
    "       float spec = pow(max(dot(viewDir, reflectDir), 0.0), max(shininess, 1e-4));\n"
    "       vec3 specular = ks * spec * C;\n"
    "       lit += diffuse + specular;\n"
    "   }\n"
    "   FragColor = vec4(lit * albedo, alpha);\n"
    "}\n";

/* Text shader */

const char *textvertexshader =
    "#version 330 core\n"
    "layout (location = 0) in vec3 vertex;\n"
    "layout (location = 1) in vec2 tex;\n"
    "out vec2 TexCoords;\n"
    "\n"
    "uniform mat4 model;\n"
    "uniform mat4 view;\n"
    "uniform mat4 proj;\n"
    "\n"
    "void main() {\n"
    "    gl_Position = proj * view * model * vec4(vertex, 1.0);\n"
    "    TexCoords = tex;\n"
    "}\n";

const char *textfragmentshader =
    "#version 330 core\n"
    "in vec2 TexCoords;\n"
    "out vec4 color;\n"
    "uniform sampler2D text;\n"
    "uniform vec4 textColor;\n"
    "\n"
    "void main() {\n"
    "   vec4 sampled = vec4(1.0, 1.0, 1.0, texture(text, TexCoords).r);\n"
    "   color = textColor * sampled;\n"
    "}\n";

/* -------------------------------------------------------
 * Compile shaders
 * ------------------------------------------------------- */

/** Compile and link a vertex + fragment shader.
 * @param[in] vertexshadersource - vertex GLSL
 * @param[in] fragmentshadersource - fragment GLSL
 * @param[out] program - linked program id
 * @returns true on success */
bool render_compileprogram(const char *vertexshadersource, const char *fragmentshadersource, GLuint *program) {
    int success;
    char infoLog[512];

    GLuint vertexshader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexshader, 1, &vertexshadersource, NULL);
    glCompileShader(vertexshader);
    glGetShaderiv(vertexshader, GL_COMPILE_STATUS, &success);
    if(!success) {
        glGetShaderInfoLog(vertexshader, 512, NULL, infoLog);
        fprintf(stderr, "morphoview: Vertex shader failed to compile with error '%s'\n", infoLog);
        glDeleteShader(vertexshader);
        return false;
    }

    GLuint fragmentshader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentshader, 1, &fragmentshadersource, NULL);
    glCompileShader(fragmentshader);
    glGetShaderiv(fragmentshader, GL_COMPILE_STATUS, &success);
    if(!success) {
        glGetShaderInfoLog(fragmentshader, 512, NULL, infoLog);
        fprintf(stderr,"Fragment shader failed to compile with error '%s'\n", infoLog);
        glDeleteShader(vertexshader);
        glDeleteShader(fragmentshader);
        return false;
    }

    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexshader);
    glAttachShader(shaderProgram, fragmentshader);
    glLinkProgram(shaderProgram);
    glDeleteShader(vertexshader);
    glDeleteShader(fragmentshader);

    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if(!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        fprintf(stderr, "Shader link failure with error '%s'\n", infoLog);
        glDeleteProgram(shaderProgram);
        return false;
    }

    if (program) *program = shaderProgram;
    return true;
}

/** Cache geometry-program uniform locations after a successful link. */
static bool render_cacheuniforms(renderer *r) {
    GLuint p = r->shader;
    r->uniforms.model = glGetUniformLocation(p, "model");
    r->uniforms.view = glGetUniformLocation(p, "view");
    r->uniforms.proj = glGetUniformLocation(p, "proj");
    r->uniforms.normalMatrix = glGetUniformLocation(p, "normalMatrix");
    r->uniforms.nLights = glGetUniformLocation(p, "nLights");
    r->uniforms.lightPos = glGetUniformLocation(p, "lightPos");
    r->uniforms.lightColor = glGetUniformLocation(p, "lightColor");
    r->uniforms.ambientColor = glGetUniformLocation(p, "ambientColor");
    r->uniforms.uColor = glGetUniformLocation(p, "uColor");
    r->uniforms.uUseUniform = glGetUniformLocation(p, "uUseUniform");
    r->uniforms.uFlat = glGetUniformLocation(p, "uFlat");
    r->uniforms.ka = glGetUniformLocation(p, "ka");
    r->uniforms.kd = glGetUniformLocation(p, "kd");
    r->uniforms.ks = glGetUniformLocation(p, "ks");
    r->uniforms.shininess = glGetUniformLocation(p, "shininess");
    if (r->uniforms.model<0 || r->uniforms.view<0 || r->uniforms.proj<0 ||
        r->uniforms.normalMatrix<0 || r->uniforms.ambientColor<0 ||
        r->uniforms.uColor<0 || r->uniforms.uUseUniform<0 || r->uniforms.uFlat<0 ||
        r->uniforms.nLights<0 || r->uniforms.lightPos<0 || r->uniforms.lightColor<0 ||
        r->uniforms.ka<0 || r->uniforms.kd<0 || r->uniforms.ks<0 || r->uniforms.shininess<0) {
        fprintf(stderr, "morphoview: geometry shader is missing a lighting/color uniform\n");
        return false;
    }
    return true;
}

/** Cache text-program uniform locations after a successful link. */
static bool render_cachetextuniforms(renderer *r) {
    GLuint p = r->textshader;
    r->textuniforms.model = glGetUniformLocation(p, "model");
    r->textuniforms.view = glGetUniformLocation(p, "view");
    r->textuniforms.proj = glGetUniformLocation(p, "proj");
    r->textuniforms.textColor = glGetUniformLocation(p, "textColor");
    if (r->textuniforms.model<0 || r->textuniforms.view<0 ||
        r->textuniforms.proj<0 || r->textuniforms.textColor<0) {
        fprintf(stderr, "morphoview: text shader is missing a uniform\n");
        return false;
    }
    return true;
}

/** Inverse-transpose of the upper 3x3 of M (column-major). */
static void render_normalmatrix(mat4x4 m, mat3x3 out) {
    mat4x4 inv;
    mat3d_invert4x4(m, inv);
    out[0]=inv[0]; out[1]=inv[4]; out[2]=inv[8];
    out[3]=inv[1]; out[4]=inv[5]; out[5]=inv[9];
    out[6]=inv[2]; out[7]=inv[6]; out[8]=inv[10];
}

/** Neutral / ThreePoint view-space dirs: keep in sync with `_povNamedRig` in xpovray.morpho. */
static const float render_neutral_dir[3][3] = {
    { 1.5f, -0.5f, 1.5f },
    { 1.5f,  1.5f, 1.5f },
    {-0.5f,  1.5f, 1.5f }
};

/** ThreePoint key / fill / rim (view space). */
static const float render_threepoint_dir[3][3] = {
    { 0.5f,  0.5f,  1.5f },
    {-1.5f, -2.5f,  1.5f },
    { 0.0f,  1.5f, -1.5f }
};
static const float render_threepoint_color[3][3] = {
    { 0.85f, 0.85f, 0.85f },
    { 0.40f, 0.40f, 0.40f },
    { 0.10f, 0.10f, 0.10f }
};

#define RENDER_NEUTRAL_INTENSITY 0.40f
static const float render_neutral_color[3][3] = {
    { RENDER_NEUTRAL_INTENSITY, RENDER_NEUTRAL_INTENSITY, RENDER_NEUTRAL_INTENSITY },
    { RENDER_NEUTRAL_INTENSITY, RENDER_NEUTRAL_INTENSITY, RENDER_NEUTRAL_INTENSITY },
    { RENDER_NEUTRAL_INTENSITY, RENDER_NEUTRAL_INTENSITY, RENDER_NEUTRAL_INTENSITY }
};

/** Copy a view-space directional rig (w=0) into the upload buffers. */
static void render_copy_dir_rig(const float dir[][3], const float col[][3], int n,
                                float pos[SCENE_MAX_LIGHTS][4],
                                float color[SCENE_MAX_LIGHTS][4]) {
    for (int i=0; i<n; i++) {
        pos[i][0]=dir[i][0];
        pos[i][1]=dir[i][1];
        pos[i][2]=dir[i][2];
        pos[i][3]=0.0f;
        color[i][0]=col[i][0];
        color[i][1]=col[i][1];
        color[i][2]=col[i][2];
        color[i][3]=0.0f;
    }
}

/** Transform a world-space point by the view matrix (w=1). */
static void render_view_transform_point(mat4x4 view, const float world[3], float out[3]) {
    out[0]=view[0]*world[0] + view[4]*world[1] + view[8]*world[2]  + view[12];
    out[1]=view[1]*world[0] + view[5]*world[1] + view[9]*world[2]  + view[13];
    out[2]=view[2]*world[0] + view[6]*world[1] + view[10]*world[2] + view[14];
}

/** Upload lighting uniforms for this frame. */
static void render_uploadlights(renderer *r, scene *s, mat4x4 view) {
    float pos[SCENE_MAX_LIGHTS][4];
    float color[SCENE_MAX_LIGHTS][4];
    float ambient[3]={1.0f, 1.0f, 1.0f};
    int n=0;
    memset(pos, 0, sizeof(pos));
    memset(color, 0, sizeof(color));

    scene_light_mode mode = s ? s->lighting : SCENE_LIGHT_NEUTRAL;
    if (s) {
        ambient[0]=s->ambient[0];
        ambient[1]=s->ambient[1];
        ambient[2]=s->ambient[2];
    }

    if (mode==SCENE_LIGHT_THREEPOINT) {
        n=3;
        render_copy_dir_rig(render_threepoint_dir, render_threepoint_color, n, pos, color);
    } else if (mode==SCENE_LIGHT_EXPLICIT && s) {
        n=s->nlights;
        if (n>SCENE_MAX_LIGHTS) n=SCENE_MAX_LIGHTS;
        for (int i=0; i<n; i++) {
            render_view_transform_point(view, s->light_pos[i], pos[i]);
            pos[i][3]=1.0f;
            color[i][0]=s->light_color[i][0];
            color[i][1]=s->light_color[i][1];
            color[i][2]=s->light_color[i][2];
            color[i][3]=0.0f;
        }
    } else {
        /* Neutral (default, including missing scene). */
        n=3;
        render_copy_dir_rig(render_neutral_dir, render_neutral_color, n, pos, color);
    }

    glUseProgram(r->shader);
    glUniform1i(r->uniforms.nLights, n);
    glUniform4fv(r->uniforms.lightPos, SCENE_MAX_LIGHTS, (const GLfloat *) pos);
    glUniform4fv(r->uniforms.lightColor, SCENE_MAX_LIGHTS, (const GLfloat *) color);
    glUniform3fv(r->uniforms.ambientColor, 1, ambient);
}

/* -------------------------------------------------------
 * Initialize/finalize display
 * ------------------------------------------------------- */

/** Initialize a renderer and compile shaders. */
bool render_init(renderer *r) {
    r->shader=0;
    r->textshader=0;
    memset(&r->uniforms, 0, sizeof(r->uniforms));
    memset(&r->textuniforms, 0, sizeof(r->textuniforms));

    if (!render_compileprogram(vertexshader, fragmentshader, &r->shader)) return false;
    if (!render_cacheuniforms(r)) {
        glDeleteProgram(r->shader);
        r->shader=0;
        return false;
    }
    if (!render_compileprogram(textvertexshader, textfragmentshader, &r->textshader)) {
        glDeleteProgram(r->shader);
        r->shader=0;
        return false;
    }
    if (!render_cachetextuniforms(r)) {
        glDeleteProgram(r->shader);
        glDeleteProgram(r->textshader);
        r->shader=0;
        r->textshader=0;
        return false;
    }
    
    /* Enable OpenGL features */
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glPointSize(6.0f);

    varray_renderobjectinit(&r->objects);
    varray_renderfontinit(&r->fonts);
    varray_renderglbuffersinit(&r->glbuffers);
    varray_renderinstructioninit(&r->renderlist);
    varray_rendertdrawinit(&r->tdraws);
    r->fontvao=0;
    r->fontvbo=0;
    mat3d_identity4x4(r->frameview);

    return true;
}

/** Free uploaded geometry/fonts; leave shader programs intact for reuse. */
void render_reset(renderer *r) {
    for (unsigned int i=0; i<r->glbuffers.count; i++) {
        renderglbuffers *b=&r->glbuffers.data[i];

        glDeleteVertexArrays(1, &b->array);
        glDeleteBuffers(1, &b->buffer);
        glDeleteBuffers(1, &b->element);
    }

    for (unsigned int i=0; i<r->fonts.count; i++) {
        glDeleteTextures(1, &r->fonts.data[i].texture);
    }

    if (r->fontvao) {
        glDeleteVertexArrays(1, &r->fontvao);
        r->fontvao=0;
    }
    if (r->fontvbo) {
        glDeleteBuffers(1, &r->fontvbo);
        r->fontvbo=0;
    }

    varray_renderglbuffersclear(&r->glbuffers);
    varray_renderfontclear(&r->fonts);
    varray_renderobjectclear(&r->objects);
    varray_renderinstructionclear(&r->renderlist);

    varray_renderglbuffersinit(&r->glbuffers);
    varray_renderfontinit(&r->fonts);
    varray_renderobjectinit(&r->objects);
    varray_renderinstructioninit(&r->renderlist);
}

/** Release all renderer GL resources including shaders. */
void render_clear(renderer *r) {
    render_reset(r);
    varray_rendertdrawclear(&r->tdraws);
    if (r->shader) glDeleteProgram(r->shader);
    if (r->textshader) glDeleteProgram(r->textshader);
    r->shader=0;
    r->textshader=0;
}


/* -------------------------------------------------------
 * Text rendering
 * ------------------------------------------------------- */

/** Create an OpenGL texture from a font atlas. */
void render_fonttexture(renderer *r, textfont *font, GLuint *out) {
    /* Now create an OpenGL texture from this */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // disable byte-alignment restriction
    
    glGenTextures(1, out); // Create and define the texture
    glBindTexture(GL_TEXTURE_2D, *out);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, font->skyline.width, font->skyline.height,
                 0, GL_RED, GL_UNSIGNED_BYTE, font->texturedata);
    // set texture options
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

/** Upload fonts and allocate the shared text quad VAO. */
void render_preparefonts(renderer *r, scene *scene) {
    
    for (int i=0; i<scene->fontlist.count; i++) {
        gfont *f=&scene->fontlist.data[i];
        /* Rebuild CPU atlas only when glyphs changed; always re-upload after render_reset. */
        if (f->font.atlas_dirty || !f->font.texturedata) {
            if (!text_generatetexture(&f->font)) continue;
        }
        
        renderfont font;
        font.font=&f->font;
        render_fonttexture(r, &f->font, &font.texture);
        varray_renderfontwrite(&r->fonts, font);
    }
    
    glGenVertexArrays(1, &r->fontvao);
    glGenBuffers(1, &r->fontvbo);
    
    glBindVertexArray(r->fontvao);
    glBindBuffer(GL_ARRAY_BUFFER, r->fontvbo);
    
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 5, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), 0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *) (sizeof(GLfloat)*3));
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

/* -------------------------------------------------------
 * Pack renderlist (skip redundant adjacent state)
 * ------------------------------------------------------- */

/** Tracks last emitted VAO / color / shade while packing the display list. */
typedef struct {
    GLuint array;
    bool have_color;
    float rgba[4];
    int use_uniform;
    bool have_shade;
    int shade_mode;
    float ka, kd, ks, shininess;
} renderpackstate;

static void render_packstate_reset(renderpackstate *st) {
    st->array=0;
    st->have_color=false;
    st->rgba[0]=st->rgba[1]=st->rgba[2]=st->rgba[3]=1.0f;
    st->use_uniform=0;
    st->have_shade=false;
    st->shade_mode=SCENE_SHADE_SHADED;
    st->ka=SCENE_MATERIAL_KA_DEFAULT;
    st->kd=SCENE_MATERIAL_KD_DEFAULT;
    st->ks=SCENE_MATERIAL_KS_DEFAULT;
    st->shininess=SCENE_MATERIAL_SHININESS_DEFAULT;
}

static bool render_pack_color_same(const renderpackstate *st, const float rgba[4], int use_uniform) {
    return st->have_color &&
           st->use_uniform==use_uniform &&
           st->rgba[0]==rgba[0] && st->rgba[1]==rgba[1] &&
           st->rgba[2]==rgba[2] && st->rgba[3]==rgba[3];
}

static void render_pack_array(renderer *r, renderpackstate *st, GLuint handle, renderobject *obj) {
    if (st->array==handle && handle!=0) return;
    renderinstruction ins = { .instruction = RARRAY, .data.array.handle = handle, .obj=obj };
    varray_renderinstructionadd(&r->renderlist, &ins, 1);
    st->array=handle;
}

static void render_pack_color(renderer *r, renderpackstate *st, const float rgba[4],
                              int use_uniform, renderobject *obj) {
    if (render_pack_color_same(st, rgba, use_uniform)) return;
    renderinstruction cins = { .instruction = RCOLOR, .obj=obj };
    cins.data.color.rgba[0]=rgba[0];
    cins.data.color.rgba[1]=rgba[1];
    cins.data.color.rgba[2]=rgba[2];
    cins.data.color.rgba[3]=rgba[3];
    cins.data.color.use_uniform=use_uniform;
    varray_renderinstructionadd(&r->renderlist, &cins, 1);
    st->have_color=true;
    st->rgba[0]=rgba[0]; st->rgba[1]=rgba[1];
    st->rgba[2]=rgba[2]; st->rgba[3]=rgba[3];
    st->use_uniform=use_uniform;
}

static void render_pack_shade(renderer *r, renderpackstate *st, int mode,
                              float ka, float kd, float ks, float shininess) {
    if (st->have_shade &&
        st->shade_mode==mode &&
        st->ka==ka && st->kd==kd && st->ks==ks && st->shininess==shininess) return;
    renderinstruction ins = { .instruction = RSHADE };
    ins.data.shade.mode=mode;
    ins.data.shade.ka=ka;
    ins.data.shade.kd=kd;
    ins.data.shade.ks=ks;
    ins.data.shade.shininess=shininess;
    varray_renderinstructionadd(&r->renderlist, &ins, 1);
    st->have_shade=true;
    st->shade_mode=mode;
    st->ka=ka; st->kd=kd; st->ks=ks; st->shininess=shininess;
}

/** Resolve draw-slot or color-id albedo into rgba + use_uniform. */
static void render_resolve_color(scene *s, int colorid, float rgba[4], int *use_uniform) {
    rgba[0]=rgba[1]=rgba[2]=rgba[3]=1.0f;
    *use_uniform=0;
    if (colorid==SCENE_EMPTY) return;
    gcolor *color = scene_getcolorfromid(s, colorid);
    if (!color) return;
    int ncomp = (color->components==4) ? 4 : 3;
    for (int k=0; k<3; k++) rgba[k]=s->data.data[color->indx+k];
    rgba[3]=(ncomp==4) ? s->data.data[color->indx+3] : 1.0f;
    *use_uniform=1;
}

/** Pack text instructions for one TEXT draw. */
void render_preparetext(renderer *r, scene *s, gdraw *drw, renderpackstate *st) {
    /* Per-draw-slot uniform albedo when stamped (Phase 5f). */
    if (drw->colorid != SCENE_EMPTY) {
        float rgba[4];
        int use_uniform;
        render_resolve_color(s, drw->colorid, rgba, &use_uniform);
        if (use_uniform) render_pack_color(r, st, rgba, use_uniform, NULL);
    }

    /* Change the model matrix if provided */
    if (drw->matindx!=SCENE_EMPTY) {
        renderinstruction ins = { .instruction = RMODEL,
                                  .data.model.model = &s->data.data[drw->matindx],
                                  .obj=NULL };
        varray_renderinstructionwrite(&r->renderlist, ins);
    }
    
    gtext *txt = &s->textlist.data[drw->id];
    
    // Store font id in the render list
    textfont *font = scene_getfontfromid(s, txt->fontid);
    if (!font) return;
    
    int rfontid;
    for (rfontid=0; rfontid<r->fonts.count; rfontid++) {
        if (r->fonts.data[rfontid].font==font) break;
    }
    if (rfontid>=r->fonts.count) return;
    
    renderinstruction ins = { .instruction = RTEXT,
                              .data.text.txt = txt->text,
                              .data.text.rfontid = rfontid,
                              .obj = NULL };

    varray_renderinstructionwrite(&r->renderlist, ins);
}

/** Draw a text string with the given font. */
void render_rendertext(renderer *r, int rfontid, char *text) {
    textglyph glyph;
    float x=0.0, y=0.0, z=0.0;
    
    renderfont *font = &r->fonts.data[rfontid];
    
    float scale = TEXT_WORLD_SCALE;

    glBindTexture(GL_TEXTURE_2D, font->texture);
    
    for (char *c = text, *next; *c!='\0'; c=next) {
        if (!text_findglyph(font->font, c, &glyph, &next)) return;
        
        float xpos = x + glyph.bearingx * scale;
        float ypos = y - (glyph.height - glyph.bearingy) * scale;
        float w = glyph.width * scale;
        float h = glyph.height * scale;
        
        float txpos = (float) glyph.x/(float) font->font->skyline.width;
        float typos = (float) glyph.y/(float) font->font->skyline.height;
        float tw = (float) glyph.width/(float) font->font->skyline.width;
        float th = (float) glyph.height/(float) font->font->skyline.height;
        
        float vertices[6][5] = {
                    { xpos,     ypos,     z, txpos,      typos + th },
                    { xpos,     ypos + h, z, txpos,      typos      },
                    { xpos + w, ypos + h, z, txpos + tw, typos      },

                    { xpos,     ypos,     z, txpos,      typos + th },
                    { xpos + w, ypos + h, z, txpos + tw, typos      },
                    { xpos + w, ypos,     z, txpos + tw, typos + th }
                };
        
        glBindBuffer(GL_ARRAY_BUFFER, r->fontvbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        
        // render quad
        glDrawArrays(GL_TRIANGLES, 0, 6) ;
        
        x += (glyph.advance >> 6) * scale; // bitshift by 6 to get value in pixels (2^6 = 64)
        z += 1e-4; // Advance z by a tiny amount so that successive glyphs are drawn over previous glyphs (for languages where overlapping glyphs exist)
    }
}

/** Draw a font atlas (debug). */
void render_renderfonttextureatlas(renderer *r, int rfontid) {
    renderfont *font = &r->fonts.data[rfontid];
    
    glBindTexture(GL_TEXTURE_2D, font->texture);
    
    float xpos = 0, ypos = 0, w = 20, h = 20;
    float txpos = 0, typos = 0, tw = 1, th = 1;
    
    float vertices[6][5] = {
                { xpos,     ypos,     0.0f, txpos,      typos + th },
                { xpos,     ypos + h, 0.0f, txpos,      typos      },
                { xpos + w, ypos + h, 0.0f, txpos + tw, typos      },

                { xpos,     ypos,     0.0f, txpos,      typos + th },
                { xpos + w, ypos + h, 0.0f, txpos + tw, typos      },
                { xpos + w, ypos,     0.0f, txpos + tw, typos + th }
            };
    
    glBindBuffer(GL_ARRAY_BUFFER, r->fontvbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
    // render quad
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

/* -------------------------------------------------------
 * Object rendering
 * ------------------------------------------------------- */

/** Find a render object by scene gobject pointer. */
renderobject *render_findrenderobject(varray_renderobject *list, gobject *obj) {
    for (unsigned int i=0; i<list->count; i++) {
        if (list->data[i].obj==obj) return &list->data[i];
    }
    return NULL;
}

/** Find a render object by scene object id. */
renderobject *render_findrenderobjectwithid(varray_renderobject *list, int id) {
    for (unsigned int i=0; i<list->count; i++) {
        if (list->data[i].obj->id==id) return &list->data[i];
    }
    return NULL;
}

/** Replace vertex floats in an existing VBO (same length only). */
bool render_updateobjectvertices(renderer *r, scene *s, int objectid) {
    if (!r || !s) return false;
    renderobject *robj = render_findrenderobjectwithid(&r->objects, objectid);
    if (!robj || !robj->obj || robj->bufferindex < 0) return false;
    if (robj->bufferindex >= (int) r->glbuffers.count) return false;
    if (robj->obj->vertexdata.indx == SCENE_EMPTY || robj->obj->vertexdata.length <= 0)
        return false;

    renderglbuffers *b = &r->glbuffers.data[robj->bufferindex];
    if (!b->buffer) return false;

    glBindBuffer(GL_ARRAY_BUFFER, b->buffer);
    glBufferSubData(GL_ARRAY_BUFFER,
                    sizeof(GLfloat) * robj->voffset,
                    sizeof(GLfloat) * (size_t) robj->obj->vertexdata.length,
                    s->data.data + robj->obj->vertexdata.indx);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

/** Add obj to the render list if it is not already present. */
renderobject *render_addobject(varray_renderobject *list, gobject *obj) {
    renderobject *out = render_findrenderobject(list, obj);
    if (!out) {
        renderobject robj = { .obj = obj, .bufferindex = -1, .voffset = 0, .eoffset = 0 };
        if (varray_renderobjectadd(list, &robj, 1)) {
            out = &list->data[list->count-1];
        }
    }
    return out;
}

/** Allocate a dedicated VAO/VBO/EBO for an object. */
void render_addobjecttoglbuffer(varray_renderglbuffers *list, renderobject *robj) {
    if (!robj || robj->bufferindex>=0) return;

    renderglbuffers new = {
        .format = robj->obj->vertexdata.format,
        .array = 0, .buffer = 0, .element = 0,
        .vlength = 0, .elength = 0
    };
    if (!varray_renderglbuffersadd(list, &new, 1)) return;

    int bindex = (int) list->count - 1;
    renderglbuffers *buffer = &list->data[bindex];
    robj->bufferindex = bindex;
    robj->voffset = 0;
    buffer->vlength = robj->obj->vertexdata.length;
    robj->eoffset = 0;
    for (unsigned int i=0; i<robj->obj->elements.count; i++) {
        buffer->elength += robj->obj->elements.data[i].length;
    }
}

/** Vertex float stride from a format string. */
int render_entrysizefromformat(scene *s, char *format) {
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

/** Flip facet windings so geometric normal agrees with averaged vertex normals. */
static void render_orient_facets(scene *s, gobject *obj, gelement *el) {
    char *fmt;
    int stride, xpos=-1, npos=-1, pos=0;
    if (!s || !obj || !el || el->type!=FACETS || el->length<3) return;
    if (!(fmt=obj->vertexdata.format) || obj->vertexdata.indx==SCENE_EMPTY) return;
    if (!strchr(fmt, 'x') || !strchr(fmt, 'n')) return;
    if ((stride=render_entrysizefromformat(s, fmt))<=0) return;

    for (; *fmt; fmt++) {
        if (*fmt=='x') { xpos=pos; pos+=s->dim; }
        else if (*fmt=='n') { npos=pos; pos+=s->dim; }
        else if (*fmt=='c') pos+=3;
        else if (*fmt=='a') pos+=1;
    }
    if (xpos<0 || npos<0) return;

    float *vbase=&s->data.data[obj->vertexdata.indx];
    int *idx=&s->indx.data[el->indx];
    int dim=s->dim;

    for (int t=0; t+2<el->length; t+=3) {
        int i0=idx[t], i1=idx[t+1], i2=idx[t+2];
        float *p0=vbase+i0*stride+xpos, *p1=vbase+i1*stride+xpos, *p2=vbase+i2*stride+xpos;
        float *n0=vbase+i0*stride+npos, *n1=vbase+i1*stride+npos, *n2=vbase+i2*stride+npos;

        vec3 e1={ p1[0]-p0[0], p1[1]-p0[1], dim>2 ? p1[2]-p0[2] : 0 };
        vec3 e2={ p2[0]-p0[0], p2[1]-p0[1], dim>2 ? p2[2]-p0[2] : 0 };
        vec3 geom={ e1[1]*e2[2]-e1[2]*e2[1],
                    e1[2]*e2[0]-e1[0]*e2[2],
                    e1[0]*e2[1]-e1[1]*e2[0] };
        vec3 nsum={ n0[0]+n1[0]+n2[0], n0[1]+n1[1]+n2[1],
                    dim>2 ? n0[2]+n1[2]+n2[2] : 0 };

        if (geom[0]*nsum[0] + geom[1]*nsum[1] + geom[2]*nsum[2] < 0) {
            idx[t+1]=i2;
            idx[t+2]=i1;
        }
    }
}

/** Upload objects that share GL buffer i. */
void render_drawobject(renderer *r, scene *s, unsigned int i) {
    renderglbuffers *b = &r->glbuffers.data[i];
    int entrysize = render_entrysizefromformat(s, b->format);
    
    glGenVertexArrays(1, &b->array);
    glGenBuffers(1, &b->buffer);
    glGenBuffers(1, &b->element);
    
    glBindVertexArray(b->array);
    
    glBindBuffer(GL_ARRAY_BUFFER, b->buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat)*b->vlength, NULL, GL_STATIC_DRAW);
    
    /* Copy all the object data into the buffer */
    for (unsigned int j=0; j<r->objects.count; j++) {
        renderobject *obj = &r->objects.data[j];
        if (obj && obj->bufferindex==(int) i) {
            glBufferSubData( GL_ARRAY_BUFFER,
                            sizeof(GLfloat)*obj->voffset,
                            sizeof(GLfloat)*obj->obj->vertexdata.length,
                            s->data.data+obj->obj->vertexdata.indx);
        }
    }
    
    unsigned int offset = 0;
    bool have_c=false, have_n=false, have_a=false;
    for (unsigned int j=0; b->format[j]!='\0'; j++) {
        if (b->format[j]=='x') {
            glVertexAttribPointer(0, s->dim, GL_FLOAT, GL_FALSE, sizeof(GLfloat)*entrysize, (void*) (sizeof(GLfloat)*offset));
            glEnableVertexAttribArray(0);
            offset += s->dim;
        } else if (b->format[j]=='c') {
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GLfloat)*entrysize, (void*) (sizeof(GLfloat)*offset));
            glEnableVertexAttribArray(1);
            have_c=true;
            offset += 3;
        } else if (b->format[j]=='n') {
            glVertexAttribPointer(2, s->dim, GL_FLOAT, GL_FALSE, sizeof(GLfloat)*entrysize, (void*) (sizeof(GLfloat)*offset));
            glEnableVertexAttribArray(2);
            have_n=true;
            offset += s->dim;
        } else if (b->format[j]=='a') {
            glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(GLfloat)*entrysize, (void*) (sizeof(GLfloat)*offset));
            glEnableVertexAttribArray(3);
            have_a=true;
            offset += 1;
        }
    }
    /* Disable unused attribs; draw paths set per-draw generics after bind. */
    if (!have_c) {
        glDisableVertexAttribArray(1);
        glVertexAttrib3f(1, 1.0f, 1.0f, 1.0f);
    }
    if (!have_n) {
        glDisableVertexAttribArray(2);
        glVertexAttrib3f(2, 0.0f, 0.0f, 1.0f);
    }
    if (!have_a) {
        glDisableVertexAttribArray(3);
        glVertexAttrib1f(3, 1.0f);
    }
    
    /* Unbind vertex array buffer */
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
    /* Now for the element array buffer */
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b->element);
    /* Size the element buffer */
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(GLuint)*b->elength, NULL, GL_STATIC_DRAW);
    
    /* Copy all the object element data into the buffer */
    for (unsigned int j=0; j<r->objects.count; j++) {
        renderobject *obj = &r->objects.data[j];
        
        if (obj->bufferindex==(int) i) {
            int eoff = obj->eoffset;
            
            /* Loop over elements */
            for (unsigned int k=0; k<obj->obj->elements.count; k++) {
                gelement *el=&obj->obj->elements.data[k];

                if (el->type==FACETS) render_orient_facets(s, obj->obj, el);
                
                /* Offset the vertex indices by the vertex offset */
                if (obj->voffset>0) for (unsigned int m=0; m<el->length; m++) {
                    s->indx.data[el->indx+m] += obj->voffset/entrysize;
                }
                
                /* Copy the vertex indices over */
                glBufferSubData( GL_ELEMENT_ARRAY_BUFFER,
                                sizeof(GLuint)*eoff,
                                sizeof(GLuint)*el->length,
                                s->indx.data+el->indx);
                
                /* Restore vertex indices */
                if (obj->voffset>0) for (unsigned int m=0; m<el->length; m++) {
                    s->indx.data[el->indx+m] -= obj->voffset/entrysize;
                }
                
                eoff+=el->length;
            }
        }
    }
    
    glBindVertexArray(0);
}

/** Pack renderlist instructions for one OBJECT draw. */
void render_prepareobject(renderer *r, scene *s, gdraw *drw, renderpackstate *st) {
    renderobject *obj = render_findrenderobjectwithid(&r->objects, drw->id);
    if (!obj || !obj->obj || obj->bufferindex<0 ||
        (unsigned) obj->bufferindex>=r->glbuffers.count) return;

    renderglbuffers *buf = &r->glbuffers.data[obj->bufferindex];

    render_pack_array(r, st, buf->array, obj);

    /* Emit RCOLOR when the slot's effective color differs from the last packed one. */
    {
        float rgba[4];
        int use_uniform;
        render_resolve_color(s, drw->colorid, rgba, &use_uniform);
        render_pack_color(r, st, rgba, use_uniform, obj);
    }

    /* Change the model matrix if provided */
    if (drw->matindx!=SCENE_EMPTY) {
        renderinstruction mins = { .instruction = RMODEL,
                                   .data.model.model = &s->data.data[drw->matindx],
                                   .obj=obj };
        varray_renderinstructionadd(&r->renderlist, &mins, 1);
    }

    /* Now loop over the elements in the object */
    int offset=obj->eoffset;
    for (unsigned int j=0; j<obj->obj->elements.count; j++) {
        gelement *el = &obj->obj->elements.data[j];
        renderinstruction eins = { .instruction = RNOP, .obj=obj};

        switch (el->type) {
            case FACETS:
                eins.instruction=RTRIANGLES;
                eins.data.triangles.offset=(void *) (sizeof(GLuint)*offset);
                eins.data.triangles.length=el->length;
                offset+=el->length;
                break;
            case LINES:
                eins.instruction=RLINES;
                eins.data.triangles.offset=(void *) (sizeof(GLuint)*offset);
                eins.data.triangles.length=el->length;
                offset+=el->length;
                break;
            case POINTS:
                eins.instruction=RPOINTS;
                eins.data.triangles.offset=(void *) (sizeof(GLuint)*offset);
                eins.data.triangles.length=el->length;
                offset+=el->length;
                break;
            default:
                break;
        }

        if (eins.instruction!=RNOP) varray_renderinstructionadd(&r->renderlist, &eins, 1);
    }
}

/* -------------------------------------------------------
 * Prepare scene
 * ------------------------------------------------------- */

/** Build GL buffers and the renderlist for a scene. */
void render_preparescene(renderer *r, scene *s) {
    render_preparefonts(r, s);
    
    /* Loop over the display list to identify objects */
    for (unsigned int i=0; i<s->displaylist.count; i++) {
        gdraw *drw=&s->displaylist.data[i];
        switch (drw->type) {
            case OBJECT:
            {
                renderobject *robj = NULL;
                
                /* Add the object to the scene if not already present */
                gobject *obj = scene_getgobjectfromid(s, s->displaylist.data[i].id);
                if (obj) robj=render_addobject(&r->objects, obj);
                
                /* Add vertex data to a suitable vertex buffer, or create one if necessary */
                if (robj) render_addobjecttoglbuffer(&r->glbuffers, robj);
            }
                break;
            default:
                break;
        }
    }
    
    /* Now allocate OpenGL buffers and arrays */
    for (unsigned int i=0; i<r->glbuffers.count; i++) {
        render_drawobject(r, s, i);
    }
    
    /* Now create the object render list */
    renderpackstate pack;
    render_packstate_reset(&pack);
    for (unsigned int i=0; i<s->displaylist.count; i++) {
        gdraw *drw=&s->displaylist.data[i];
        switch (drw->type) {
            case OBJECT:
                render_prepareobject(r, s, drw, &pack);
                break;
            case TEXT:
                render_preparetext(r, s, drw, &pack);
                break;
            case COLOR:
            {
                float rgba[4];
                int use_uniform;
                render_resolve_color(s, drw->id, rgba, &use_uniform);
                if (use_uniform) {
                    render_pack_color(r, &pack, rgba, use_uniform, NULL);
                } else {
                    printf("Color %i not found.\n", drw->id);
                }
            }
                break;
            case SHADE:
            {
                float ka=SCENE_MATERIAL_KA_DEFAULT;
                float kd=SCENE_MATERIAL_KD_DEFAULT;
                float ks=SCENE_MATERIAL_KS_DEFAULT;
                float shininess=SCENE_MATERIAL_SHININESS_DEFAULT;
                if (drw->matindx!=SCENE_EMPTY) {
                    float *m=&s->data.data[drw->matindx];
                    ka=m[0]; kd=m[1]; ks=m[2]; shininess=m[3];
                }
                render_pack_shade(r, &pack, drw->id, ka, kd, ks, shininess);
            }
                break;
        }
    }
}

/* -------------------------------------------------------
 * Render the scene
 * ------------------------------------------------------- */

/** Upload model and derived normalMatrix. */
static void render_setmodel(renderer *r, mat4x4 model) {
    mat4x4 vm;
    mat3x3 nmat;
    mat3d_mul4x4(r->frameview, model, vm);
    render_normalmatrix(vm, nmat);
    glUniformMatrix4fv(r->uniforms.model, 1, GL_FALSE, model);
    glUniformMatrix3fv(r->uniforms.normalMatrix, 1, GL_FALSE, nmat);
}

/** Bind view and projection uniforms. */
static void render_bind_viewproj(renderer *r, mat4x4 view, mat4x4 proj) {
    glUseProgram(r->shader);
    glUniformMatrix4fv(r->uniforms.view, 1, GL_FALSE, view);
    glUniformMatrix4fv(r->uniforms.proj, 1, GL_FALSE, proj);
}

/** Upload geometry-program uniforms (lights already bound for the frame). */
static void render_setgeometryuniforms(renderer *r, mat4x4 view, mat4x4 proj,
                                       mat4x4 model, float *ucolor, int use_uniform, int uflat,
                                       float ka, float kd, float ks, float shininess) {
    renderuniforms *u = &r->uniforms;
    glUseProgram(r->shader);
    glUniformMatrix4fv(u->view, 1, GL_FALSE, view);
    glUniformMatrix4fv(u->proj, 1, GL_FALSE, proj);
    render_setmodel(r, model);
    glUniform4fv(u->uColor, 1, ucolor);
    glUniform1i(u->uUseUniform, use_uniform);
    glUniform1i(u->uFlat, uflat);
    glUniform1f(u->ka, ka);
    glUniform1f(u->kd, kd);
    glUniform1f(u->ks, ks);
    glUniform1f(u->shininess, shininess);
}

#define RENDER_OPAQUE_ALPHA_EPS 0.999f

/** True if this draw should go in the transparent pass. */
static bool render_is_transparent(int use_uniform, float alpha, const char *format) {
    if (format && strchr(format, 'a')) return true;
    float a = (use_uniform!=0) ? alpha : 1.0f;
    return (a < RENDER_OPAQUE_ALPHA_EPS);
}

/** Mutable geometry state while walking the render list. */
typedef struct {
    mat4x4 model;
    float ka;
    float kd;
    float ks;
    float shininess;
    float ucolor[4];
    int use_uniform;
    int uflat;
    GLuint curvao;
} rendergeostate;

static void render_geostate_reset(rendergeostate *st) {
    mat3d_identity4x4(st->model);
    st->ka=SCENE_MATERIAL_KA_DEFAULT;
    st->kd=SCENE_MATERIAL_KD_DEFAULT;
    st->ks=SCENE_MATERIAL_KS_DEFAULT;
    st->shininess=SCENE_MATERIAL_SHININESS_DEFAULT;
    st->ucolor[0]=st->ucolor[1]=st->ucolor[2]=st->ucolor[3]=1.0f;
    st->use_uniform=0;
    st->uflat=0;
    st->curvao=0;
}

/** Set generic color/alpha for meshes with those arrays disabled (`xn`, `x`). */
static void render_set_fallback_attribs(const float rgba[4]) {
    glVertexAttrib3f(1, rgba[0], rgba[1], rgba[2]);
    glVertexAttrib3f(2, 0.0f, 0.0f, 1.0f);
    glVertexAttrib1f(3, rgba[3]);
}

/** Local-space AABB center of object positions.
 * @param[in] s - scene vertex store
 * @param[in,out] obj - caches centroid until geometry changes
 * @param[out] out - centroid
 * @returns false if no usable vertices */
static bool render_object_centroid(scene *s, gobject *obj, vec3 out) {
    if (!s || !obj || !obj->vertexdata.format || !strchr(obj->vertexdata.format, 'x')) return false;
    if (obj->vertexdata.indx==SCENE_EMPTY || obj->vertexdata.length<=0) return false;

    if (obj->centroid_valid) {
        out[0]=obj->centroid[0];
        out[1]=obj->centroid[1];
        out[2]=obj->centroid[2];
        return true;
    }

    int stride=render_entrysizefromformat(s, obj->vertexdata.format);
    if (stride<=0) return false;

    int xpos=0;
    for (char *c=obj->vertexdata.format; *c!='\0' && *c!='x'; c++) {
        if (*c=='n') xpos+=s->dim;
        else if (*c=='c') xpos+=3;
        else if (*c=='a') xpos+=1;
    }

    float bbox[6];
    bool any=false;
    int nvert=obj->vertexdata.length/stride;
    for (int v=0; v<nvert; v++) {
        float *base=&s->data.data[obj->vertexdata.indx + v*stride + xpos];
        float x=base[0];
        float y=(s->dim>1) ? base[1] : 0.0f;
        float z=(s->dim>2) ? base[2] : 0.0f;
        if (!any) {
            bbox[0]=bbox[1]=x; bbox[2]=bbox[3]=y; bbox[4]=bbox[5]=z;
            any=true;
        } else {
            if (x<bbox[0]) bbox[0]=x; if (x>bbox[1]) bbox[1]=x;
            if (y<bbox[2]) bbox[2]=y; if (y>bbox[3]) bbox[3]=y;
            if (z<bbox[4]) bbox[4]=z; if (z>bbox[5]) bbox[5]=z;
        }
    }
    if (!any) return false;

    obj->centroid[0]=0.5f*(bbox[0]+bbox[1]);
    obj->centroid[1]=0.5f*(bbox[2]+bbox[3]);
    obj->centroid[2]=0.5f*(bbox[4]+bbox[5]);
    obj->centroid_valid=true;
    out[0]=obj->centroid[0];
    out[1]=obj->centroid[1];
    out[2]=obj->centroid[2];
    return true;
}

/** View-space z of model*local (column-major mat4). */
static float render_view_depth(mat4x4 view, mat4x4 model, vec3 local) {
    float wx=model[0]*local[0] + model[4]*local[1] + model[8]*local[2]  + model[12];
    float wy=model[1]*local[0] + model[5]*local[1] + model[9]*local[2]  + model[13];
    float wz=model[2]*local[0] + model[6]*local[1] + model[10]*local[2] + model[14];
    return view[2]*wx + view[6]*wy + view[10]*wz + view[14];
}

static int render_tdraw_cmp(const void *a, const void *b) {
    float da=((const rendertdraw *)a)->depth;
    float db=((const rendertdraw *)b)->depth;
    return (da<db) ? -1 : (da>db) ? 1 : 0;
}

/** Draw a transparent packet; closed meshes get back faces then front. */
static void render_draw_tdraw(renderer *r, rendertdraw *d) {
    glUseProgram(r->shader);
    render_setmodel(r, d->model);
    glUniform4fv(r->uniforms.uColor, 1, d->rgba);
    glUniform1i(r->uniforms.uUseUniform, d->use_uniform);
    glUniform1i(r->uniforms.uFlat, d->uflat);
    glUniform1f(r->uniforms.ka, d->ka);
    glUniform1f(r->uniforms.kd, d->kd);
    glUniform1f(r->uniforms.ks, d->ks);
    glUniform1f(r->uniforms.shininess, d->shininess);
    if (!d->vao) return;
    glBindVertexArray(d->vao);
    render_set_fallback_attribs(d->rgba);
    if (d->mode==GL_TRIANGLES) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        glDrawElements(GL_TRIANGLES, d->length, GL_UNSIGNED_INT, d->offset);
        glCullFace(GL_BACK);
        glDrawElements(GL_TRIANGLES, d->length, GL_UNSIGNED_INT, d->offset);
        glDisable(GL_CULL_FACE);
    } else {
        glDrawElements(d->mode, d->length, GL_UNSIGNED_INT, d->offset);
    }
}

/** Append one transparent draw to the scratch list.
 * @returns false on allocation failure */
static bool render_collect_tdraw(renderer *r, scene *s, mat4x4 view, rendergeostate *st,
                                 renderinstruction *ins) {
    rendertdraw d;
    memset(&d, 0, sizeof(d));
    d.mode=(ins->instruction==RTRIANGLES) ? GL_TRIANGLES :
           (ins->instruction==RLINES) ? GL_LINES : GL_POINTS;
    d.length=ins->data.triangles.length;
    d.offset=ins->data.triangles.offset;
    d.vao=st->curvao;
    memcpy(d.model, st->model, sizeof(mat4x4));
    memcpy(d.rgba, st->ucolor, sizeof(d.rgba));
    d.use_uniform=st->use_uniform;
    d.uflat=st->uflat;
    d.ka=st->ka; d.kd=st->kd; d.ks=st->ks; d.shininess=st->shininess;
    d.depth=0.0f;
    if (s && ins->obj && ins->obj->obj) {
        vec3 local={0,0,0};
        if (render_object_centroid(s, ins->obj->obj, local))
            d.depth=render_view_depth(view, st->model, local);
    }
    return varray_rendertdrawadd(&r->tdraws, &d, 1);
}

typedef enum {
    RENDER_PASS_OPAQUE=0,          /* draw opaque; skip transparent */
    RENDER_PASS_COLLECT_TRANSPARENT /* bake transparent into r->tdraws */
} rendergeopass;

/** Walk the geometry renderlist, drawing opaque or collecting transparent. */
static bool render_walk_geometry(renderer *r, scene *s, mat4x4 view, mat4x4 proj,
                                 rendergeopass pass) {
    rendergeostate st;
    render_geostate_reset(&st);

    if (pass==RENDER_PASS_OPAQUE) {
        render_setgeometryuniforms(r, view, proj, st.model,
                                   st.ucolor, st.use_uniform, st.uflat,
                                   st.ka, st.kd, st.ks, st.shininess);
    } else {
        r->tdraws.count=0;
    }

    for (unsigned i=0; i<r->renderlist.count; i++) {
        renderinstruction *ins=&r->renderlist.data[i];
        switch (ins->instruction) {
            case RNOP:
            case RTEXT:
                break;
            case RSHADE:
                st.ka=ins->data.shade.ka;
                st.kd=ins->data.shade.kd;
                st.ks=ins->data.shade.ks;
                st.shininess=ins->data.shade.shininess;
                st.uflat=(ins->data.shade.mode==SCENE_SHADE_FLAT) ? 1 : 0;
                if (pass==RENDER_PASS_OPAQUE)
                    render_setgeometryuniforms(r, view, proj, st.model,
                                               st.ucolor, st.use_uniform, st.uflat,
                                               st.ka, st.kd, st.ks, st.shininess);
                break;
            case RCOLOR:
                st.ucolor[0]=ins->data.color.rgba[0];
                st.ucolor[1]=ins->data.color.rgba[1];
                st.ucolor[2]=ins->data.color.rgba[2];
                st.ucolor[3]=ins->data.color.rgba[3];
                st.use_uniform=ins->data.color.use_uniform;
                if (pass==RENDER_PASS_OPAQUE) {
                    glUniform4fv(r->uniforms.uColor, 1, st.ucolor);
                    glUniform1i(r->uniforms.uUseUniform, st.use_uniform);
                }
                break;
            case RMODEL:
                memcpy(st.model, ins->data.model.model, sizeof(mat4x4));
                if (pass==RENDER_PASS_OPAQUE) render_setmodel(r, st.model);
                break;
            case RARRAY:
                st.curvao=ins->data.array.handle;
                if (pass==RENDER_PASS_OPAQUE) glBindVertexArray(st.curvao);
                break;
            case RTRIANGLES:
            case RLINES:
            case RPOINTS: {
                /* Color state is the last packed RCOLOR for this object. */
                int use_uniform = st.use_uniform;
                float alpha = st.ucolor[3];
                const char *fmt = (ins->obj && ins->obj->obj) ? ins->obj->obj->vertexdata.format : NULL;
                bool trans=render_is_transparent(use_uniform, alpha, fmt);
                if (pass==RENDER_PASS_OPAQUE && !trans) {
                    /* Force GPU color even if a prior uniform C left uUseUniform set. */
                    glUniform1i(r->uniforms.uFlat, st.uflat);
                    glUniform1i(r->uniforms.uUseUniform, use_uniform);
                    if (use_uniform==0) {
                        float one[4]={1.0f,1.0f,1.0f,1.0f};
                        glUniform4fv(r->uniforms.uColor, 1, one);
                        render_set_fallback_attribs(one);
                    } else {
                        glUniform4fv(r->uniforms.uColor, 1, st.ucolor);
                        render_set_fallback_attribs(st.ucolor);
                    }
                    GLenum mode=(ins->instruction==RTRIANGLES) ? GL_TRIANGLES :
                                (ins->instruction==RLINES) ? GL_LINES : GL_POINTS;
                    glDrawElements(mode, ins->data.triangles.length, GL_UNSIGNED_INT,
                                   ins->data.triangles.offset);
                } else if (pass==RENDER_PASS_COLLECT_TRANSPARENT && trans) {
                    int saved=st.use_uniform;
                    float saveda=st.ucolor[3];
                    st.use_uniform=use_uniform;
                    st.ucolor[3]=alpha;
                    if (!render_collect_tdraw(r, s, view, &st, ins)) {
                        st.use_uniform=saved;
                        st.ucolor[3]=saveda;
                        return false;
                    }
                    st.use_uniform=saved;
                    st.ucolor[3]=saveda;
                }
                break;
            }
        }
    }
    return true;
}

/** Draw one frame: opaque pass, sorted transparent pass, then text. */
void render_render(renderer *r, float aspectratio, mat4x4 view, float near, float far, scene *s) {
    /* Clear the display */
    if (s) {
        glClearColor(s->background[0], s->background[1], s->background[2], 1.0f);
    } else {
        glClearColor(SCENE_BACKGROUND_R_DEFAULT, SCENE_BACKGROUND_G_DEFAULT,
                     SCENE_BACKGROUND_B_DEFAULT, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    mat3d_copy4x4(view, r->frameview);
    render_uploadlights(r, s, view);

    mat4x4 proj;
    mat3d_ortho(NULL, proj, -1.0*aspectratio, 1.0*aspectratio, -1.0, 1.0, near, far);

    mat4x4 model;
    mat3d_identity4x4(model);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* --- Opaque pass: display-list order, depth write on, no blending --- */
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    render_walk_geometry(r, s, view, proj, RENDER_PASS_OPAQUE);

    /* --- Transparent pass: collect, sort far→near by centroid view-z, draw --- */
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    if (render_walk_geometry(r, s, view, proj, RENDER_PASS_COLLECT_TRANSPARENT) &&
        r->tdraws.count>0) {
        qsort(r->tdraws.data, r->tdraws.count, sizeof(rendertdraw), render_tdraw_cmp);
        render_bind_viewproj(r, view, proj);
        for (unsigned i=0; i<r->tdraws.count; i++) render_draw_tdraw(r, &r->tdraws.data[i]);
    }

    glDepthMask(GL_TRUE);
    glEnable(GL_BLEND);
    
    /* Text rendering pass */
    glUseProgram(r->textshader);

    float textcolor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glUniform4fv(r->textuniforms.textColor, 1, textcolor);
    glUniformMatrix4fv(r->textuniforms.view, 1, GL_FALSE, view);
    glUniformMatrix4fv(r->textuniforms.proj, 1, GL_FALSE, proj);

    mat3d_identity4x4(model);
    glUniformMatrix4fv(r->textuniforms.model, 1, GL_FALSE, model);
    
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(r->fontvao);
    
    for (unsigned i=0; i<r->renderlist.count; i++) {
        renderinstruction *ins=&r->renderlist.data[i];
        switch (ins->instruction) {
            case RMODEL:
                glUniformMatrix4fv(r->textuniforms.model, 1, GL_FALSE, ins->data.model.model);
                break;
            case RTEXT:
                render_rendertext(r, ins->data.text.rfontid, ins->data.text.txt);
                break;
            case RCOLOR:
                glUniform4fv(r->textuniforms.textColor, 1, ins->data.color.rgba);
                break;
            default:
                break;
        }
    }
    
    GLenum er = glGetError();
    if (er!=0) {
        fprintf(stderr, "OpenGL error %u\n",er);
    }
    
    glBindVertexArray(0);
}
