/** @file display.c
 *  @author T J Atherton
 *
 *  @brief Window handling using GLFW
 */

#include <math.h>
#include "display.h"
#include "scene.h"
#include "render.h"
#include "command.h"
#include "listener.h"

/* -------------------------------------------------------
 * Global variables
 * ------------------------------------------------------- */

display *opendisplays;

/* -------------------------------------------------------
 * Utility functions
 * ------------------------------------------------------- */

/** Identify the display structure from a given window ref. */
static display *display_fromwindow(windowref *window) {
    return (display *) glfwGetWindowUserPointer(window);
}

/** Find the display attached to a scene */
display *display_findforscene(scene *s) {
    for (display *d = opendisplays; d!=NULL; d=d->next) {
        if (d->s==s) return d;
    }
    return NULL;
}

/** Add to list of open displays */
void display_add(display *d) {
    d->next=opendisplays;
    opendisplays=d;
}

/** Frees data attached to a display */
void display_free(display *d) {
    scene_free(d->s);
    /* GL resources are cleared in display_loop before the window is destroyed */
    if (d->window) {
        glfwMakeContextCurrent(d->window);
        render_clear(&d->render);
    }
    free(d);
}

/** Remove from list of open displays */
void display_remove(display *d) {
    if (opendisplays==d) {
        opendisplays=d->next;
        display_free(d);
    } else {
        display *prev = NULL;
        for (display *e = opendisplays; e!=NULL; e=e->next) {
            if (d==e) {
                prev->next=d->next;
                display_free(d);
                return;
            }
            prev=e;
        }
    }
}

/* -------------------------------------------------------
 * Event callbacks
 * ------------------------------------------------------- */

/** Error callback */
static void display_errorcallback(int error, const char* description) {
    fprintf(stderr, "morphoview: GLFW error '%s'\n", description);
}

/** Framebuffer resize callback */
static void display_framebuffersizecallback(windowref *window, int width, int height) {
    display *d=display_fromwindow(window);
    
    d->aspectRatio=(float) width/(float) height;
    //glViewport(0, 0, width, height);
    d->width = (float) width;
    glViewport(0, 0, width, height);
}

/** Keypress callback function */
static void display_keycallback(windowref *window, int key, int scancode, int action, int mods) {
    if (action!=GLFW_PRESS) return;
    display *d=display_fromwindow(window);
    
    switch (key) {
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, true);
            break;
        case GLFW_KEY_TAB:
        { /* Reset to fitted home view */
            d->ox=0.0; d->oy=0.0;
            mat3d_identity4x4(d->view);
            d->view_user_modified=false;
            d->view_fitted=false;
            if (d->s && d->s->bbox_valid) display_fit(d);
        }
            break;
        case GLFW_KEY_LEFT:
            d->view_user_modified=true;
            if (mods & GLFW_MOD_ALT) {
                /* Translate left */
                vec3 a = {-0.1, 0.0, 0.0};
                mat3d_translate(d->view, a, d->view);
            } else { /* Rotate left */
                vec3 a = {0.0, 1.0, 0.0};
                mat3d_rotate(d->view, a, -0.1, d->view);
            }
            break;
        case GLFW_KEY_RIGHT:
            d->view_user_modified=true;
            if (mods & GLFW_MOD_ALT) {
                /* Translate right */
                vec3 a = {0.1, 0.0, 0.0};
                mat3d_translate(d->view, a, d->view);
            } else { /* Rotate right */
                vec3 a = {0.0, 1.0, 0.0};
                mat3d_rotate(d->view, a, +0.1, d->view);
            }
            break;
        case GLFW_KEY_DOWN:
            d->view_user_modified=true;
            if (mods & GLFW_MOD_ALT) {
                /* Translate down */
                vec3 a = {0.0, -0.1, 0.0};
                mat3d_translate(d->view, a, d->view);
            } else { /* Rotate down */
                vec3 a = {1.0, 0.0, 0.0};
                mat3d_rotate(d->view, a, +0.1, d->view);
            }
            break;
        case GLFW_KEY_UP:
            d->view_user_modified=true;
            if (mods & GLFW_MOD_ALT) {
                /* Translate up */
                vec3 a = {0.0, 0.1, 0.0};
                mat3d_translate(d->view, a, d->view);
            } else { /* Rotate up */
                vec3 a = {1.0, 0.0, 0.0};
                mat3d_rotate(d->view, a, -0.1, d->view);
            }
            break;
        case GLFW_KEY_PAGE_DOWN:
            d->view_user_modified=true;
        { /* Rotate clockwise */
            vec3 a = {0.0, 0.0, 1.0};
            mat3d_rotate(d->view, a, -0.1, d->view);
        }
            break;
        case GLFW_KEY_PAGE_UP:
            d->view_user_modified=true;
        { /* Rotate anticlockwise */
            vec3 a = {0.0, 0.0, 1.0};
            mat3d_rotate(d->view, a, +0.1, d->view);
        }
            break;
        case GLFW_KEY_EQUAL:
            d->view_user_modified=true;
            mat3d_scale(d->view, 1.05, d->view);
            break;
        case GLFW_KEY_MINUS:
            d->view_user_modified=true;
            mat3d_scale(d->view, 0.95, d->view);
            break;
    }
    
}

