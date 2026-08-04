/** @file matrix3d.c
 *  @author T J Atherton
 *
 *  @brief Matrix math for 3d graphics (4x4 and 3x3 matrices)
 *  @details Implements simple matrix operations with no dependencies.
 */

#include "matrix3d.h"
#include <math.h>
#include <string.h> 

/** Use Apple's Accelerate library for LAPACK and BLAS */
#ifdef __APPLE__
#define ACCELERATE_NEW_LAPACK
#include <Accelerate/Accelerate.h>
#else
#include <cblas.h>
#include <lapacke.h>
#define USE_LAPACKE
#endif



#define EPS 1e-16

/** @brief Normalizes a vector
 * @param[in] in - input vector
 * @param[out] out - output vector. */
void mat3d_vectornormalize(vec3 in, vec3 out) {
    float norm = sqrtf(in[0]*in[0] + in[1]*in[1] + in[2]*in[2]);
    if (norm>EPS) norm = 1.0f/norm;
    if (out!=in) memcpy(out, in, sizeof(float)*3);
    out[0] *= norm; out[1] *= norm; out[2] *= norm;
}

/** @brief Stores the identity matrix in out.
 * @param[out] out - output matrix. */
void mat3d_identity4x4(mat4x4 out) {
    static const float ident[] = { 1.0f, 0.0f, 0.0f, 0.0f,
                                   0.0f, 1.0f, 0.0f, 0.0f,
                                   0.0f, 0.0f, 1.0f, 0.0f,
                                   0.0f, 0.0f, 0.0f, 1.0f };
    memcpy(out, ident, sizeof(ident));
}

/** @brief Stores the identity matrix in out.
 * @param[out] out - output matrix. */
void mat3d_identity3x3(mat3x3 out) {
    static const float ident[] = { 1.0f, 0.0f, 0.0f,
                                   0.0f, 1.0f, 0.0f,
                                   0.0f, 0.0f, 1.0f };
    memcpy(out, ident, sizeof(ident));
}

/** @brief Multiply out = a*b
 * @param[in] a input matrix
 * @param[in] b input matrix
 * @param[out] out filled with a*b
 * @warning: out must be distinct from a and b */
void mat3d_mul4x4(mat4x4 a, mat4x4 b, mat4x4 out) {
    for (unsigned int col=0; col<4; col++) {
        for (unsigned int row=0; row<4; row++) {
            out[col*4+row] = a[row]*b[col*4] + a[4+row]*b[col*4+1] +
                             a[8+row]*b[col*4+2] + a[12+row]*b[col*4+3];
        }
    }
}

/** @brief Multiply: out = a*b
 * @param[in] a input matrix
 * @param[in] b input matrix
 * @param[out] out filled with a*b
 * @warning: out must be distinct from a and b */
void mat3d_mul3x3(mat3x3 a, mat3x3 b, mat3x3 out) {
    for (unsigned int col=0; col<3; col++) {
        for (unsigned int row=0; row<3; row++) {
            out[col*3+row] = a[row]*b[col*3] + a[3+row]*b[col*3+1] + a[6+row]*b[col*3+2];
        }
    }
}

/** @brief Add with scale: out = a + alpha*b
 * @param[in] a input matrix
 * @param[in] b input matrix
 * @param[out] out filled with a + alpha*b  */
void mat3d_addscale3x3(mat3x3 a, float alpha, mat3x3 b, mat3x3 out) {
    if (a!=out) memcpy(out, a, sizeof(float)*9);
    for (unsigned int i=0; i<9; i++) out[i] += alpha*b[i];
}

/** @brief Copy: out = a
 * @param[in] a input matrix
 * @param[out] out filled with a*b */
void mat3d_copy4x4(mat4x4 a, mat4x4 out) {
    memcpy(out, a, sizeof(float)*16);
}

/** @brief Matrix inversion
 * @param[in] a input matrix
 * @param[out] out filled with inverse(a)  */
