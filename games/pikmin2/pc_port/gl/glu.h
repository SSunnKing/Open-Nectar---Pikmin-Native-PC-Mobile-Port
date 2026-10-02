/**
 * @file glu.h
 * @brief Minimal GLU stub header for the PC port.
 */
#ifndef _PC_PORT_GLU_H
#define _PC_PORT_GLU_H

#include "gl.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar) {
    (void)fovy; (void)aspect; (void)zNear; (void)zFar;
}
static inline void gluLookAt(GLdouble ex, GLdouble ey, GLdouble ez,
                              GLdouble cx, GLdouble cy, GLdouble cz,
                              GLdouble ux, GLdouble uy, GLdouble uz) {
    (void)ex;(void)ey;(void)ez;(void)cx;(void)cy;(void)cz;(void)ux;(void)uy;(void)uz;
}
static inline void gluOrtho2D(GLdouble l, GLdouble r, GLdouble b, GLdouble t) {
    (void)l; (void)r; (void)b; (void)t;
}
static inline GLint gluBuild2DMipmaps(GLenum target, GLint components, GLsizei w, GLsizei h,
                                       GLenum format, GLenum type, const void* data) {
    (void)target;(void)components;(void)w;(void)h;(void)format;(void)type;(void)data;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* _PC_PORT_GLU_H */
