/* Minimal wingdi.h stub - Windows GDI types used by oglGraphics.cpp.
 *
 * On Windows the real header exists, and this file sits on the include path
 * ahead of the SDK, so it would shadow it: windows.h -> wincon.h -> commdlg.h
 * then fail on LOGFONT and LF_FACESIZE. Hand those translation units the real
 * header instead of this stub.
 */
#if defined(_WIN32)
#include_next <wingdi.h>
#else
#ifndef _PC_PORT_WINGDI_H
#define _PC_PORT_WINGDI_H
typedef int BOOL;
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#endif
#endif
