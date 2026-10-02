/* Minimal glext.h stub */
#ifndef _PC_PORT_GLEXT_H
#define _PC_PORT_GLEXT_H
#include "gl.h"
typedef void (*PFNGLLOCKARRAYSEXTPROC)(GLint, GLsizei);
typedef void (*PFNGLUNLOCKARRAYSEXTPROC)(void);
typedef void (*PFNGLACTIVETEXTUREARBPROC)(GLenum);
typedef void (*PFNGLMULTITEXCOORD2FARBPROC)(GLenum, GLfloat, GLfloat);
typedef void (*PFNGLCLIENTACTIVETEXTUREARBPROC)(GLenum);
#define GL_TEXTURE0_ARB 0x84C0
#define GL_TEXTURE1_ARB 0x84C1
#endif