void mat3d_invert4x4(mat4x4 a, mat4x4 out) {
    int m = 4, n = 4;
    int piv[4];
    int info;
    /* Copy a into out */
    memcpy(out, a, sizeof(float)*16);
    /* Compute LU decomposition, storing result in place */
#ifdef USE_LAPACKE
    info = LAPACKE_sgetrf(LAPACK_COL_MAJOR, m, n, out, m, piv);
#else
    sgetrf_(&m, &n, out, &m, piv, &info);
#endif
    
    if (!info) {
        /* Now compute inverse */
#ifdef USE_LAPACKE
        info=LAPACKE_sgetri(LAPACK_COL_MAJOR, n, out, n, piv);
#else
        float work[16];
        int lwork=16;
        sgetri_(&n, out, &n, piv, work, &lwork, &info);
#endif
    }
}

/** @brief Convert a 3x3 matrix to a 4x4 matrix
 * @param[in] in input matrix
 * @param[out] out filled with inverse(a)  */
void mat3d_lift(mat3x3 in, mat4x4 out) {
    mat4x4 new = { in[0], in[1], in[2], 0.0f, // Col major order!
                   in[3], in[4], in[5], 0.0f,
                   in[6], in[7], in[8], 0.0f,
                    0.0f,  0.0f,  0.0f, 1.0f };
    memcpy(out, new, sizeof(new));
}

/** @brief Print a 3x3 matrix */
void mat3d_print3x3(mat3x3 in) {
    for (unsigned int j=0; j<3; j++) { // row
        printf("[ ");
        for (unsigned int i=0; i<3; i++) { // column
            printf("%g ", in[i*3+j]);
        }
        printf("]\n");
    }
}

/** @brief Print a 3x3 matrix */
void mat3d_print4x4(mat4x4 in) {
    for (unsigned int j=0; j<4; j++) { // row
        printf("[ ");
        for (unsigned int i=0; i<4; i++) { // column
            printf("%g ", in[i*4+j]);
        }
        printf("]\n");
    }
}

/** @brief Translate by a vector
 * @param[in] in input matrix
 * @param[in] vec translation vector
 * @param[out] out on output, contains T*in where T is the translation matrix computed from vec */
void mat3d_translate(mat4x4 in, vec3 vec, mat4x4 out) {
    mat4x4 tr = { 1.0f, 0.0f, 0.0f, 0.0f, // Col major order!
                  0.0f, 1.0f, 0.0f, 0.0f,
                  0.0f, 0.0f, 1.0f, 0.0f,
                  vec[0], vec[1], vec[2], 1.0f };
    mat4x4 in2;
    if (in==out) mat3d_copy4x4(in, in2); /* Use a copy if in and out are the same matrix */
    if (in) mat3d_mul4x4(tr, (in==out ? in2 : in), out);
    else mat3d_copy4x4(tr, out);
}

/** @brief Scale by a factor
 * @param[in] in input matrix
 * @param[in] scale scale factor
 * @param[out] out on output, contains S*in where S is uniform scale */
void mat3d_scale(mat4x4 in, float scale, mat4x4 out) {
    vec3 s = { scale, scale, scale };
    mat3d_scale3(in, s, out);
}

/** @brief Non-uniform scale
 * @param[in] in input matrix
 * @param[in] scale per-axis scale factors
 * @param[out] out on output, contains S*in where S is diag(sx,sy,sz,1) */
void mat3d_scale3(mat4x4 in, vec3 scale, mat4x4 out) {
    mat4x4 tr = { scale[0], 0.0f, 0.0f, 0.0f, // Col major order!
                  0.0f, scale[1], 0.0f, 0.0f,
                  0.0f, 0.0f, scale[2], 0.0f,
                  0.0f, 0.0f,  0.0f, 1.0f };
    mat4x4 in2;
    if (in==out) mat3d_copy4x4(in, in2); /* Use a copy if in and out are the same matrix */
    if (in) mat3d_mul4x4(tr, (in==out ? in2 : in), out);
    else mat3d_copy4x4(tr, out);
}

