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
 * Lighting and normals in model space; normalMatrix supplied from CPU. */

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
    "   gl_Position = proj * view * model * vec4(vPos, 1.0);\n"
    "   fragColor = vColor;\n"
    "   fragPos = vec3(model * vec4(vPos, 1.0));\n"
    "   normal = normalMatrix * vNormal;\n"
    "   fragAlpha = vAlpha;\n"
    "}\n";

const char *fragmentshader =
    "#version 330 core\n"
    "out vec4 FragColor;\n"
    "in vec3 fragColor;\n"
    "in vec3 fragPos;\n"
    "in vec3 normal;\n"
    "in float fragAlpha;\n"
    "uniform vec3 lightColor;\n"
    "uniform vec3 lightPos;\n"
    "uniform vec3 viewPos;\n"
    "uniform vec4 uColor;\n"
    "uniform int uUseUniform;\n"
    "uniform int uFlat;\n"
    "uniform float ka;\n"
    "uniform float kd;\n"
    "uniform float ks;\n"
    "uniform float shininess;\n"
    "\n"
    "void main() {\n"
    "   vec3 albedo = (uUseUniform != 0) ? uColor.rgb : fragColor;\n"
    "   float alpha = ((uUseUniform != 0) ? uColor.a : 1.0) * fragAlpha;\n"
    "   if (uFlat != 0) {\n"
    "       FragColor = vec4(albedo, alpha);\n"
    "       return;\n"
    "   }\n"
    "   vec3 norm = normalize(normal);\n"
    "   if (!gl_FrontFacing) norm = -norm;\n"
    "   vec3 lightDir = normalize(lightPos - fragPos);\n"
    "   float NdotL = max(dot(norm, lightDir), 0.0);\n"
    "   vec3 ambient = ka * lightColor;\n"
    "   vec3 diffuse = kd * NdotL * lightColor;\n"
    "   vec3 viewDir = normalize(viewPos - fragPos);\n"
    "   vec3 reflectDir = reflect(-lightDir, norm);\n"
    "   float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);\n"
    "   vec3 specular = ks * spec * lightColor;\n"
    "   vec3 result = (ambient + diffuse + specular) * albedo;\n"
    "   FragColor = vec4(result, alpha);\n"
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

/** Compiles and links shaders
 * @param[in] vertexshadersource - vertex shader
 * @param[in] fragmentshadersource - fragment shader
 * @param[out] program - compiled program id
 * @returns true on success, false if compilation failed */
bool render_compileprogram(const char *vertexshadersource, const char *fragmentshadersource, GLuint *program) {
    int success;
    
    /* Create and compile vertex shader */
    unsigned int vertexshader;
    vertexshader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexshader, 1, &vertexshadersource, NULL);
    glCompileShader(vertexshader);
    
    /* Check shader compilation was successful */
    char infoLog[512];
    glGetShaderiv(vertexshader, GL_COMPILE_STATUS, &success);
    if(!success) {
        glGetShaderInfoLog(vertexshader, 512, NULL, infoLog);
        fprintf(stderr, "morphoview: Vertex shader failed to compile with error '%s'\n", infoLog);
        return false;
    }
    
    /* Create and compile fragment shader */
    unsigned int fragmentshader;
    fragmentshader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentshader, 1, &fragmentshadersource, NULL);
    glCompileShader(fragmentshader);
    glGetShaderiv(fragmentshader, GL_COMPILE_STATUS, &success);
    if(!success) {
        glGetShaderInfoLog(fragmentshader, 512, NULL, infoLog);
        fprintf(stderr,"Fragment shader failed to compile with error '%s'\n", infoLog);
        return false;
    }
    
    /* Link shader program */
    unsigned int shaderProgram;
    shaderProgram = glCreateProgram();
    
    glAttachShader(shaderProgram, vertexshader);
    glAttachShader(shaderProgram, fragmentshader);
    glLinkProgram(shaderProgram);
    
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if(!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        fprintf(stderr, "Shader link failure with error '%s'\n", infoLog);
        return false;
    }
    
    if (program) *program = shaderProgram;
    
    /* Delete the compiled vertex and fragment shaders */
    glDeleteShader(vertexshader);
    glDeleteShader(fragmentshader);
    
    return true;
}

