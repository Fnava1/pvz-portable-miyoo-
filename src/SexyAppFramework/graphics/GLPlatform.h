/*
 * Portions of this file are based on the PopCap Games Framework
 * Copyright (C) 2005-2009 PopCap Games, Inc.
 *
 * Copyright (C) 2026 Zhou Qiankang <wszqkzqk@qq.com>
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later AND LicenseRef-PopCap
 *
 * This file is part of PvZ-Portable.
 */

#ifndef __GLPLATFORM_H__
#define __GLPLATFORM_H__

#if defined(PVZ_ENABLE_GLES) && PVZ_ENABLE_GLES

#ifdef __SWITCH__
#include <switch.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4551) // glad generated code triggers this on MSVC
#endif

#include <glad/gles2.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <SDL.h>

// Shared macro definitions — identical keywords in GLSL ES 1.00 and GLSL 1.20
#define GLSL_VERT_MACROS \
	"#define VERT_IN attribute\n" \
	"#define V2F varying\n"

#define GLSL_FRAG_MACROS \
	"#define V2F varying\n" \
	"#define FRAG_OUT gl_FragColor\n" \
	"#define TEX2D texture2D\n"

// When true, a desktop GL compatibility context is in use and shaders
extern bool gDesktopGLFallback;

inline void PlatformGLInit()
{
#ifdef __SWITCH__
	gladLoadGLES2((GLADloadfunc)eglGetProcAddress);
#else
	gladLoadGLES2((GLADloadfunc)SDL_GL_GetProcAddress);
#endif
}

#else // PVZ_ENABLE_GLES == 0: 100% Software Renderer for Miyoo Mini / Retro Handhelds

#include <SDL.h>

typedef unsigned int GLuint;
typedef unsigned int GLenum;
typedef int GLint;
typedef float GLfloat;
typedef char GLchar;

#define GL_SRC_ALPHA 0
#define GL_ONE_MINUS_SRC_ALPHA 0

extern bool gDesktopGLFallback;

inline void PlatformGLInit()
{
	// No OpenGL initialization needed for software rasterizer
}

#endif // PVZ_ENABLE_GLES

#endif // __GLPLATFORM_H__