/** @brief Rotate by angle around an axis
 * @param[in] in input matrix
 * @param[in] axis rotation axis
 * @param[in] angle rotation angle
 * @param[out] out on output, contains R*in where R is the translation matrix computed from vec */
void mat3d_rotate(mat4x4 in, vec3 axis, float angle, mat4x4 out) {
    vec3 u;
    mat3x3 rot;
    mat4x4 rot4;
    mat4x4 in2;
    if (in==out) mat3d_copy4x4(in, in2); /* Use a copy if in and out are the same matrix */
    
    /* Construct rotation matrix from Rodrigues formula */
    mat3d_vectornormalize(axis, u);
    mat3x3 w = { 0.0f,  u[2], -u[1],  // Col major order
                -u[2],  0.0f,  u[0],
                 u[1], -u[0],  0.0f };
    mat3x3 w2;
    
    /* Rodrigues formula: R = I + sin(a) W + 2 sin(a/2)^2 W^2 */
    mat3d_identity3x3(rot);
    mat3d_addscale3x3(rot, sin(angle), w, rot);
    mat3d_mul3x3(w, w, w2);
    float phi = sin(angle/2);
    mat3d_addscale3x3(rot, 2*phi*phi, w2, rot);
    
    /* Convert to 4x4 matrix */
    mat3d_lift(rot, rot4);

    /* Multiply the input matrix by this */
    if (in) mat3d_mul4x4(rot4, (in==out ? in2 : in), out);
    else mat3d_copy4x4(rot4, out);
}

/** @brief Orthographic projection matrix
 * @param[in] in input matrix
 * @param[in] left     } Bounds of the viewing area
 * @param[in] right   }
 * @param[in] bottom }
 * @param[in] top        }
 * @param[in] near      }
 * @param[in] far        }
 * @param[out] out on output, contains R*in where R is the translation matrix computed from vec */
void mat3d_ortho(mat4x4 in, mat4x4 out, float left, float right, float bottom, float top, float near, float far) {
    mat4x4 pr = { 2.0f/(right-left), 0.0f, 0.0f, 0.0f, // Col major order!
                  0.0f, 2.0f/(top-bottom), 0.0f, 0.0f,
                  0.0f, 0.0f, -2.0f/(far-near), 0.0f,
                  0.0f, 0.0f, 0.0f, 1.0f };
    mat4x4 in2;
    if (in==out) mat3d_copy4x4(in, in2); /* Use a copy if in and out are the same matrix */
    
    /* Multiply the input matrix by this */
    if (in) mat3d_mul4x4(pr, (in==out ? in2 : in), out);
    else mat3d_copy4x4(pr, out);
}

/** @brief Perspective projection matrix
 * @param[in] in input matrix
 * @param[in] left     } Bounds of the viewing area
 * @param[in] right   }
 * @param[in] bottom }
 * @param[in] top        }
 * @param[in] near      }
 * @param[in] far        }
 * @param[out] out on output, contains R*in where R is the translation matrix computed from vec */
void mat3d_frustum(mat4x4 in, mat4x4 out, float left, float right, float bottom, float top, float near, float far) {
    mat4x4 pr = { 2*near/(right-left), 0.0f, 0.0f, 0.0f, // Col major order!
                  0.0f, 2*near/(top-bottom), 0.0f, 0.0f,
                  (right+left)/(right-left), (top+bottom)/(top-bottom), -(far+near)/(far-near), -1.0f,
                  0.0f, 0.0f, -2*far*near/(far-near), 0.0f };
    mat4x4 in2;
    if (in==out) mat3d_copy4x4(in, in2); /* Use a copy if in and out are the same matrix */
    
    /* Multiply the input matrix by this */
    if (in) mat3d_mul4x4(pr, (in==out ? in2 : in), out);
    else mat3d_copy4x4(pr, out);
}