/** Cache geometry-program uniform locations after a successful link. */
static void render_cacheuniforms(renderer *r) {
    GLuint p = r->shader;
    r->uniforms.model = glGetUniformLocation(p, "model");
    r->uniforms.view = glGetUniformLocation(p, "view");
    r->uniforms.proj = glGetUniformLocation(p, "proj");
    r->uniforms.normalMatrix = glGetUniformLocation(p, "normalMatrix");
    r->uniforms.lightColor = glGetUniformLocation(p, "lightColor");
    r->uniforms.lightPos = glGetUniformLocation(p, "lightPos");
    r->uniforms.viewPos = glGetUniformLocation(p, "viewPos");
    r->uniforms.uColor = glGetUniformLocation(p, "uColor");
    r->uniforms.uUseUniform = glGetUniformLocation(p, "uUseUniform");
    r->uniforms.uFlat = glGetUniformLocation(p, "uFlat");
    r->uniforms.ka = glGetUniformLocation(p, "ka");
    r->uniforms.kd = glGetUniformLocation(p, "kd");
    r->uniforms.ks = glGetUniformLocation(p, "ks");
    r->uniforms.shininess = glGetUniformLocation(p, "shininess");
}

/** normalMatrix = transpose(inverse(upper 3x3 of model)), column-major. */
static void render_normalmatrix(mat4x4 model, mat3x3 out) {
    mat4x4 inv;
    mat3d_invert4x4(model, inv);
    out[0]=inv[0]; out[1]=inv[4]; out[2]=inv[8];
    out[3]=inv[1]; out[4]=inv[5]; out[5]=inv[9];
    out[6]=inv[2]; out[7]=inv[6]; out[8]=inv[10];
}

/* -------------------------------------------------------
 * Initialize/finalize display
 * ------------------------------------------------------- */