/** Cursor position callback */
static void display_cursorposncallback(windowref *window, double x, double y) {
    display *d=display_fromwindow(window);

    if (d->state==DRAGGING_ROT) {
        d->view_user_modified=true;
        float dx=2.0*(x-d->ox)/d->width;
        float dy=-2.0*(y-d->oy)/d->width;
        
        vec3 axis = {-dy, dx, 0};
        mat3d_rotate(d->view, axis, 1.5*sqrt(dx*dx+dy*dy), d->view);
    } else if (d->state==DRAGGING_TRANS) {
        d->view_user_modified=true;
        float dx=2.0*((float)(x-d->ox))/d->width;
        float dy=2.0*((float)(y-d->oy))/d->width;
        
        vec3 a = {dx, -dy, 0.0};
        mat3d_translate(d->view, a, d->view);
    }
    d->ox=x; d->oy=y;
}

/** Scroll callback */
static void display_scrollcallback(windowref *window, double x, double y) {
    display *d=display_fromwindow(window);
    
    d->view_user_modified=true;
    mat3d_scale(d->view, 1.0-0.25*y, d->view);
}

/** Mouse click callback */
static void display_mousebuttoncallback(windowref *window, int button, int action, int mods) {
    display *d=display_fromwindow(window);
    
    if (action == GLFW_PRESS) {
        d->state = (button==GLFW_MOUSE_BUTTON_LEFT ? DRAGGING_ROT : DRAGGING_TRANS);
    } else {
        d->state = NORMAL;
    }
}

/* -------------------------------------------------------
 * Create a window
 * ------------------------------------------------------- */

/** Initializes a display structure */
void display_init(display *d, scene *s) {
    d->s=s;
    d->width=0.0;
    d->aspectRatio=1.0;
    d->ox=0.0;
    d->oy=0.0;
    d->state=NORMAL;
    d->window=NULL;
    mat3d_identity4x4(d->view);
    d->view_fitted=false;
    d->view_user_modified=false;
    d->ortho_near=-10.0f;
    d->ortho_far=10.0f;
}

/** Create a new display */
display *display_open(scene *s) {
    display *new = malloc(sizeof(display));
    if (!new) {
        fprintf(stderr, "morphoview: Couldn't allocate display structure");
        return NULL;
    }
    
    display_init(new, s);
    
    windowref *window = NULL;
    
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, true);
#endif
    glfwWindowHint(GLFW_SAMPLES, 4);
    
    /* Create a windowed mode window and its OpenGL context.
     * Share with an existing window so multi-window GL objects behave reliably. */
    windowref *share = (opendisplays!=NULL) ? opendisplays->window : NULL;
    window = glfwCreateWindow(DISPLAY_DEFAULTWIDTH, DISPLAY_DEFAULTHEIGHT, DISPLAY_DEFAULTTITLE, NULL, share);
    if (!window) return NULL;

    new->width=DISPLAY_DEFAULTWIDTH;
    new->aspectRatio=((float) DISPLAY_DEFAULTWIDTH)/((float) DISPLAY_DEFAULTHEIGHT);

    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, display_keycallback);
    glfwSetFramebufferSizeCallback(window, display_framebuffersizecallback);
    glfwSetScrollCallback(window, display_scrollcallback);
    glfwSetCursorPosCallback(window, display_cursorposncallback);
    glfwSetMouseButtonCallback(window, display_mousebuttoncallback);
    glfwSetWindowUserPointer(window, new);
    
    if (!gladLoadGLLoader((GLADloadproc) glfwGetProcAddress)) {
        fprintf(stderr, "morphoview: Failed to initialize GLAD");
    }
    
    /** Initialize the display */
    render_init(&new->render);
    new->window=window;
    
    /** Add this to the display list */
    display_add(new);
    
    return new;
}