/** Initializes a display, compiling shaders */
bool render_init(renderer *r) {
    r->shader=0;
    r->textshader=0;
    memset(&r->uniforms, 0, sizeof(r->uniforms));

    if (!render_compileprogram(vertexshader, fragmentshader, &r->shader)) return false;
    render_cacheuniforms(r);
    if (!render_compileprogram(textvertexshader, textfragmentshader, &r->textshader)) return false;
    
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

//GLuint fonttexture;
//GLuint fontvao;
//GLuint fontvbo;

/** Creates an OpenGL texture from the texture atlas */
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

/** Prepare fonts for display */
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

/** Prepares text for display */
void render_preparetext(renderer *r, scene *s, gdraw *drw, GLuint *carray) {
    (void) carray;

    /* Per-draw-slot uniform albedo when stamped (Phase 5f). */
    if (drw->colorid != SCENE_EMPTY) {
        gcolor *color = scene_getcolorfromid(s, drw->colorid);
        if (color) {
            renderinstruction cins = { .instruction = RCOLOR, .obj = NULL };
            int ncomp = (color->components == 4) ? 4 : 3;
            for (int k = 0; k < 3; k++)
                cins.data.color.rgba[k] = s->data.data[color->indx + k];
            cins.data.color.rgba[3] =
                (ncomp == 4) ? s->data.data[color->indx + 3] : 1.0f;
            cins.data.color.use_uniform = 1;
            varray_renderinstructionwrite(&r->renderlist, cins);
        }
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

/** Draws a text element */
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

/** Renders the texture atlas for a font (for debugging purposes) */
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

/** Checks if an object is present in the render object list */
renderobject *render_findrenderobject(varray_renderobject *list, gobject *obj) {
    for (unsigned int i=0; i<list->count; i++) {
        if (list->data[i].obj==obj) return &list->data[i];
    }
    return NULL;
}

/** Finds a render object based on an object id */
renderobject *render_findrenderobjectwithid(varray_renderobject *list, int id) {
    for (unsigned int i=0; i<list->count; i++) {
        if (list->data[i].obj->id==id) return &list->data[i];
    }
    return NULL;
}

/** Same-length vertex upload into an existing VBO (no full prepare). */
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

/** Adds an id to an id list only if it is not present already */
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

/** Adds an object to its own OpenGL buffer (one VAO/VBO/EBO per object).
 *  Sharing by vertex format used to store raw pointers into a reallocating
 *  varray; one-buffer-per-object keeps lifetimes simple and isolates meshes. */
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

/** Calculate the size of vertex data given a format string */
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

/** Flip facet windings so geometric normal agrees with averaged vertex normals.
 *  Dense Matrix connectivity (and bad serializers) can lose winding; transparent
 *  back/front passes need consistent orientation. */
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

/** Draws an object to  newly allocated OpenGL buffers */
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
    for (unsigned int j=0; b->format[j]!='\0'; j++) {
        if (b->format[j]=='x') {
            glVertexAttribPointer(0, s->dim, GL_FLOAT, GL_FALSE, sizeof(GLfloat)*entrysize, (void*) (sizeof(GLfloat)*offset));
            glEnableVertexAttribArray(0);
            offset += s->dim;
        } else if (b->format[j]=='c') {
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GLfloat)*entrysize, (void*) (sizeof(GLfloat)*offset));
            glEnableVertexAttribArray(1);
            offset += 3;
        } else if (b->format[j]=='n') {
            glVertexAttribPointer(2, s->dim, GL_FLOAT, GL_FALSE, sizeof(GLfloat)*entrysize, (void*) (sizeof(GLfloat)*offset));
            glEnableVertexAttribArray(2);
            offset += s->dim;
        } else if (b->format[j]=='a') {
            glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(GLfloat)*entrysize, (void*) (sizeof(GLfloat)*offset));
            glEnableVertexAttribArray(3);
            offset += 1;
        }
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

/** Prepares an object for rendering, inserting appropriate instructions into the render list */
void render_prepareobject(renderer *r, scene *s, gdraw *drw, GLuint *carray) {
    renderobject *obj = render_findrenderobjectwithid(&r->objects, drw->id);
    if (!obj || !obj->obj || obj->bufferindex<0 ||
        (unsigned) obj->bufferindex>=r->glbuffers.count) return;

    renderglbuffers *buf = &r->glbuffers.data[obj->bufferindex];

    /* Select the vertex array — always rebind (don't skip); VAO switches between
     * `xn` (no color attrib) and `xnc` must not leave stale bindings. */
    renderinstruction ins = { .instruction = RARRAY, .data.array.handle = buf->array, .obj=obj };
    varray_renderinstructionadd(&r->renderlist, &ins, 1);
    *carray=buf->array;

    /* Per-draw albedo: `C` on the slot is a uniform override (even on `xnc`/`xc`).
     * Always emit RCOLOR so a prior translucent `C` cannot leak into the next object. */
    {
        renderinstruction cins = { .instruction = RCOLOR, .obj=obj };
        cins.data.color.rgba[0]=1.0f;
        cins.data.color.rgba[1]=1.0f;
        cins.data.color.rgba[2]=1.0f;
        cins.data.color.rgba[3]=1.0f;
        cins.data.color.use_uniform=0;
        if (drw->colorid != SCENE_EMPTY) {
            gcolor *color = scene_getcolorfromid(s, drw->colorid);
            if (color) {
                int ncomp = (color->components==4) ? 4 : 3;
                for (int k=0; k<3; k++) cins.data.color.rgba[k]=s->data.data[color->indx+k];
                cins.data.color.rgba[3]=(ncomp==4) ? s->data.data[color->indx+3] : 1.0f;
                cins.data.color.use_uniform=1;
            }
        }
        varray_renderinstructionadd(&r->renderlist, &cins, 1);
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

/** Prepares a scene for rendering */
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
    GLuint carray=0;
    for (unsigned int i=0; i<s->displaylist.count; i++) {
        gdraw *drw=&s->displaylist.data[i];
        switch (drw->type) {
            case OBJECT:
                render_prepareobject(r, s, drw, &carray);
                break;
            case TEXT:
                render_preparetext(r, s, drw, &carray);
                break;
            case COLOR:
            {
                gcolor *color = scene_getcolorfromid(s, drw->id);
                
                if (color) {
                    renderinstruction ins = { .instruction = RCOLOR };
                    int ncomp = (color->components==4) ? 4 : 3;
                    for (int k=0; k<3; k++) ins.data.color.rgba[k]=s->data.data[color->indx+k];
                    ins.data.color.rgba[3]=(ncomp==4) ? s->data.data[color->indx+3] : 1.0f;
                    ins.data.color.use_uniform=1;
                    varray_renderinstructionadd(&r->renderlist, &ins, 1);
                } else {
                    printf("Color %i not found.\n", drw->id);
                }
            }
                break;
            case SHADE:
            {
                renderinstruction ins = { .instruction = RSHADE };
                ins.data.shade.mode=drw->id;
                ins.data.shade.ka=SCENE_MATERIAL_KA_DEFAULT;
                ins.data.shade.kd=SCENE_MATERIAL_KD_DEFAULT;
                ins.data.shade.ks=SCENE_MATERIAL_KS_DEFAULT;
                ins.data.shade.shininess=SCENE_MATERIAL_SHININESS_DEFAULT;
                if (drw->matindx!=SCENE_EMPTY) {
                    float *m=&s->data.data[drw->matindx];
                    ins.data.shade.ka=m[0];
                    ins.data.shade.kd=m[1];
                    ins.data.shade.ks=m[2];
                    ins.data.shade.shininess=m[3];
                }
                varray_renderinstructionadd(&r->renderlist, &ins, 1);
            }
                break;
        }
    }
}

/* -------------------------------------------------------
 * Render the scene
 * ------------------------------------------------------- */

/** Upload model + derived normalMatrix via cached locations. */
static void render_setmodel(renderer *r, mat4x4 model) {
    mat3x3 nmat;
    render_normalmatrix(model, nmat);
    glUniformMatrix4fv(r->uniforms.model, 1, GL_FALSE, model);
    glUniformMatrix3fv(r->uniforms.normalMatrix, 1, GL_FALSE, nmat);
}

/** Upload uniforms for the geometry program. */
static void render_setgeometryuniforms(renderer *r, mat4x4 view, mat4x4 proj,
                                       mat4x4 model, vec3 lightcolor, vec3 lightposn, vec3 viewposn,
                                       float *ucolor, int use_uniform, int uflat,
                                       float ka, float kd, float ks, float shininess) {
    renderuniforms *u = &r->uniforms;
    glUseProgram(r->shader);
    glUniformMatrix4fv(u->view, 1, GL_FALSE, view);
    glUniformMatrix4fv(u->proj, 1, GL_FALSE, proj);
    render_setmodel(r, model);
    glUniform4fv(u->uColor, 1, ucolor);
    glUniform1i(u->uUseUniform, use_uniform);
    glUniform1i(u->uFlat, uflat);
    glUniform3fv(u->lightColor, 1, lightcolor);
    glUniform3fv(u->lightPos, 1, lightposn);
    glUniform3fv(u->viewPos, 1, viewposn);
    glUniform1f(u->ka, ka);
    glUniform1f(u->kd, kd);
    glUniform1f(u->ks, ks);
    glUniform1f(u->shininess, shininess);
}

#define RENDER_OPAQUE_ALPHA_EPS 0.999f

/** True when the current uniform/vertex color is treated as transparent. */
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

/** Local-space AABB center of object positions; false if no usable vertices. */
static bool render_object_centroid(scene *s, gobject *obj, vec3 out) {
    if (!s || !obj || !obj->vertexdata.format || !strchr(obj->vertexdata.format, 'x')) return false;
    if (obj->vertexdata.indx==SCENE_EMPTY || obj->vertexdata.length<=0) return false;

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

    out[0]=0.5f*(bbox[0]+bbox[1]);
    out[1]=0.5f*(bbox[2]+bbox[3]);
    out[2]=0.5f*(bbox[4]+bbox[5]);
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

/** Apply baked packet state and issue the draw.
 *  Closed translucent meshes: back faces then front (avoids mesh-order striping). */
static void render_draw_tdraw(renderer *r, rendertdraw *d) {
    render_setmodel(r, d->model);
    glUniform4fv(r->uniforms.uColor, 1, d->rgba);
    glUniform1i(r->uniforms.uUseUniform, d->use_uniform);
    glUniform1i(r->uniforms.uFlat, d->uflat);
    glUniform1f(r->uniforms.ka, d->ka);
    glUniform1f(r->uniforms.kd, d->kd);
    glUniform1f(r->uniforms.ks, d->ks);
    glUniform1f(r->uniforms.shininess, d->shininess);
    glBindVertexArray(d->vao);
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

/** Bake one transparent draw into the scratch varray; false on allocation failure. */
static bool render_collect_tdraw(renderer *r, scene *s, mat4x4 view, rendergeostate *st,
                                 renderinstruction *ins) {
    rendertdraw d;
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

/** Walk the geometry render list once, applying state and either drawing or collecting. */
static bool render_walk_geometry(renderer *r, scene *s, mat4x4 view, mat4x4 proj,
                                 vec3 lightcolor, vec3 lightposn, vec3 viewposn,
                                 rendergeopass pass) {
    rendergeostate st;
    render_geostate_reset(&st);

    if (pass==RENDER_PASS_OPAQUE) {
        render_setgeometryuniforms(r, view, proj, st.model, lightcolor, lightposn, viewposn,
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
                    render_setgeometryuniforms(r, view, proj, st.model, lightcolor, lightposn, viewposn,
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
                /* Trust the last RCOLOR for this object (always emitted at prepare).
                 * A draw-slot `C` may override vertex colors; colorless/`xnc` without
                 * `C` uses use_uniform=0 so a prior translucent `C` cannot leak. */
                int use_uniform = st.use_uniform;
                float alpha = st.ucolor[3];
                const char *fmt = (ins->obj && ins->obj->obj) ? ins->obj->obj->vertexdata.format : NULL;
                bool trans=render_is_transparent(use_uniform, alpha, fmt);
                if (pass==RENDER_PASS_OPAQUE && !trans) {
                    /* Force GPU state even if a prior uniform `C` left uUseUniform set. */
                    glUniform1i(r->uniforms.uUseUniform, use_uniform);
                    if (use_uniform==0) {
                        float one[4]={1.0f,1.0f,1.0f,1.0f};
                        glUniform4fv(r->uniforms.uColor, 1, one);
                    } else {
                        glUniform4fv(r->uniforms.uColor, 1, st.ucolor);
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

void render_render(renderer *r, float aspectratio, mat4x4 view, float near, float far, scene *s) {
    /* Clear the display */
    if (s) {
        glClearColor(s->background[0], s->background[1], s->background[2], 1.0f);
    } else {
        glClearColor(SCENE_BACKGROUND_R_DEFAULT, SCENE_BACKGROUND_G_DEFAULT,
                     SCENE_BACKGROUND_B_DEFAULT, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* Default vertex color/normal/alpha when format lacks those attributes */
    glVertexAttrib3f(1, 1.0f, 1.0f, 1.0f);
    glVertexAttrib3f(2, 0.0f, 0.0f, 1.0f);
    glVertexAttrib1f(3, 1.0f);
    
    vec3 lightcolor = {1.0f, 1.0f, 1.0f};
    vec3 lightposn = {2.0f, 1.0f, 5.0f};
    vec3 viewposn = {0.0f, 0.0f, 1.0f};

    /* Eye position in model/world space from inverse view */
    {
        mat4x4 invview;
        mat3d_invert4x4(view, invview);
        viewposn[0]=invview[12];
        viewposn[1]=invview[13];
        viewposn[2]=invview[14];
    }

    if (s && s->light_explicit) {
        lightposn[0]=s->light_pos[0];
        lightposn[1]=s->light_pos[1];
        lightposn[2]=s->light_pos[2];
        lightcolor[0]=s->light_color[0];
        lightcolor[1]=s->light_color[1];
        lightcolor[2]=s->light_color[2];
    } else if (s && s->bbox_valid) {
        float cx=0.5f*(s->bbox[0]+s->bbox[1]);
        float cy=0.5f*(s->bbox[2]+s->bbox[3]);
        float cz=0.5f*(s->bbox[4]+s->bbox[5]);
        float hx=0.5f*(s->bbox[1]-s->bbox[0]);
        float hy=0.5f*(s->bbox[3]-s->bbox[2]);
        float hz=0.5f*(s->bbox[5]-s->bbox[4]);
        float radius = sqrtf(hx*hx + hy*hy + hz*hz);
        if (radius < 1e-6f) radius = 1.0f;

        lightposn[0] = cx + 0.7f * radius;
        lightposn[1] = cy + 1.0f * radius;
        lightposn[2] = cz + 1.5f * radius;
    }
    
    mat4x4 proj;
    mat3d_ortho(NULL, proj, -1.0*aspectratio, 1.0*aspectratio, -1.0, 1.0, near, far);

    mat4x4 model;
    mat3d_identity4x4(model);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* --- Opaque pass: display-list order, depth write on, no blending --- */
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    render_walk_geometry(r, s, view, proj, lightcolor, lightposn, viewposn, RENDER_PASS_OPAQUE);

    /* --- Transparent pass: collect, sort far→near by centroid view-z, draw --- */
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    if (render_walk_geometry(r, s, view, proj, lightcolor, lightposn, viewposn,
                             RENDER_PASS_COLLECT_TRANSPARENT) &&
        r->tdraws.count>0) {
        float defaults[4]={1.0f, 1.0f, 1.0f, 1.0f};
        qsort(r->tdraws.data, r->tdraws.count, sizeof(rendertdraw), render_tdraw_cmp);
        render_setgeometryuniforms(r, view, proj, model, lightcolor, lightposn, viewposn,
                                   defaults, 0, 0,
                                   SCENE_MATERIAL_KA_DEFAULT, SCENE_MATERIAL_KD_DEFAULT,
                                   SCENE_MATERIAL_KS_DEFAULT, SCENE_MATERIAL_SHININESS_DEFAULT);
        for (unsigned i=0; i<r->tdraws.count; i++) render_draw_tdraw(r, &r->tdraws.data[i]);
    }

    glDepthMask(GL_TRUE);
    glEnable(GL_BLEND);
    
    /* Text rendering pass */
    glUseProgram(r->textshader);
    
    GLint textcoloruniform = glGetUniformLocation(r->textshader, "textColor");
    float textcolor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glUniform4fv(textcoloruniform, 1, textcolor);
    
    GLint modeluniform = glGetUniformLocation(r->textshader, "model");
    GLint viewuniform = glGetUniformLocation(r->textshader, "view");
    GLint projuniform = glGetUniformLocation(r->textshader, "proj");
    
    glUniformMatrix4fv(viewuniform, 1, GL_FALSE, view);
    glUniformMatrix4fv(projuniform, 1, GL_FALSE, proj);
    
    mat3d_identity4x4(model);
    glUniformMatrix4fv(modeluniform, 1, GL_FALSE, model);
    
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(r->fontvao);
    
    for (unsigned i=0; i<r->renderlist.count; i++) {
        renderinstruction *ins=&r->renderlist.data[i];
        switch (ins->instruction) {
            case RMODEL:
                glUniformMatrix4fv(modeluniform, 1, GL_FALSE, ins->data.model.model);
                break;
            case RTEXT:
                render_rendertext(r, ins->data.text.rfontid, ins->data.text.txt);
                break;
            case RCOLOR:
                glUniform4fv(textcoloruniform, 1, ins->data.color.rgba);
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