/** Sets the window title */
void display_setwindowtitle(display *d, char *title) {
    if (d) glfwSetWindowTitle(d->window, title);
}

/** Mark a display's window for close; display_loop performs teardown. */
void display_requestclose(display *d) {
    if (d && d->window) glfwSetWindowShouldClose(d->window, true);
}

/** Mark every open display for close. */
void display_requestcloseall(void) {
    for (display *d = opendisplays; d!=NULL; d=d->next) {
        display_requestclose(d);
    }
}

/** True if at least one display is open. */
bool display_anyopen(void) {
    return opendisplays != NULL;
}

/** Fit the camera so the scene AABB fills the window with margin. */
void display_fit(display *d) {
    if (!d || !d->s || !d->s->bbox_valid) return;

    float *b=d->s->bbox;
    float cx=0.5f*(b[0]+b[1]);
    float cy=0.5f*(b[2]+b[3]);
    float cz=0.5f*(b[4]+b[5]);
    float hx=0.5f*(b[1]-b[0]);
    float hy=0.5f*(b[3]-b[2]);
    float hz=0.5f*(b[5]-b[4]);

    float aspect = (d->aspectRatio>1e-6f) ? d->aspectRatio : 1.0f;
    float extent = fmaxf(hx/aspect, hy);
    float scale = 1.0f;
    if (extent>1e-6f) scale = 1.0f / (extent * 1.1f);

    mat3d_identity4x4(d->view);
    vec3 center = {-cx, -cy, -cz};
    mat3d_translate(d->view, center, d->view);
    mat3d_scale(d->view, scale, d->view);

    float zspan = scale*hz + 1.0f;
    if (zspan < 2.0f) zspan = 2.0f;
    d->ortho_near = -zspan;
    d->ortho_far = zspan;

    d->view_fitted = true;
    d->s->bbox_fit_pending = false;
}

/** Upload changed scenes to GL (skip displays whose scene was not touched). */
void display_prepareall(void) {
    for (display *d = opendisplays; d!=NULL; d=d->next) {
        if (!d->window || !d->s || !d->s->changed) continue;
        glfwMakeContextCurrent(d->window);

        /* render_preparescene appends; reset so re-prepare after D/U S is safe. */
        render_reset(&d->render);

        if (!d->s->bbox_explicit)
            scene_computebbox(d->s);

        render_preparescene(&d->render, d->s);

        if (d->s->bbox_valid && !d->view_user_modified &&
            (!d->view_fitted || d->s->bbox_fit_pending))
            display_fit(d);

        d->s->changed = false;
    }
}

/* -------------------------------------------------------
 * Main loop
 * ------------------------------------------------------- */

void display_loop(void) {
    bool had_displays = (opendisplays != NULL);

    while (opendisplays != NULL || listener_isactive()) {
        glfwWaitEvents();
        command_process();

        if (opendisplays != NULL) had_displays = true;

        for (display *d=opendisplays; d!=NULL; d=d->next) {
            if (glfwWindowShouldClose(d->window)) {
                /* Free GL resources while this window's context is still current */
                glfwMakeContextCurrent(d->window);
                render_clear(&d->render);
                glfwDestroyWindow(d->window);
                d->window=NULL;
                display_remove(d);
                break;
            } else {
                glfwMakeContextCurrent(d->window);
                render_render(&d->render, d->aspectRatio, d->view, d->ortho_near, d->ortho_far, d->s);
                
                glfwSwapBuffers(d->window);
            }
        }

        /* After the last window closes, notify Morpho and stop the listener. */
        if (had_displays && opendisplays == NULL && listener_isactive()) {
            listener_reply(LISTENER_WINDOW_CLOSED);
            listener_stop();
        }
    }
}

/* -------------------------------------------------------
 * Initialization/Finalization
 * ------------------------------------------------------- */

bool display_initialize(void) {
    bool success = glfwInit();
    if (!success) fprintf(stderr, "morphoview: Could not launch GLFW.\n");
        
    glfwSetErrorCallback(display_errorcallback);
    
    opendisplays=NULL;
    
    return success;
}

void display_finalize(void) {
    glfwTerminate();
}
