/*
 * sdl/video_gl.c - SDL library specific port code - OpenGL accelerated video display
 *
 * Copyright (c) 2010 Tomasz Krasuski
 * Copyright (C) 2010 Atari800 development team (see DOC/CREDITS)
 *
 * This file is part of the Atari800 emulator project which emulates
 * the Atari 400, 800, 800XL, 130XE, and 5200 8-bit computers.
 *
 * Atari800 is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Atari800 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Atari800; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <SDL.h>
#include <SDL_opengl.h>
#include <stdlib.h>

#include "af80.h"
#include "antic.h"
#include "bit3.h"
#include "artifact.h"
#include "altirra_artifacting/artifacting_c.h"
#include "atari.h"
#include "cfg.h"
#include "colours.h"
#include "config.h"
#include "filter_ntsc.h"
#include "gtia.h"
#include "log.h"
#include "pbi_proto80.h"
#ifdef PAL_BLENDING
#include "pal_blending.h"
#endif /* PAL_BLENDING */
#include "platform.h"
#include "screen.h"
#include "videomode.h"
#include "xep80.h"
#include "xep80_fonts.h"
#include "util.h"

#include "sdl/palette.h"
#include "sdl/video.h"
#include "sdl/video_gl.h"
#if SDL2
#include "sdl/crt-royale/crt_royale_masks.h"
#include "sdl/crt-royale/crt_royale_masks.c"
#endif

#ifndef M_PI
# define M_PI 3.141592653589793
#endif

static int currently_rotated = FALSE;
/* If TRUE, then 32 bit, else 16 bit screen. */
static int bpp_32 = FALSE;

static ATC_ArtifactingEngine *pal_hi_engine = NULL;
static Uint8 *pal_hi_input = NULL;
static Uint32 *pal_hi_output = NULL;
static Uint32 pal_hi_map_r[256];
static Uint32 pal_hi_map_g[256];
static Uint32 pal_hi_map_b[256];
static int pal_hi_pixel_format = -1;
static Uint8 pal_hi_scanline_hires[ATC_ARTIFACTING_M];

int SDL_VIDEO_GL_filtering = 0;
int SDL_VIDEO_GL_pixel_format = SDL_VIDEO_GL_PIXEL_FORMAT_BGR16;
#if SDL2
#  define SDL_OpenGL_FLAG SDL_WINDOW_OPENGL
#  define SDL_OpenGL_FULLSCREEN SDL_WINDOW_FULLSCREEN
static int screen_width = 0;
static int screen_height = 0;
float zoom_factor = 1.0f;
#else
#  define SDL_OpenGL_FLAG SDL_OPENGL
#  define SDL_OpenGL_FULLSCREEN SDL_FULLSCREEN
#endif

/* Path to the OpenGL shared library. */
static char const *library_path = NULL;

/* Pointers to OpenGL functions, loaded dynamically during initialisation. */
static struct
{
	void(APIENTRY*Viewport)(GLint,GLint,GLsizei,GLsizei);
	void(APIENTRY*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
	void(APIENTRY*Clear)(GLbitfield);
	void(APIENTRY*Enable)(GLenum);
	void(APIENTRY*Disable)(GLenum);
	void(APIENTRY*GenTextures)(GLsizei, GLuint*);
	void(APIENTRY*DeleteTextures)(GLsizei, const GLuint*);
	void(APIENTRY*BindTexture)(GLenum, GLuint);
	void(APIENTRY*TexParameteri)(GLenum, GLenum, GLint);
	void(APIENTRY*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid*);
	void(APIENTRY*TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid*);
	void(APIENTRY*TexCoord2f)(GLfloat, GLfloat);
	void(APIENTRY*Vertex3f)(GLfloat, GLfloat, GLfloat);
	void(APIENTRY*Color4f)(GLfloat, GLfloat, GLfloat, GLfloat);
	void(APIENTRY*BlendFunc)(GLenum,GLenum);
	void(APIENTRY*MatrixMode)(GLenum);
	void(APIENTRY*Ortho)(GLdouble,GLdouble,GLdouble,GLdouble,GLdouble,GLdouble);
	void(APIENTRY*LoadIdentity)(void);
	void(APIENTRY*Begin)(GLenum);
	void(APIENTRY*End)(void);
	void(APIENTRY*GetIntegerv)(GLenum, GLint*);
	const GLubyte*(APIENTRY*GetString)(GLenum);
	GLuint(APIENTRY*GenLists)(GLsizei);
	void(APIENTRY*DeleteLists)(GLuint, GLsizei);
	void(APIENTRY*NewList)(GLuint, GLenum);
	void(APIENTRY*EndList)(void);
	void(APIENTRY*CallList)(GLuint);
	void(APIENTRY*GenBuffersARB)(GLsizei, GLuint*);
	void(APIENTRY*DeleteBuffersARB)(GLsizei, const GLuint*);
	void(APIENTRY*BindBufferARB)(GLenum, GLuint);
	void(APIENTRY*BufferDataARB)(GLenum, GLsizeiptr, const GLvoid*, GLenum);
	void*(APIENTRY*MapBuffer)(GLenum, GLenum);
	GLboolean(APIENTRY*UnmapBuffer)(GLenum);
#if SDL2
	GLenum (APIENTRY* GetError)(void);
	GLuint (APIENTRY* CreateShader)(GLenum);
	void   (APIENTRY* ShaderSource)(GLuint shader, GLsizei count, GLchar* const* string, const GLint* length);
	GLuint (APIENTRY* CreateProgram)(void);
	void   (APIENTRY* CompileShader)(GLuint shader);
	void   (APIENTRY* GetShaderiv)(GLuint shader, GLenum pname, GLint* params);
	void   (APIENTRY* GetShaderInfoLog)(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
	void   (APIENTRY* AttachShader)(GLuint program, GLuint shader);
	void   (APIENTRY* LinkProgram)(GLuint program);
	void   (APIENTRY* GetProgramiv)(GLuint program, GLenum pname, GLint* params);
	void   (APIENTRY* GetProgramInfoLog)(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
	void   (APIENTRY* DeleteShader)(GLuint shader);
	void   (APIENTRY* UseProgram)(GLuint program);
	void   (APIENTRY* Uniform1f)(GLint location, GLfloat v0);
	void   (APIENTRY* Uniform2f)(GLint location, GLfloat v0, GLfloat v1);
	void   (APIENTRY* Uniform1i)(GLint location, GLint v0);
	void   (APIENTRY* UniformMatrix4fv)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
	void   (APIENTRY* ActiveTexture)(GLenum texture);
	void   (APIENTRY* VertexAttribPointer)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer);
	void   (APIENTRY* EnableVertexAttribArray)(GLuint index);
	void   (APIENTRY* GenVertexArrays)(GLsizei n, GLuint* arrays);
	void   (APIENTRY* BindVertexArray)(GLuint array);
	void   (APIENTRY* GenBuffers)(GLsizei n, GLuint* buffers);
	void   (APIENTRY* BufferData)(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
	void   (APIENTRY* BindBuffer)(GLenum target, GLuint buffer);
	GLint  (APIENTRY* GetUniformLocation)(GLuint program, const GLchar* name);
	GLint  (APIENTRY* GetAttribLocation)(GLuint program, const GLchar* name);
	void   (APIENTRY* DrawElements)(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices);
	void   (APIENTRY* BindAttribLocation)(GLuint program, GLuint index, const GLchar* name);
	void   (APIENTRY* DeleteProgram)(GLuint program);
	void   (APIENTRY* GenFramebuffers)(GLsizei n, GLuint* framebuffers);
	void   (APIENTRY* BindFramebuffer)(GLenum target, GLuint framebuffer);
	void   (APIENTRY* FramebufferTexture2D)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
	GLenum (APIENTRY* CheckFramebufferStatus)(GLenum target);
	void   (APIENTRY* DeleteFramebuffers)(GLsizei n, const GLuint* framebuffers);
	void   (APIENTRY* DeleteVertexArrays)(GLsizei n, const GLuint* arrays);
	void   (APIENTRY* DeleteBuffers)(GLsizei n, const GLuint* buffers);
	void   (APIENTRY* GenerateMipmap)(GLenum target);
#endif
} gl;

static void DisplayNormal(GLvoid *dest);
static void DisplayAltirraPalHi(GLvoid *dest);
#if NTSC_FILTER
static void DisplayNTSCEmu(GLvoid *dest);
#endif
#ifdef XEP80_EMULATION
static void DisplayXEP80(GLvoid *dest);
#endif
#ifdef PBI_PROTO80
static void DisplayProto80(GLvoid *dest);
#endif
#ifdef AF80
static void DisplayAF80(GLvoid *dest);
#endif
#ifdef BIT3
static void DisplayBIT3(GLvoid *dest);
#endif
#ifdef PAL_BLENDING
static void DisplayPalBlending(GLvoid *dest);
#endif /* PAL_BLENDING */
#if SDL2
static void CrtRoyale_Destroy(void);
static int CrtRoyale_Display(void);
#endif

static void (* blit_funcs[VIDEOMODE_MODE_SIZE])(GLvoid *) = {
	&DisplayNormal
#if NTSC_FILTER
	,&DisplayNTSCEmu
#endif
#ifdef XEP80_EMULATION
	,&DisplayXEP80
#endif
#ifdef PBI_PROTO80
	,&DisplayProto80
#endif
#ifdef AF80
	,&DisplayAF80
#endif
#ifdef BIT3
	,&DisplayBIT3
#endif
};

/* GL textures - [0] is screen, [1] is scanlines. */
static GLuint textures[2];

int SDL_VIDEO_GL_pbo = TRUE;

/* Indicates whether Pixel Buffer Objects GL extension is available.
   Available from OpenGL 2.1, it gives a significant boost in blit speed. */
static int pbo_available;
/* Name of the main screen Pixel Buffer Object. */
static GLuint screen_pbo;

/* Data for the screen texture. not used when PBOs are used. */
static GLvoid *screen_texture = NULL;

/* 16- and 32-bit ARGB textures, both of size 1x2, used for displaying scanlines.
   They contain a transparent black pixel above an opaque black pixel. */
static Uint32 const scanline_tex32[2] = { 0x00000000, 0xff000000 }; /* BGRA 8-8-8-8-REV */
/* The 16-bit one is padded to 32 bits, hence it contains 4 values, not 2. */
static Uint16 const scanline_tex16[4] = { 0x0000, 0x0000, 0x8000, 0x0000 }; /* BGRA 5-5-5-1-REV */

/* Variables used with "subpixel shifting". Screen and scanline textures
   sometimes are intentionally shifted by a part of a pixel to look better/clearer. */
static GLfloat screen_vshift;
static GLfloat screen_hshift;
static GLfloat scanline_vshift;
static int paint_scanlines;

/* GL Display List for placing the screen and scanline textures on the screen. */
static GLuint screen_dlist;

static char const * const pixel_format_cfg_strings[SDL_VIDEO_GL_PIXEL_FORMAT_SIZE] = {
	"BGR16",
	"RGB16",
	"BGRA32",
	"ARGB32"
};

typedef struct pixel_format_t {
	GLint internal_format;
	GLenum format;
	GLenum type;
	Uint32 black_pixel;
	Uint32 rmask;
	Uint32 gmask;
	Uint32 bmask;
	void(*calc_pal_func)(void *dest, int const *palette, int size);
	void(*ntsc_blit_func)(atari_ntsc_t const*, ATARI_NTSC_IN_T const*, long, int, int, void*, long);
} pixel_format_t;

pixel_format_t const pixel_formats[4] = {
	{ GL_RGB5, GL_RGB, GL_UNSIGNED_SHORT_5_6_5_REV, 0x0000,
	  0x0000001f, 0x000007e0, 0x0000f800,
	  &SDL_PALETTE_Calculate16_B5G6R5, &atari_ntsc_blit_bgr16 },
	{ GL_RGB5, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 0x0000,
	  0x0000f800, 0x000007e0, 0x0000001f,
	  &SDL_PALETTE_Calculate16_R5G6B5, &atari_ntsc_blit_rgb16 }, /* NVIDIA 16-bit */
	{ GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8, 0xff000000,
	  0x0000ff00, 0x00ff0000, 0xff000000,
	  &SDL_PALETTE_Calculate32_B8G8R8A8, &atari_ntsc_blit_bgra32 },
	{ GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, 0xff000000,
	  0x00ff0000, 0x0000ff00, 0x000000ff,
	  &SDL_PALETTE_Calculate32_A8R8G8B8, &atari_ntsc_blit_argb32 } /* NVIDIA 32-bit */
};

/* Conversion between function pointers and 'void *' is forbidden in
   ISO C, but unfortunately unavoidable when using SDL_GL_GetProcAddress.
   So the code below is non-portable and gives a warning with gcc -ansi
   -Wall -pedantic, but is the only possible solution. */
static void (*GetGlFunc(const char* s))(void)
{
/* suppress warning: ISO C forbids conversion of object pointer to function pointer type [-pedantic] */
#ifdef __GNUC__
	__extension__
#endif
	void(*f)(void) = (void(*)(void))SDL_GL_GetProcAddress(s);
	if (f == NULL)
		Log_print("Unable to get function pointer for %s\n",s);
	return f;
}

/* Alocates memory for the screen texture, if needed. */
static void AllocTexture(void)
{
	if (!SDL_VIDEO_GL_pbo && screen_texture == NULL)
		/* The largest width is in NTSC-filtered full overscan mode - 672 pixels.
		   The largest height is in PAL XEP-80 mode - 300 pixels. Add 1 pixel at each side
		   to nicely render screen borders. The texture is 1024x512, which is more than
		   enough - although it's rounded to powers of 2 to be more compatible (earlier
		   versions of OpenGL supported only textures with width/height of powers of 2). */
		screen_texture = Util_malloc(1024*512*(bpp_32 ? sizeof(Uint32) : sizeof(Uint16)));
}

/* Frees memory for the screen texture, if needed. */
static void FreeTexture(void)
{
	if (screen_texture != NULL) {
		free(screen_texture);
		screen_texture = NULL;
	}
}

/* Sets up the initial parameters of the OpenGL context. See also CleanGlContext. */
static void InitGlContext(void)
{
	GLint filtering = SDL_VIDEO_GL_filtering ? GL_LINEAR : GL_NEAREST;
#if SDL2
	filtering = GL_NEAREST;
#endif
	gl.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	gl.Clear(GL_COLOR_BUFFER_BIT);

	gl.Enable(GL_TEXTURE_2D);
	gl.GenTextures(2, textures);

	/* Screen texture. */
	gl.BindTexture(GL_TEXTURE_2D, textures[0]);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filtering);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filtering);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

	/* Scanlines texture. */
	filtering = SDL_VIDEO_interpolate_scanlines ? GL_LINEAR : GL_NEAREST;
	gl.BindTexture(GL_TEXTURE_2D, textures[1]);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filtering);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filtering);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	gl.MatrixMode(GL_PROJECTION);
	gl.LoadIdentity();
	gl.Ortho(-1.0, 1.0, -1.0, 1.0, 0.0, 10.0);
	gl.MatrixMode(GL_MODELVIEW);
	gl.LoadIdentity();
	screen_dlist = gl.GenLists(1);
	if (SDL_VIDEO_GL_pbo)
		gl.GenBuffersARB(1, &screen_pbo);
}

/* Cleans up the structures allocated in InitGlContext. */
static void CleanGlContext(void)
{
	if (!gl.DeleteLists) return;

#if SDL2
	CrtRoyale_Destroy();
#endif
		if (SDL_VIDEO_GL_pbo)
			gl.DeleteBuffersARB(1, &screen_pbo);
		gl.DeleteLists(screen_dlist, 1);
		gl.DeleteTextures(2, textures);
}

/* Sets up the initial parameters of all used textures and the PBO. */
static void InitGlTextures(void)
{
	/* Texture for the display surface. */
	gl.BindTexture(GL_TEXTURE_2D, textures[0]);
	gl.TexImage2D(GL_TEXTURE_2D, 0, pixel_formats[SDL_VIDEO_GL_pixel_format].internal_format, 1024, 512, 0,
	              pixel_formats[SDL_VIDEO_GL_pixel_format].format, pixel_formats[SDL_VIDEO_GL_pixel_format].type,
	              NULL);
	/* Texture for scanlines. */
	gl.BindTexture(GL_TEXTURE_2D, textures[1]);
	if (bpp_32)
		gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 2, 0,
		              GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV,
		              scanline_tex32);
	else
		gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 2, 0,
		              GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV,
		              scanline_tex16);
	if (SDL_VIDEO_GL_pbo) {
		gl.BindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, screen_pbo);
		gl.BufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB, 1024*512*(bpp_32 ? sizeof(Uint32) : sizeof(Uint16)), NULL, GL_DYNAMIC_DRAW_ARB);
		gl.BindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
	}
}

void SDL_VIDEO_GL_Cleanup(void)
{
	if (SDL_VIDEO_screen != NULL && (SDL_VIDEO_screen->flags & SDL_OpenGL_FLAG) == SDL_OpenGL_FLAG)
		CleanGlContext();
	FreeTexture();
}

void SDL_VIDEO_GL_GetPixelFormat(PLATFORM_pixel_format_t *format)
{
	format->bpp = bpp_32 ? 32 : 16;
	format->rmask = pixel_formats[SDL_VIDEO_GL_pixel_format].rmask;
	format->gmask = pixel_formats[SDL_VIDEO_GL_pixel_format].gmask;
	format->bmask = pixel_formats[SDL_VIDEO_GL_pixel_format].bmask;
}

void SDL_VIDEO_GL_MapRGB(void *dest, int const *palette, int size)
{
	(*pixel_formats[SDL_VIDEO_GL_pixel_format].calc_pal_func)(dest, palette, size);
}

/* Calculate the palette in the 32-bit BGRA format, or 16-bit BGR 5-6-5 format. */
static void UpdatePaletteLookup(VIDEOMODE_MODE_t mode)
{
	SDL_VIDEO_UpdatePaletteLookup(mode, bpp_32);
}

void SDL_VIDEO_GL_PaletteUpdate(void)
{
	UpdatePaletteLookup(SDL_VIDEO_current_display_mode);
}

/* Set parameters that will shift the screen and scanline textures a bit,
   in order to look better/cleaner on screen. */
static void SetSubpixelShifts(void)
{
	int dest_width;
	int dest_height;
	int vmult, hmult;
	if (currently_rotated) {
		dest_width = VIDEOMODE_dest_height;
		dest_height = VIDEOMODE_dest_width;
	} else {
		dest_width = VIDEOMODE_dest_width;
		dest_height = VIDEOMODE_dest_height;
	}
	vmult = dest_height / VIDEOMODE_src_height;
	hmult = dest_width / VIDEOMODE_src_width;

	paint_scanlines = vmult >= 2 && SDL_VIDEO_scanlines_percentage != 0;

	if (dest_height % VIDEOMODE_src_height == 0 &&
	    SDL_VIDEO_GL_filtering &&
	    !(vmult & 1))
		screen_vshift = 0.5 / vmult;
	else
		screen_vshift = 0.0;

	
	if (dest_height % VIDEOMODE_src_height == 0 &&
	    ((SDL_VIDEO_interpolate_scanlines && !(vmult & 1)) ||
	     (!SDL_VIDEO_interpolate_scanlines && (vmult & 3) == 3)
	    )
	   )
		scanline_vshift = -0.25 + 0.5 / vmult;
	else
		scanline_vshift = -0.25;

	if (dest_width % VIDEOMODE_src_width == 0 &&
	    SDL_VIDEO_GL_filtering &&
	    !(hmult & 1))
		screen_hshift = 0.5 / hmult;
	else
		screen_hshift = 0.0;
}

/* Sets up the GL Display List that creates a textured rectangle of the main
   screen and a second, translucent, rectangle with scanlines. */
static void SetGlDisplayList(void)
{
#if SDL2
	return;
#endif
	gl.NewList(screen_dlist, GL_COMPILE);
	gl.Clear(GL_COLOR_BUFFER_BIT);
	gl.BindTexture(GL_TEXTURE_2D, textures[0]);
	gl.Begin(GL_QUADS);
	if (currently_rotated) {
		gl.TexCoord2f(screen_hshift/1024.0f, ((GLfloat)VIDEOMODE_src_height + screen_vshift)/512.0f);
		gl.Vertex3f(1.0f, -1.0f, -2.0f);
		gl.TexCoord2f(((GLfloat)VIDEOMODE_actual_width + screen_hshift)/1024.0f, ((GLfloat)VIDEOMODE_src_height + screen_vshift)/512.0f);
		gl.Vertex3f(1.0f, 1.0f, -2.0f);
		gl.TexCoord2f(((GLfloat)VIDEOMODE_actual_width + screen_hshift)/1024.0f, screen_vshift/512.0f);
		gl.Vertex3f(-1.0f, 1.0f, -2.0f);
		gl.TexCoord2f(screen_hshift/1024.0f, screen_vshift/512.0f);
		gl.Vertex3f(-1.0f, -1.0f, -2.0f);
	} else {
		gl.TexCoord2f(screen_hshift/1024.0f, ((GLfloat)VIDEOMODE_src_height + screen_vshift)/512.0f);
		gl.Vertex3f(-1.0f, -1.0f, -2.0f);
		gl.TexCoord2f(((GLfloat)VIDEOMODE_actual_width + screen_hshift)/1024.0f, ((GLfloat)VIDEOMODE_src_height + screen_vshift)/512.0f);
		gl.Vertex3f(1.0f, -1.0f, -2.0f);
		gl.TexCoord2f(((GLfloat)VIDEOMODE_actual_width + screen_hshift)/1024.0f, screen_vshift/512.0f);
		gl.Vertex3f(1.0f, 1.0f, -2.0f);
		gl.TexCoord2f(screen_hshift/1024.0f, screen_vshift/512.0f);
		gl.Vertex3f(-1.0f, 1.0f, -2.0f);
	}
	gl.End();
	if (paint_scanlines) {
		gl.Enable(GL_BLEND);
		gl.Color4f(1.0f, 1.0f, 1.0f, ((GLfloat)SDL_VIDEO_scanlines_percentage / 100.0f));
		gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		gl.BindTexture(GL_TEXTURE_2D, textures[1]);
		gl.Begin(GL_QUADS);
		if (currently_rotated) {
			gl.TexCoord2f(0.0f, (GLfloat)VIDEOMODE_src_height + scanline_vshift);
			gl.Vertex3f(1.0f, -1.0f, -1.0f);
			gl.TexCoord2f(1.0f, (GLfloat)VIDEOMODE_src_height + scanline_vshift);
			gl.Vertex3f(1.0f, 1.0f, -1.0f);
			gl.TexCoord2f(1.0f, scanline_vshift);
			gl.Vertex3f(-1.0f, 1.0f, -1.0f);
			gl.TexCoord2f(0.0f, scanline_vshift);
			gl.Vertex3f(-1.0f, -1.0f, -1.0f);
		} else {
			gl.TexCoord2f(0.0f, (GLfloat)VIDEOMODE_src_height + scanline_vshift);
			gl.Vertex3f(-1.0f, -1.0f, -1.0f);
			gl.TexCoord2f(1.0f, (GLfloat)VIDEOMODE_src_height + scanline_vshift);
			gl.Vertex3f(1.0f, -1.0f, -1.0f);
			gl.TexCoord2f(1.0f, scanline_vshift);
			gl.Vertex3f(1.0f, 1.0f, -1.0f);
			gl.TexCoord2f(0.0f, scanline_vshift);
			gl.Vertex3f(-1.0f, 1.0f, -1.0f);
		}
		gl.End();
		gl.Disable(GL_BLEND);
	}
	gl.EndList();
}

/* Resets the screen texture/PBO to all-black. */
static void CleanDisplayTexture(void)
{
	GLvoid *ptr;
	gl.BindTexture(GL_TEXTURE_2D, textures[0]);
	if (SDL_VIDEO_GL_pbo) {
		gl.BindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, screen_pbo);
		ptr = gl.MapBuffer(GL_PIXEL_UNPACK_BUFFER_ARB, GL_WRITE_ONLY_ARB);
	}
	else
		ptr = screen_texture;
	if (bpp_32) {
		Uint32* tex = (Uint32 *)ptr;
		unsigned int i;
		for (i = 0; i < 1024*512; i ++)
			/* Set alpha channel to full opacity. */
			tex[i] = pixel_formats[SDL_VIDEO_GL_pixel_format].black_pixel;

	} else
		memset(ptr, 0x00, 1024*512*sizeof(Uint16));
	if (SDL_VIDEO_GL_pbo) {
		gl.UnmapBuffer(GL_PIXEL_UNPACK_BUFFER_ARB);
		ptr = NULL;
	}
	if (bpp_32)
		gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, 512,
				GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV,
				ptr);
	else
		gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, 512,
				GL_RGB, GL_UNSIGNED_SHORT_5_6_5,
				ptr);
	if (SDL_VIDEO_GL_pbo)
		gl.BindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
}

#if SDL2
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wincompatible-pointer-types"
#endif

/* Sets pointers to OpenGL functions. Returns TRUE on success, FALSE on failure. */
static int InitGlFunctions(void)
{
	if (
#if SDL2
	(gl.GetError = GetGlFunc("glGetError")) == NULL ||
	(gl.CreateShader = GetGlFunc("glCreateShader")) == NULL ||
	(gl.ShaderSource = GetGlFunc("glShaderSource")) == NULL ||
	(gl.CreateProgram = GetGlFunc("glCreateProgram")) == NULL ||
	(gl.CompileShader = GetGlFunc("glCompileShader")) == NULL ||
	(gl.GetShaderiv = GetGlFunc("glGetShaderiv")) == NULL ||
	(gl.GetShaderInfoLog = GetGlFunc("glGetShaderInfoLog")) == NULL ||
	(gl.AttachShader = GetGlFunc("glAttachShader")) == NULL ||
	(gl.LinkProgram = GetGlFunc("glLinkProgram")) == NULL ||
	(gl.GetProgramiv = GetGlFunc("glGetProgramiv")) == NULL ||
	(gl.GetProgramInfoLog = GetGlFunc("glGetProgramInfoLog")) == NULL ||
	(gl.DeleteShader = GetGlFunc("glDeleteShader")) == NULL ||
	(gl.UseProgram = GetGlFunc("glUseProgram")) == NULL ||
	(gl.Uniform1f = GetGlFunc("glUniform1f")) == NULL ||
	(gl.Uniform2f = GetGlFunc("glUniform2f")) == NULL ||
	(gl.Uniform1i = GetGlFunc("glUniform1i")) == NULL ||
	(gl.UniformMatrix4fv = GetGlFunc("glUniformMatrix4fv")) == NULL ||
	(gl.ActiveTexture = GetGlFunc("glActiveTexture")) == NULL ||
	(gl.BindBuffer = GetGlFunc("glBindBuffer")) == NULL ||
	(gl.BufferData = GetGlFunc("glBufferData")) == NULL ||
	(gl.GenBuffers = GetGlFunc("glGenBuffers")) == NULL ||
	(gl.VertexAttribPointer = GetGlFunc("glVertexAttribPointer")) == NULL ||
	(gl.EnableVertexAttribArray = GetGlFunc("glEnableVertexAttribArray")) == NULL ||
	(gl.GenVertexArrays = GetGlFunc("glGenVertexArrays")) == NULL ||
	(gl.BindVertexArray = GetGlFunc("glBindVertexArray")) == NULL ||
	(gl.GetUniformLocation = GetGlFunc("glGetUniformLocation")) == NULL ||
	(gl.GetAttribLocation = GetGlFunc("glGetAttribLocation")) == NULL ||
	(gl.DrawElements = GetGlFunc("glDrawElements")) == NULL ||
#endif
	    (gl.Viewport = (void(APIENTRY*)(GLint,GLint,GLsizei,GLsizei))GetGlFunc("glViewport")) == NULL ||
	    (gl.ClearColor = (void(APIENTRY*)(GLfloat, GLfloat, GLfloat, GLfloat))GetGlFunc("glClearColor")) == NULL ||
	    (gl.Clear = (void(APIENTRY*)(GLbitfield))GetGlFunc("glClear")) == NULL ||
	    (gl.Enable = (void(APIENTRY*)(GLenum))GetGlFunc("glEnable")) == NULL ||
	    (gl.Disable = (void(APIENTRY*)(GLenum))GetGlFunc("glDisable")) == NULL ||
	    (gl.GenTextures = (void(APIENTRY*)(GLsizei, GLuint*))GetGlFunc("glGenTextures")) == NULL ||
	    (gl.DeleteTextures = (void(APIENTRY*)(GLsizei, const GLuint*))GetGlFunc("glDeleteTextures")) == NULL ||
	    (gl.BindTexture = (void(APIENTRY*)(GLenum, GLuint))GetGlFunc("glBindTexture")) == NULL ||
	    (gl.TexParameteri = (void(APIENTRY*)(GLenum, GLenum, GLint))GetGlFunc("glTexParameteri")) == NULL ||
	    (gl.TexImage2D = (void(APIENTRY*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid*))GetGlFunc("glTexImage2D")) == NULL ||
	    (gl.TexSubImage2D = (void(APIENTRY*)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid*))GetGlFunc("glTexSubImage2D")) == NULL ||
	    (gl.TexCoord2f = (void(APIENTRY*)(GLfloat, GLfloat))GetGlFunc("glTexCoord2f")) == NULL ||
	    (gl.Vertex3f = (void(APIENTRY*)(GLfloat, GLfloat, GLfloat))GetGlFunc("glVertex3f")) == NULL ||
	    (gl.Color4f = (void(APIENTRY*)(GLfloat, GLfloat, GLfloat, GLfloat))GetGlFunc("glColor4f")) == NULL ||
	    (gl.BlendFunc = (void(APIENTRY*)(GLenum,GLenum))GetGlFunc("glBlendFunc")) == NULL ||
	    (gl.MatrixMode = (void(APIENTRY*)(GLenum))GetGlFunc("glMatrixMode")) == NULL ||
	    (gl.Ortho = (void(APIENTRY*)(GLdouble,GLdouble,GLdouble,GLdouble,GLdouble,GLdouble))GetGlFunc("glOrtho")) == NULL ||
	    (gl.LoadIdentity = (void(APIENTRY*)(void))GetGlFunc("glLoadIdentity")) == NULL ||
	    (gl.Begin = (void(APIENTRY*)(GLenum))GetGlFunc("glBegin")) == NULL ||
	    (gl.End = (void(APIENTRY*)(void))GetGlFunc("glEnd")) == NULL ||
	    (gl.GetIntegerv = (void(APIENTRY*)(GLenum, GLint*))GetGlFunc("glGetIntegerv")) == NULL ||
	    (gl.GetString = (const GLubyte*(APIENTRY*)(GLenum))GetGlFunc("glGetString")) == NULL ||
	    (gl.GenLists = (GLuint(APIENTRY*)(GLsizei))GetGlFunc("glGenLists")) == NULL ||
	    (gl.DeleteLists = (void(APIENTRY*)(GLuint, GLsizei))GetGlFunc("glDeleteLists")) == NULL ||
	    (gl.NewList = (void(APIENTRY*)(GLuint, GLenum))GetGlFunc("glNewList")) == NULL ||
	    (gl.EndList = (void(APIENTRY*)(void))GetGlFunc("glEndList")) == NULL ||
	    (gl.CallList = (void(APIENTRY*)(GLuint))GetGlFunc("glCallList")) == NULL)
		return FALSE;
	return TRUE;
}

#if SDL2
#pragma GCC diagnostic pop
#endif

/* Checks availability of Pixel Buffer Objests extension and sets pointers of PBO-related OpenGL functions.
   Returns TRUE on success, FALSE on failure. */
static int InitGlPbo(void)
{
#if SDL2
	// OpenGL 3.3 and up
	/*
	int pbo = FALSE;
	GLint n = 0; 
	gl.GetIntegerv(GL_NUM_EXTENSIONS, &n);

	PFNGLGETSTRINGIPROC glGetStringi = (PFNGLGETSTRINGIPROC)GetGlFunc("glGetStringi");
	if (!glGetStringi) return FALSE;

	for (GLint i = 0; i < n; ++i) { 
		const char* extension = (const char*)glGetStringi(GL_EXTENSIONS, i);
		printf("ext: '%s'\n", extension);
		if (strcmp(extension, "GL_ARB_pixel_buffer_object") == 0) {
			pbo = TRUE;
			break;
		}
	} 
	if (!pbo) return FALSE;
	*/
	return FALSE;
#else
	const GLubyte *extensions = gl.GetString(GL_EXTENSIONS);
	if (!strstr((char *)extensions, "EXT_pixel_buffer_object")) {
		return FALSE;
	}
#endif
	if ((gl.GenBuffersARB = (void(APIENTRY*)(GLsizei, GLuint*))GetGlFunc("glGenBuffersARB")) == NULL ||
	    (gl.DeleteBuffersARB = (void(APIENTRY*)(GLsizei, const GLuint*))GetGlFunc("glDeleteBuffersARB")) == NULL ||
	    (gl.BindBufferARB = (void(APIENTRY*)(GLenum, GLuint))GetGlFunc("glBindBufferARB")) == NULL ||
	    (gl.BufferDataARB = (void(APIENTRY*)(GLenum, GLsizeiptr, const GLvoid*, GLenum))GetGlFunc("glBufferDataARB")) == NULL ||
	    (gl.MapBuffer = (void*(APIENTRY*)(GLenum, GLenum))GetGlFunc("glMapBufferARB")) == NULL ||
	    (gl.UnmapBuffer = (GLboolean(APIENTRY*)(GLenum))GetGlFunc("glUnmapBufferARB")) == NULL)
		return FALSE;

	return TRUE;
}

static void ModeInfo(void)
{
	const char *fullstring = (SDL_VIDEO_screen->flags & SDL_OpenGL_FULLSCREEN) == SDL_OpenGL_FULLSCREEN ? "fullscreen" : "windowed";
	Log_print("Video Mode: %dx%dx%d %s, pixel format: %s", SDL_VIDEO_screen->w, SDL_VIDEO_screen->h,
		   SDL_VIDEO_screen->format->BitsPerPixel, fullstring, pixel_format_cfg_strings[SDL_VIDEO_GL_pixel_format]);
}


/* Return value of TRUE indicates that the video subsystem was reinitialised. */
static int SetVideoMode(int w, int h, int windowed)
{
	int reinit = FALSE;
#if SDL2
	static SDL_Surface OpenGL_screen_dummy;
	static SDL_PixelFormat pix;

	Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN;
	if (!windowed) {
		flags |= SDL_WINDOW_FULLSCREEN;
	}

	if (SDL_VIDEO_wnd && (!SDL_VIDEO_screen || SDL_VIDEO_screen->flags != flags)) {
		SDL_DestroyWindow(SDL_VIDEO_wnd);
		SDL_VIDEO_wnd = 0;
	}

	if (!SDL_VIDEO_wnd) {
		SDL_VIDEO_wnd = SDL_CreateWindow(Atari800_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
		if (!SDL_VIDEO_wnd) {
			Log_print("Creating an OpenGL window with size %dx%d failed: %s", w, h, SDL_GetError());
			Log_flushlog();
			exit(-1);
		}

		SDL_GLContext ctx = SDL_GL_CreateContext(SDL_VIDEO_wnd);
		if (!ctx) {
			Log_print("OpenGL context could not be created: %s", SDL_GetError());
			Log_flushlog();
			exit(-1);
		}
	
		memset(&OpenGL_screen_dummy, 0, sizeof(OpenGL_screen_dummy));
		memset(&pix, 0, sizeof(pix));
		SDL_VIDEO_screen = &OpenGL_screen_dummy;
		SDL_VIDEO_screen->format = &pix;
		SDL_VIDEO_screen->w = w;
		SDL_VIDEO_screen->h = h;
		SDL_VIDEO_screen->flags = flags;

		reinit = TRUE;
	}
	else {
		int cw, ch;
		SDL_GetWindowSize(SDL_VIDEO_wnd, &cw, &ch);
		if (w != cw || h != ch) {
			SDL_SetWindowSize(SDL_VIDEO_wnd, w, h);
		}
	}

	int width = 0, height = 0;
	SDL_GL_GetDrawableSize(SDL_VIDEO_wnd, &width, &height);
	SDL_VIDEO_width = width;
	SDL_VIDEO_height = height;
	screen_width = width;
	screen_height = height;
	VIDEOMODE_dest_scale_factor = (double)width / w;

	SDL_VIDEO_vsync_available = TRUE;
	if (SDL_VIDEO_vsync) {
		SDL_GL_SetSwapInterval(1); // VSync
	}

#else
	Uint32 flags = SDL_OPENGL | (windowed ? SDL_RESIZABLE : SDL_OpenGL_FULLSCREEN);
	/* In OpenGL mode, the SDL screen is always opened with the default
	   desktop depth - it is the most compatible way. */
	SDL_VIDEO_screen = SDL_SetVideoMode(w, h, SDL_VIDEO_native_bpp, flags);
	if (SDL_VIDEO_screen == NULL) {
		/* Some SDL_SetVideoMode errors can be averted by reinitialising the SDL video subsystem. */
		Log_print("Setting video mode: %dx%dx%d failed: %s. Reinitialising video.", w, h, SDL_VIDEO_native_bpp, SDL_GetError());
		SDL_VIDEO_ReinitSDL();
		reinit = TRUE;
		SDL_VIDEO_screen = SDL_SetVideoMode(w, h, SDL_VIDEO_native_bpp, flags);
		if (SDL_VIDEO_screen == NULL) {
			Log_print("Setting Video Mode: %dx%dx%d failed: %s", w, h, SDL_VIDEO_native_bpp, SDL_GetError());
			Log_flushlog();
			exit(-1);
		}
	}
	SDL_VIDEO_width = SDL_VIDEO_screen->w;
	SDL_VIDEO_height = SDL_VIDEO_screen->h;
	SDL_VIDEO_vsync_available = FALSE;
#endif
	ModeInfo();
	return reinit;
}

#if SDL2

static GLuint progID = 0;
static GLuint buffers[3];
static GLuint vaos[1];
static GLint our_texture;
static GLint sh_scanlines;
static GLint sh_curvature;
static GLint sh_aPos;
static GLint sh_aTexCoord;
static GLint sh_vp_matrix;
static GLint sh_resolution;
static GLint sh_pixelSpread;
static GLint sh_glow;

#define CRT_ROYALE_PASS_COUNT 9
#define CRT_ROYALE_MAX_PASS_PREV 6
#define CRT_ROYALE_MASK_TEXTURES 6

typedef struct {
	GLint mvp;
	GLint frame_count;
	GLint frame_direction;
	GLint output_size;
	GLint input_size;
	GLint texture_size;
	GLint texture;
	GLint pass_prev_tex[CRT_ROYALE_MAX_PASS_PREV];
	GLint pass_prev_input_size[CRT_ROYALE_MAX_PASS_PREV];
	GLint pass_prev_texture_size[CRT_ROYALE_MAX_PASS_PREV];
	GLint mask_tex[CRT_ROYALE_MASK_TEXTURES];
} CrtRoyaleUniforms;

static int crt_royale_ready = FALSE;
static int crt_royale_failed = FALSE;
static int crt_royale_frame_count = 0;
static int crt_royale_input_w = 0;
static int crt_royale_input_h = 0;
static int crt_royale_viewport_w = 0;
static int crt_royale_viewport_h = 0;
static int crt_royale_glsl_version = 130;
static int crt_royale_pass_w[CRT_ROYALE_PASS_COUNT];
static int crt_royale_pass_h[CRT_ROYALE_PASS_COUNT];
static int crt_royale_use_mipmaps = FALSE;
static GLuint crt_royale_programs[CRT_ROYALE_PASS_COUNT];
static CrtRoyaleUniforms crt_royale_uniforms[CRT_ROYALE_PASS_COUNT];
static GLuint crt_royale_fbos[CRT_ROYALE_PASS_COUNT];
static GLuint crt_royale_textures[CRT_ROYALE_PASS_COUNT];
static GLuint crt_royale_mask_textures[CRT_ROYALE_MASK_TEXTURES];
static GLuint crt_royale_vao = 0;
static GLuint crt_royale_vbo_pos = 0;
static GLuint crt_royale_vbo_uv = 0;
static GLuint crt_royale_vbo_color = 0;
static GLuint crt_royale_ebo = 0;

#define	TEX_WIDTH	1024
#define	TEX_HEIGHT	512
#define	WIDTH		320
#define	HEIGHT		Screen_HEIGHT

static const int kVertexCount = 4;
static const int kIndexCount = 6;

static const GLfloat vertices[] = {
	-1.0f, -1.0f, 0.0f,
	+1.0f, -1.0f, 0.0f,
	+1.0f, +1.0f, 0.0f,
	-1.0f, +1.0f, 0.0f,
};

static const GLushort indices[] =
{
	0, 1, 2,
	0, 2, 3,
};

static void SDL_set_up_uvs(void) {
	float min_u	= 0.0f;
	float max_u	= (float)VIDEOMODE_custom_horizontal_area / TEX_WIDTH;
	float min_v	= 0.0f;
	float max_v	= (float)VIDEOMODE_custom_vertical_area / TEX_HEIGHT;
	const GLfloat uvs[] = {
		min_u, min_v,
		max_u, min_v,
		max_u, max_v,
		min_u, max_v,
	};
	gl.BindBuffer(GL_ARRAY_BUFFER, buffers[1]);
	gl.BufferData(GL_ARRAY_BUFFER, kVertexCount * sizeof(GLfloat) * 2, uvs, GL_STATIC_DRAW);
	gl.VertexAttribPointer(sh_aTexCoord, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), NULL);
}

static void SDL_set_up_opengl(void) {
	gl.GenTextures(2, textures);
	gl.BindTexture(GL_TEXTURE_2D, textures[0]);
	// gl.TexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, TEX_WIDTH, TEX_HEIGHT, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
	// gl.BindTexture(GL_TEXTURE_2D, textures[1]); // color palette
	// gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 256, 1, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, NULL);
	our_texture = gl.GetUniformLocation(progID, "ourTexture");
	sh_scanlines = gl.GetUniformLocation(progID, "scanlinesFactor");
	sh_curvature = gl.GetUniformLocation(progID, "screenCurvature");
	sh_aPos = gl.GetAttribLocation(progID, "aPos");
	sh_aTexCoord = gl.GetAttribLocation(progID, "aTexCoord");
	sh_vp_matrix = gl.GetUniformLocation(progID, "u_vp_matrix");
	sh_resolution = gl.GetUniformLocation(progID, "u_resolution");
	sh_pixelSpread = gl.GetUniformLocation(progID, "u_pixelSpread");
	sh_glow = gl.GetUniformLocation(progID, "u_glowCoeff");

	gl.GenBuffers(3, buffers);
	gl.GenVertexArrays(1, vaos);
	gl.BindVertexArray(vaos[0]);
	gl.BindBuffer(GL_ARRAY_BUFFER, buffers[0]);
	gl.BufferData(GL_ARRAY_BUFFER, kVertexCount * sizeof(GLfloat) * 3, vertices, GL_STATIC_DRAW);
	gl.VertexAttribPointer(sh_aPos, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), NULL);
	gl.EnableVertexAttribArray(sh_aPos);

	SDL_set_up_uvs();
	gl.EnableVertexAttribArray(sh_aTexCoord);

	gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[2]);
	gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, kIndexCount * sizeof(GLushort), indices, GL_STATIC_DRAW);
	gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	gl.Disable(GL_DEPTH_TEST);
	gl.Disable(GL_DITHER);
}

static void SetOrtho(float m[4][4], float scale_x, float scale_y, float angle) {
	memset(m, 0, 4*4*sizeof(float));
	m[2][2] = 1.0f;
	m[3][0] = 0;
	m[3][1] = 0;
	m[3][2] = 0;
	m[3][3] = 1;
	// rotate and scale:
	angle = angle * M_PI / 180;
	double s = sin(angle);
	double c = cos(angle);
	m[0][0] = scale_x * c;
	m[0][1] = scale_x * s;
	m[1][0] = -scale_y * -s;
	m[1][1] = -scale_y * c;
}

static const char * const crt_royale_shader_paths[CRT_ROYALE_PASS_COUNT] = {
	"sdl/crt-royale/src/crt-royale-first-pass-linearize-crt-gamma-bob-fields.glsl",
	"sdl/crt-royale/src/crt-royale-scanlines-vertical-interlacing.glsl",
	"sdl/crt-royale/src/crt-royale-bloom-approx-fake-bloom.glsl",
	"sdl/crt-royale/shaders/blurs/royale/blur9fast-vertical.glsl",
	"sdl/crt-royale/shaders/blurs/royale/blur9fast-horizontal.glsl",
	"sdl/crt-royale/src/crt-royale-mask-resize-vertical.glsl",
	"sdl/crt-royale/src/crt-royale-mask-resize-horizontal.glsl",
	"sdl/crt-royale/src/crt-royale-scanlines-horizontal-apply-mask-fake-bloom.glsl",
	"sdl/crt-royale/src/crt-royale-geometry-aa-last-pass.glsl"
};

static const char * const crt_royale_mask_uniforms[CRT_ROYALE_MASK_TEXTURES] = {
	"mask_grille_texture_small",
	"mask_grille_texture_large",
	"mask_slot_texture_small",
	"mask_slot_texture_large",
	"mask_shadow_texture_small",
	"mask_shadow_texture_large"
};

enum {
	CRT_ROYALE_ATTR_POS = 0,
	CRT_ROYALE_ATTR_TEX = 1,
	CRT_ROYALE_ATTR_COLOR = 2
};

enum {
	CRT_ROYALE_TEXUNIT_TEXTURE = 0,
	CRT_ROYALE_TEXUNIT_PASS_PREV_BASE = 1,
	CRT_ROYALE_TEXUNIT_MASK_BASE = 7
};

static char *CrtRoyale_ReadShaderFile(const char *path)
{
	const char *prefixes[] = { "", "src/", "../src/", "../../src/" };
	char full[FILENAME_MAX];
	FILE *fp = NULL;
	size_t i;

	for (i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
		snprintf(full, sizeof(full), "%s%s", prefixes[i], path);
		fp = fopen(full, "rb");
		if (fp != NULL)
			break;
	}
	if (fp == NULL)
		return NULL;

	fseek(fp, 0L, SEEK_END);
	long size = ftell(fp);
	rewind(fp);

	char *buffer = (char *)Util_malloc((size_t)size + 1);
	if (buffer == NULL) {
		fclose(fp);
		return NULL;
	}

	if (fread(buffer, (size_t)size, 1, fp) != 1) {
		fclose(fp);
		free(buffer);
		return NULL;
	}
	buffer[size] = 0;
	fclose(fp);

	return buffer;
}

static int CrtRoyale_DetectGlslVersion(void)
{
	const char *ver = (const char *)gl.GetString(GL_SHADING_LANGUAGE_VERSION);
	int major = 0;
	int minor = 0;
	int combined;

	if (!ver)
		return 120;
	if (sscanf(ver, "%d.%d", &major, &minor) < 1)
		return 120;

	combined = major * 100 + minor;
	if (combined >= 150)
		return 150;
	if (combined >= 130)
		return 130;
	if (combined >= 120)
		return 120;
	return 110;
}

static char *CrtRoyale_PrependDefine(const char *source, const char *define_name)
{
	const char *newline = strchr(source, '\n');
	size_t define_len;
	char define_line[64];
	char version_line[32];
	size_t head_len;
	size_t tail_len;
	char *out;

	if (newline == NULL)
		return NULL;

	snprintf(version_line, sizeof(version_line), "#version %d\n", crt_royale_glsl_version);
	snprintf(define_line, sizeof(define_line), "#define %s\n", define_name);
	define_len = strlen(define_line);
	head_len = strlen(version_line);
	tail_len = strlen(newline + 1);

	out = (char *)Util_malloc(head_len + define_len + tail_len + 1);
	if (out == NULL)
		return NULL;

	memcpy(out, version_line, head_len);
	memcpy(out + head_len, define_line, define_len);
	memcpy(out + head_len + define_len, newline + 1, tail_len);
	out[head_len + define_len + tail_len] = 0;
	return out;
}

static GLuint CrtRoyale_CompileShader(GLenum type, const char *source, const char *label)
{
	GLint success = 0;
	GLuint shader = gl.CreateShader(type);

	gl.ShaderSource(shader, 1, (GLchar * const *)&source, NULL);
	gl.CompileShader(shader);
	gl.GetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success) {
		char buf[1024];
		gl.GetShaderInfoLog(shader, sizeof(buf), NULL, buf);
		Log_print("Cannot compile CRT-Royale shader (%s): %s", label, buf);
		gl.DeleteShader(shader);
		return 0;
	}
	return shader;
}

static void CrtRoyale_InitUniforms(GLuint program, CrtRoyaleUniforms *uniforms)
{
	int i;

	memset(uniforms, 0xff, sizeof(*uniforms));
	uniforms->mvp = gl.GetUniformLocation(program, "MVPMatrix");
	uniforms->frame_count = gl.GetUniformLocation(program, "FrameCount");
	uniforms->frame_direction = gl.GetUniformLocation(program, "FrameDirection");
	uniforms->output_size = gl.GetUniformLocation(program, "OutputSize");
	uniforms->input_size = gl.GetUniformLocation(program, "InputSize");
	uniforms->texture_size = gl.GetUniformLocation(program, "TextureSize");
	uniforms->texture = gl.GetUniformLocation(program, "Texture");

	for (i = 0; i < CRT_ROYALE_MAX_PASS_PREV; i++) {
		char name[64];
		snprintf(name, sizeof(name), "PassPrev%dTexture", i + 1);
		uniforms->pass_prev_tex[i] = gl.GetUniformLocation(program, name);
		snprintf(name, sizeof(name), "PassPrev%dInputSize", i + 1);
		uniforms->pass_prev_input_size[i] = gl.GetUniformLocation(program, name);
		snprintf(name, sizeof(name), "PassPrev%dTextureSize", i + 1);
		uniforms->pass_prev_texture_size[i] = gl.GetUniformLocation(program, name);
	}
	for (i = 0; i < CRT_ROYALE_MASK_TEXTURES; i++)
		uniforms->mask_tex[i] = gl.GetUniformLocation(program, crt_royale_mask_uniforms[i]);

	gl.UseProgram(program);
	if (uniforms->texture >= 0)
		gl.Uniform1i(uniforms->texture, CRT_ROYALE_TEXUNIT_TEXTURE);
	for (i = 0; i < CRT_ROYALE_MAX_PASS_PREV; i++) {
		if (uniforms->pass_prev_tex[i] >= 0)
			gl.Uniform1i(uniforms->pass_prev_tex[i], CRT_ROYALE_TEXUNIT_PASS_PREV_BASE + i);
	}
	for (i = 0; i < CRT_ROYALE_MASK_TEXTURES; i++) {
		if (uniforms->mask_tex[i] >= 0)
			gl.Uniform1i(uniforms->mask_tex[i], CRT_ROYALE_TEXUNIT_MASK_BASE + i);
	}
	gl.UseProgram(0);
}

static GLuint CrtRoyale_CreateProgram(const char *path, CrtRoyaleUniforms *uniforms)
{
	GLuint vertex = 0;
	GLuint fragment = 0;
	GLuint program = 0;
	GLint success = 0;
	char *source = CrtRoyale_ReadShaderFile(path);
	if (source == NULL) {
		Log_print("Missing CRT-Royale shader file '%s'", path);
		return 0;
	}
	char *vertex_source = CrtRoyale_PrependDefine(source, "VERTEX");
	char *fragment_source = CrtRoyale_PrependDefine(source, "FRAGMENT");

	free(source);
	if (vertex_source == NULL || fragment_source == NULL) {
		free(vertex_source);
		free(fragment_source);
		return 0;
	}

	vertex = CrtRoyale_CompileShader(GL_VERTEX_SHADER, vertex_source, path);
	fragment = CrtRoyale_CompileShader(GL_FRAGMENT_SHADER, fragment_source, path);
	free(vertex_source);
	free(fragment_source);

	if (vertex == 0 || fragment == 0) {
		if (vertex != 0)
			gl.DeleteShader(vertex);
		if (fragment != 0)
			gl.DeleteShader(fragment);
		return 0;
	}

	program = gl.CreateProgram();
	gl.AttachShader(program, vertex);
	gl.AttachShader(program, fragment);
	if (gl.BindAttribLocation) {
		gl.BindAttribLocation(program, CRT_ROYALE_ATTR_POS, "VertexCoord");
		gl.BindAttribLocation(program, CRT_ROYALE_ATTR_TEX, "TexCoord");
		gl.BindAttribLocation(program, CRT_ROYALE_ATTR_COLOR, "COLOR");
	}
	gl.LinkProgram(program);
	gl.GetProgramiv(program, GL_LINK_STATUS, &success);
	if (!success) {
		char buf[1024];
		gl.GetProgramInfoLog(program, sizeof(buf), NULL, buf);
		Log_print("Cannot link CRT-Royale shader program (%s): %s", path, buf);
		gl.DeleteShader(vertex);
		gl.DeleteShader(fragment);
		if (gl.DeleteProgram)
			gl.DeleteProgram(program);
		return 0;
	}

	gl.DeleteShader(vertex);
	gl.DeleteShader(fragment);
	CrtRoyale_InitUniforms(program, uniforms);
	return program;
}

#ifdef DEBUG_SHADERS
static char* read_text_file(const char* path) {
	FILE* fp = fopen(path , "rb");
	if (!fp) return NULL;

	fseek(fp, 0L, SEEK_END);
	long size = ftell(fp);
	rewind(fp);

	char* buffer = malloc(size + 1);
	if (!buffer) {
		fclose(fp);
		return NULL;
	}

	/* copy the file into the buffer */
	if (fread(buffer, size, 1, fp) != 1) {
		fclose(fp);
		free(buffer);
		return NULL;
	}

	buffer[size] = 0;
	fclose(fp);

	return buffer;
}
#endif

static int CrtRoyale_LoadFunctions(void)
{
	if (!gl.BindAttribLocation)
		gl.BindAttribLocation = (void(APIENTRY*)(GLuint, GLuint, const GLchar*))GetGlFunc("glBindAttribLocation");
	if (!gl.GenFramebuffers)
		gl.GenFramebuffers = (void(APIENTRY*)(GLsizei, GLuint*))GetGlFunc("glGenFramebuffers");
	if (!gl.BindFramebuffer)
		gl.BindFramebuffer = (void(APIENTRY*)(GLenum, GLuint))GetGlFunc("glBindFramebuffer");
	if (!gl.FramebufferTexture2D)
		gl.FramebufferTexture2D = (void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint))GetGlFunc("glFramebufferTexture2D");
	if (!gl.CheckFramebufferStatus)
		gl.CheckFramebufferStatus = (GLenum(APIENTRY*)(GLenum))GetGlFunc("glCheckFramebufferStatus");
	if (!gl.DeleteFramebuffers)
		gl.DeleteFramebuffers = (void(APIENTRY*)(GLsizei, const GLuint*))GetGlFunc("glDeleteFramebuffers");
	if (!gl.DeleteProgram)
		gl.DeleteProgram = (void(APIENTRY*)(GLuint))GetGlFunc("glDeleteProgram");
	if (!gl.DeleteVertexArrays)
		gl.DeleteVertexArrays = (void(APIENTRY*)(GLsizei, const GLuint*))GetGlFunc("glDeleteVertexArrays");
	if (!gl.DeleteBuffers)
		gl.DeleteBuffers = (void(APIENTRY*)(GLsizei, const GLuint*))GetGlFunc("glDeleteBuffers");
	if (!gl.GenerateMipmap)
		gl.GenerateMipmap = (void(APIENTRY*)(GLenum))GetGlFunc("glGenerateMipmap");

	if (!gl.BindAttribLocation || !gl.GenFramebuffers || !gl.BindFramebuffer ||
	    !gl.FramebufferTexture2D || !gl.CheckFramebufferStatus)
		return FALSE;

	return TRUE;
}

static void CrtRoyale_Destroy(void)
{
	int i;

	if (gl.DeleteProgram) {
		for (i = 0; i < CRT_ROYALE_PASS_COUNT; i++) {
			if (crt_royale_programs[i]) {
				gl.DeleteProgram(crt_royale_programs[i]);
				crt_royale_programs[i] = 0;
			}
		}
	}
	if (gl.DeleteTextures) {
		gl.DeleteTextures(CRT_ROYALE_MASK_TEXTURES, crt_royale_mask_textures);
		gl.DeleteTextures(CRT_ROYALE_PASS_COUNT - 1, crt_royale_textures);
	}
	if (gl.DeleteFramebuffers)
		gl.DeleteFramebuffers(CRT_ROYALE_PASS_COUNT - 1, crt_royale_fbos);
	if (gl.DeleteVertexArrays && crt_royale_vao)
		gl.DeleteVertexArrays(1, &crt_royale_vao);
	if (gl.DeleteBuffers) {
		GLuint buffers_to_delete[] = {
			crt_royale_vbo_pos, crt_royale_vbo_uv, crt_royale_vbo_color, crt_royale_ebo
		};
		gl.DeleteBuffers((GLsizei)(sizeof(buffers_to_delete) / sizeof(buffers_to_delete[0])), buffers_to_delete);
	}

	memset(crt_royale_fbos, 0, sizeof(crt_royale_fbos));
	memset(crt_royale_textures, 0, sizeof(crt_royale_textures));
	memset(crt_royale_mask_textures, 0, sizeof(crt_royale_mask_textures));
	crt_royale_vao = 0;
	crt_royale_vbo_pos = 0;
	crt_royale_vbo_uv = 0;
	crt_royale_vbo_color = 0;
	crt_royale_ebo = 0;
	crt_royale_input_w = 0;
	crt_royale_input_h = 0;
	crt_royale_viewport_w = 0;
	crt_royale_viewport_h = 0;
	crt_royale_frame_count = 0;
	crt_royale_ready = FALSE;
	crt_royale_failed = FALSE;
}

static void CrtRoyale_SetUVs(float max_u, float max_v)
{
	const GLfloat uvs[] = {
		0.0f, 0.0f, 0.0f, 0.0f,
		max_u, 0.0f, 0.0f, 0.0f,
		max_u, max_v, 0.0f, 0.0f,
		0.0f, max_v, 0.0f, 0.0f
	};

	gl.BindBuffer(GL_ARRAY_BUFFER, crt_royale_vbo_uv);
	gl.BufferData(GL_ARRAY_BUFFER, sizeof(uvs), uvs, GL_STATIC_DRAW);
	gl.VertexAttribPointer(CRT_ROYALE_ATTR_TEX, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), NULL);
}

static void CrtRoyale_CalcViewport(int *vp_x, int *vp_y, int *vp_w, int *vp_h)
{
	float sx = 1.0f;
	float sy = 1.0f;
	float vw = VIDEOMODE_custom_horizontal_area;
	float vh = VIDEOMODE_custom_vertical_area;
	float a = (float)screen_width / (float)screen_height;
	float a0 = currently_rotated ? vh / vw : vw / vh;

	if (a > a0)
		sx = a0 / a;
	else
		sy = a / a0;

	if (VIDEOMODE_GetKeepAspect() == VIDEOMODE_KEEP_ASPECT_REAL) {
		float ar = VIDEOMODE_GetPixelAspectRatio(VIDEOMODE_MODE_NORMAL);
		if (ar > 1.0f)
			sy /= ar;
		else
			sx *= ar;
	}

	*vp_w = (int)(screen_width * sx * zoom_factor + 0.5f);
	*vp_h = (int)(screen_height * sy * zoom_factor + 0.5f);
	if (*vp_w < 1)
		*vp_w = 1;
	if (*vp_h < 1)
		*vp_h = 1;

	*vp_x = (screen_width - *vp_w) / 2;
	*vp_y = (screen_height - *vp_h) / 2;
	if (*vp_x < 0)
		*vp_x = 0;
	if (*vp_y < 0)
		*vp_y = 0;
}

static void CrtRoyale_CalcPassSizes(int input_w, int input_h, int viewport_w, int viewport_h)
{
	int mask_h = (int)(viewport_h * 0.0625f + 0.5f);
	int mask_w = (int)(viewport_w * 0.0625f + 0.5f);

	if (mask_h < 1)
		mask_h = 1;
	if (mask_w < 1)
		mask_w = 1;

	crt_royale_pass_w[0] = input_w;
	crt_royale_pass_h[0] = input_h;
	crt_royale_pass_w[1] = input_w;
	crt_royale_pass_h[1] = viewport_h;
	crt_royale_pass_w[2] = 400;
	crt_royale_pass_h[2] = 300;
	crt_royale_pass_w[3] = crt_royale_pass_w[2];
	crt_royale_pass_h[3] = crt_royale_pass_h[2];
	crt_royale_pass_w[4] = crt_royale_pass_w[2];
	crt_royale_pass_h[4] = crt_royale_pass_h[2];
	crt_royale_pass_w[5] = 64;
	crt_royale_pass_h[5] = mask_h;
	crt_royale_pass_w[6] = mask_w;
	crt_royale_pass_h[6] = mask_h;
	crt_royale_pass_w[7] = viewport_w;
	crt_royale_pass_h[7] = viewport_h;
	crt_royale_pass_w[8] = viewport_w;
	crt_royale_pass_h[8] = viewport_h;
}

static void CrtRoyale_SetOutputTextureParams(int pass)
{
	GLint min_filter = GL_LINEAR;
	GLint mag_filter = GL_LINEAR;

	if (pass == 5) {
		min_filter = GL_NEAREST;
		mag_filter = GL_NEAREST;
	}
	if (pass == 7 && crt_royale_use_mipmaps)
		min_filter = GL_LINEAR_MIPMAP_LINEAR;

	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static int CrtRoyale_EnsureTargets(void)
{
	int i;

	if (!crt_royale_textures[0])
		gl.GenTextures(CRT_ROYALE_PASS_COUNT - 1, crt_royale_textures);
	if (!crt_royale_fbos[0])
		gl.GenFramebuffers(CRT_ROYALE_PASS_COUNT - 1, crt_royale_fbos);

	for (i = 0; i < CRT_ROYALE_PASS_COUNT - 1; i++) {
		gl.BindTexture(GL_TEXTURE_2D, crt_royale_textures[i]);
		CrtRoyale_SetOutputTextureParams(i);
		gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, crt_royale_pass_w[i], crt_royale_pass_h[i], 0,
		              GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		gl.BindFramebuffer(GL_FRAMEBUFFER, crt_royale_fbos[i]);
		gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, crt_royale_textures[i], 0);
		if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			Log_print("CRT-Royale framebuffer incomplete on pass %d", i);
			gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
			return FALSE;
		}
	}
	gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
	return TRUE;
}

static void CrtRoyale_BindMaskTextures(void)
{
	int i;

	for (i = 0; i < CRT_ROYALE_MASK_TEXTURES; i++) {
		gl.ActiveTexture(GL_TEXTURE0 + CRT_ROYALE_TEXUNIT_MASK_BASE + i);
		gl.BindTexture(GL_TEXTURE_2D, crt_royale_mask_textures[i]);
	}
	gl.ActiveTexture(GL_TEXTURE0);
}

static void CrtRoyale_BindPassTextures(int pass_index, GLuint input_texture)
{
	int i;

	gl.ActiveTexture(GL_TEXTURE0 + CRT_ROYALE_TEXUNIT_TEXTURE);
	gl.BindTexture(GL_TEXTURE_2D, input_texture);

	for (i = 0; i < CRT_ROYALE_MAX_PASS_PREV; i++) {
		int prev = pass_index - (i + 1);
		GLuint tex = 0;
		if (prev >= 0)
			tex = crt_royale_textures[prev];
		gl.ActiveTexture(GL_TEXTURE0 + CRT_ROYALE_TEXUNIT_PASS_PREV_BASE + i);
		gl.BindTexture(GL_TEXTURE_2D, tex);
	}
	gl.ActiveTexture(GL_TEXTURE0);
}

static void CrtRoyale_SetPassUniforms(int pass_index, int input_w, int input_h,
                                      int texture_w, int texture_h, int output_w, int output_h,
                                      const float mvp[4][4])
{
	int i;
	CrtRoyaleUniforms *uniforms = &crt_royale_uniforms[pass_index];

	if (uniforms->mvp >= 0)
		gl.UniformMatrix4fv(uniforms->mvp, 1, GL_FALSE, &mvp[0][0]);
	if (uniforms->frame_count >= 0)
		gl.Uniform1i(uniforms->frame_count, crt_royale_frame_count);
	if (uniforms->frame_direction >= 0)
		gl.Uniform1i(uniforms->frame_direction, 1);
	if (uniforms->input_size >= 0)
		gl.Uniform2f(uniforms->input_size, (float)input_w, (float)input_h);
	if (uniforms->texture_size >= 0)
		gl.Uniform2f(uniforms->texture_size, (float)texture_w, (float)texture_h);
	if (uniforms->output_size >= 0)
		gl.Uniform2f(uniforms->output_size, (float)output_w, (float)output_h);

	for (i = 0; i < CRT_ROYALE_MAX_PASS_PREV; i++) {
		int prev = pass_index - (i + 1);
		if (prev < 0)
			continue;
		if (uniforms->pass_prev_input_size[i] >= 0)
			gl.Uniform2f(uniforms->pass_prev_input_size[i],
			             (float)crt_royale_pass_w[prev], (float)crt_royale_pass_h[prev]);
		if (uniforms->pass_prev_texture_size[i] >= 0)
			gl.Uniform2f(uniforms->pass_prev_texture_size[i],
			             (float)crt_royale_pass_w[prev], (float)crt_royale_pass_h[prev]);
	}
}

static int CrtRoyale_Init(void)
{
	static const GLfloat vertices[] = {
		-1.0f, -1.0f, 0.0f, 1.0f,
		+1.0f, -1.0f, 0.0f, 1.0f,
		+1.0f, +1.0f, 0.0f, 1.0f,
		-1.0f, +1.0f, 0.0f, 1.0f
	};
	static const GLfloat colors[] = {
		1.0f, 1.0f, 1.0f, 1.0f,
		1.0f, 1.0f, 1.0f, 1.0f,
		1.0f, 1.0f, 1.0f, 1.0f,
		1.0f, 1.0f, 1.0f, 1.0f
	};
	int i;

	if (crt_royale_ready)
		return TRUE;
	if (!CrtRoyale_LoadFunctions())
		return FALSE;
	crt_royale_glsl_version = CrtRoyale_DetectGlslVersion();

	gl.GenVertexArrays(1, &crt_royale_vao);
	gl.BindVertexArray(crt_royale_vao);
	gl.GenBuffers(1, &crt_royale_vbo_pos);
	gl.BindBuffer(GL_ARRAY_BUFFER, crt_royale_vbo_pos);
	gl.BufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
	gl.VertexAttribPointer(CRT_ROYALE_ATTR_POS, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), NULL);
	gl.EnableVertexAttribArray(CRT_ROYALE_ATTR_POS);

	gl.GenBuffers(1, &crt_royale_vbo_uv);
	CrtRoyale_SetUVs(1.0f, 1.0f);
	gl.EnableVertexAttribArray(CRT_ROYALE_ATTR_TEX);

	gl.GenBuffers(1, &crt_royale_vbo_color);
	gl.BindBuffer(GL_ARRAY_BUFFER, crt_royale_vbo_color);
	gl.BufferData(GL_ARRAY_BUFFER, sizeof(colors), colors, GL_STATIC_DRAW);
	gl.VertexAttribPointer(CRT_ROYALE_ATTR_COLOR, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), NULL);
	gl.EnableVertexAttribArray(CRT_ROYALE_ATTR_COLOR);

	gl.GenBuffers(1, &crt_royale_ebo);
	gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, crt_royale_ebo);
	gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
	gl.BindVertexArray(0);

	memset(crt_royale_programs, 0, sizeof(crt_royale_programs));
	for (i = 0; i < CRT_ROYALE_PASS_COUNT; i++) {
		crt_royale_programs[i] = CrtRoyale_CreateProgram(crt_royale_shader_paths[i], &crt_royale_uniforms[i]);
		if (!crt_royale_programs[i]) {
			CrtRoyale_Destroy();
			return FALSE;
		}
	}

	gl.GenTextures(CRT_ROYALE_MASK_TEXTURES, crt_royale_mask_textures);
	for (i = 0; i < CRT_ROYALE_MASK_TEXTURES; i++) {
		const CRT_RoyaleMask *mask = &CRT_ROYALE_masks[i];
		int is_large = mask->width >= 256;
		gl.BindTexture(GL_TEXTURE_2D, crt_royale_mask_textures[i]);
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		if (is_large && gl.GenerateMipmap)
			gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		else
			gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, mask->width, mask->height, 0,
		              GL_RGB, GL_UNSIGNED_BYTE, mask->data);
		if (is_large && gl.GenerateMipmap)
			gl.GenerateMipmap(GL_TEXTURE_2D);
	}

	crt_royale_use_mipmaps = gl.GenerateMipmap != NULL;
	crt_royale_ready = TRUE;
	crt_royale_failed = FALSE;
	return TRUE;
}

static int CrtRoyale_Display(void)
{
	int input_w;
	int input_h;
	int vp_x;
	int vp_y;
	int vp_w;
	int vp_h;
	float proj[4][4];
	int pass;

	if (crt_royale_failed)
		return FALSE;
	if (!CrtRoyale_Init()) {
		crt_royale_failed = TRUE;
		return FALSE;
	}

	input_w = (int)VIDEOMODE_custom_horizontal_area;
	input_h = (int)VIDEOMODE_custom_vertical_area;
	if (input_w <= 0 || input_h <= 0)
		return FALSE;

	CrtRoyale_CalcViewport(&vp_x, &vp_y, &vp_w, &vp_h);
	if (input_w != crt_royale_input_w || input_h != crt_royale_input_h ||
	    vp_w != crt_royale_viewport_w || vp_h != crt_royale_viewport_h) {
		crt_royale_input_w = input_w;
		crt_royale_input_h = input_h;
		crt_royale_viewport_w = vp_w;
		crt_royale_viewport_h = vp_h;
		CrtRoyale_CalcPassSizes(input_w, input_h, vp_w, vp_h);
		if (!CrtRoyale_EnsureTargets()) {
			crt_royale_failed = TRUE;
			return FALSE;
		}
	}

	gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
	gl.Viewport(0, 0, screen_width, screen_height);
	gl.Clear(GL_COLOR_BUFFER_BIT);

	gl.BindTexture(GL_TEXTURE_2D, textures[0]);
	(*blit_funcs[SDL_VIDEO_current_display_mode])(screen_texture);
	gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, VIDEOMODE_actual_width, VIDEOMODE_src_height,
	                 pixel_formats[SDL_VIDEO_GL_pixel_format].format,
	                 pixel_formats[SDL_VIDEO_GL_pixel_format].type,
	                 screen_texture);

	gl.Disable(GL_BLEND);
	gl.BindVertexArray(crt_royale_vao);

	CrtRoyale_SetUVs((float)VIDEOMODE_custom_horizontal_area / TEX_WIDTH,
	                 (float)VIDEOMODE_custom_vertical_area / TEX_HEIGHT);

	for (pass = 0; pass < CRT_ROYALE_PASS_COUNT - 1; pass++) {
		GLuint input_texture = (pass == 0) ? textures[0] : crt_royale_textures[pass - 1];
		int pass_input_w = (pass == 0) ? input_w : crt_royale_pass_w[pass - 1];
		int pass_input_h = (pass == 0) ? input_h : crt_royale_pass_h[pass - 1];
		int pass_tex_w = (pass == 0) ? TEX_WIDTH : pass_input_w;
		int pass_tex_h = (pass == 0) ? TEX_HEIGHT : pass_input_h;

		if (pass == 1)
			CrtRoyale_SetUVs(1.0f, 1.0f);

		SetOrtho(proj, 1.0f, -1.0f, 0.0f);
		gl.UseProgram(crt_royale_programs[pass]);
		CrtRoyale_SetPassUniforms(pass, pass_input_w, pass_input_h,
		                          pass_tex_w, pass_tex_h,
		                          crt_royale_pass_w[pass], crt_royale_pass_h[pass], proj);
		CrtRoyale_BindPassTextures(pass, input_texture);
		CrtRoyale_BindMaskTextures();

		gl.BindFramebuffer(GL_FRAMEBUFFER, crt_royale_fbos[pass]);
		gl.Viewport(0, 0, crt_royale_pass_w[pass], crt_royale_pass_h[pass]);
		gl.DrawElements(GL_TRIANGLES, kIndexCount, GL_UNSIGNED_SHORT, 0);

		if (pass == 7 && crt_royale_use_mipmaps) {
			gl.BindTexture(GL_TEXTURE_2D, crt_royale_textures[7]);
			gl.GenerateMipmap(GL_TEXTURE_2D);
		}
	}

	SetOrtho(proj, 1.0f, 1.0f, currently_rotated ? 90.0f : 0.0f);
	gl.UseProgram(crt_royale_programs[CRT_ROYALE_PASS_COUNT - 1]);
	CrtRoyale_SetPassUniforms(CRT_ROYALE_PASS_COUNT - 1,
	                          crt_royale_pass_w[CRT_ROYALE_PASS_COUNT - 2],
	                          crt_royale_pass_h[CRT_ROYALE_PASS_COUNT - 2],
	                          crt_royale_pass_w[CRT_ROYALE_PASS_COUNT - 2],
	                          crt_royale_pass_h[CRT_ROYALE_PASS_COUNT - 2],
	                          crt_royale_pass_w[CRT_ROYALE_PASS_COUNT - 1],
	                          crt_royale_pass_h[CRT_ROYALE_PASS_COUNT - 1], proj);
	CrtRoyale_BindPassTextures(CRT_ROYALE_PASS_COUNT - 1, crt_royale_textures[CRT_ROYALE_PASS_COUNT - 2]);
	CrtRoyale_BindMaskTextures();

	gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
	gl.Viewport(vp_x, vp_y, vp_w, vp_h);
	gl.DrawElements(GL_TRIANGLES, kIndexCount, GL_UNSIGNED_SHORT, 0);

	SDL_GL_SwapWindow(SDL_VIDEO_wnd);
	gl.Viewport(0, 0, screen_width, screen_height);
	gl.BindVertexArray(0);
	crt_royale_frame_count++;
	return TRUE;
}

#endif /* SDL2 */


int SDL_VIDEO_GL_SetVideoMode(VIDEOMODE_resolution_t const *res, int windowed, VIDEOMODE_MODE_t mode, int rotate90)
{
	int isnew = SDL_VIDEO_screen == NULL; /* TRUE means the SDL/GL screen was not yet initialised */
	int context_updated = FALSE; /* TRUE means the OpenGL context has been recreated */
	currently_rotated = rotate90;

	/* Call SetVideoMode only when there was change in width, height, or windowed/fullscreen. */
	if (isnew || SDL_VIDEO_screen->w != res->width || SDL_VIDEO_screen->h != res->height ||
	    ((SDL_VIDEO_screen->flags & SDL_OpenGL_FULLSCREEN) == SDL_OpenGL_FULLSCREEN) == windowed) {
		if (!isnew) {
			CleanGlContext();
		}
#if HAVE_WINDOWS_H && !SDL2
		if (isnew && !windowed) {
			/* Switching from fullscreen software mode directly to fullscreen OpenGL mode causes
			   glitches on Windows (eg. when switched to windowed mode, the window would spontaneously
			   go back to fullscreen each time it loses and regains focus). We avoid the issue by
			   switching to a windowed non-OpenGL mode inbetween. */
			SDL_SetVideoMode(320, 200, SDL_VIDEO_native_bpp, SDL_RESIZABLE);
		}
#endif /* HAVE_WINDOWS_H */
		if (SetVideoMode(res->width, res->height, windowed))
			/* Reinitialisation happened! Need to recreate GL context. */
			isnew = TRUE;
		if (!InitGlFunctions()) {
			Log_print("Cannot use OpenGL - some functions are not provided.");
			return FALSE;
		}
		if (isnew) {
			GLint tex_size;
			gl.GetIntegerv(GL_MAX_TEXTURE_SIZE, & tex_size);
			if (tex_size < 1024) {
				Log_print("Cannot use OpenGL - Supported texture size is too small (%d).", tex_size);
				return FALSE;
			}
#if SDL2
			// add shaders
#ifdef DEBUG_SHADERS
			GLchar* vertexShader = read_text_file("atari800-shader.vert");
			if (!vertexShader) {
				Log_print("Missing vertex shader file 'atari800-shader.vert'");
				exit(1);
			}
			GLchar* fragmentShader = read_text_file("atari800-shader.frag");
			if (!fragmentShader) {
				Log_print("Missing fragment shader file 'atari800-shader.frag'");
				exit(1);
			}
#else
#include "sdl/gen-atari800-shader.vert.h"
			GLchar* vertexShader = Util_malloc(vertexShaderArr_len + 1);
			strncpy(vertexShader, (char*)vertexShaderArr, vertexShaderArr_len);
			vertexShader[vertexShaderArr_len] = 0;
#include "sdl/gen-atari800-shader.frag.h"
			GLchar* fragmentShader = Util_malloc(fragmentShaderArr_len + 1);
			strncpy(fragmentShader, (char*)fragmentShaderArr, fragmentShaderArr_len);
			fragmentShader[fragmentShaderArr_len] = 0;
#endif
			GLint success = 0;

			int vertex = gl.CreateShader(GL_VERTEX_SHADER);
			gl.ShaderSource(vertex, 1, &vertexShader, NULL);
			gl.CompileShader(vertex);
			gl.GetShaderiv(vertex, GL_COMPILE_STATUS, &success);
			if (!success) {
				char buf[500];
				gl.GetShaderInfoLog(vertex, 500, NULL, buf);
				Log_print("Cannot use OpenGL - error compiling vertex shader: %s", buf);
				exit(1);
			}
			int fragment = gl.CreateShader(GL_FRAGMENT_SHADER);
			gl.ShaderSource(fragment, 1, &fragmentShader, NULL);
			gl.CompileShader(fragment);
			gl.GetShaderiv(fragment, GL_COMPILE_STATUS, &success);
			if (!success) {
				char buf[500];
				gl.GetShaderInfoLog(fragment, 500, NULL, buf);
				Log_print("Cannot use OpenGL - error compiling fragment shader: %s", buf);
				exit(1);
			}

			progID = gl.CreateProgram();
			gl.AttachShader(progID, vertex);
			gl.AttachShader(progID, fragment);
			gl.LinkProgram(progID);
			gl.GetProgramiv(progID, GL_LINK_STATUS, &success);
			if (!success) {
				char buf[500];
				gl.GetProgramInfoLog(progID, 500, NULL, buf);
				Log_print("Cannot use OpenGL - error linking shader program: %s", buf);
				exit(1);
			}

			gl.DeleteShader(vertex);
			gl.DeleteShader(fragment);
			free(vertexShader);
			free(fragmentShader);

			SDL_set_up_opengl();
		}
		else {
			SDL_set_up_uvs();
#endif
		}
		pbo_available = InitGlPbo();
		if (!pbo_available)
			SDL_VIDEO_GL_pbo = FALSE;
		if (isnew) {
			Log_print("OpenGL initialized successfully. Version: %s", gl.GetString(GL_VERSION));
#if !SDL2
			if (pbo_available)
				Log_print("OpenGL Pixel Buffer Objects available.");
			else
				Log_print("OpenGL Pixel Buffer Objects not available.");
#endif
		}
		InitGlContext();
		context_updated = TRUE;
	}

	if (isnew) {
		FreeTexture();
		AllocTexture();
	}

	UpdatePaletteLookup(mode);

	if (context_updated)
		InitGlTextures();

	SDL_ShowCursor(SDL_DISABLE);	/* hide mouse cursor */

	if (mode == VIDEOMODE_MODE_NORMAL) {
#ifdef PAL_BLENDING
		if (ARTIFACT_mode == ARTIFACT_PAL_BLEND)
			blit_funcs[0] = &DisplayPalBlending;
		else
#endif /* PAL_BLENDING */
		if (ARTIFACT_mode == ARTIFACT_PAL_ALTIRRA_HI)
			blit_funcs[0] = &DisplayAltirraPalHi;
		else
			blit_funcs[0] = &DisplayNormal;
	}

#if SDL2
	gl.Viewport(0, 0, screen_width, screen_height);
#else
	gl.Viewport(VIDEOMODE_dest_offset_left, VIDEOMODE_dest_offset_top, VIDEOMODE_dest_width, VIDEOMODE_dest_height);
	SetSubpixelShifts();
	SetGlDisplayList();
#endif
	CleanDisplayTexture();
	return TRUE;
}

int SDL_VIDEO_GL_SupportsVideomode(VIDEOMODE_MODE_t mode, int stretch, int rotate90)
{
	/* OpenGL supports rotation and stretching in all display modes. */
	return TRUE;
}

static void BuildChannelMap(Uint32 mask, Uint32 *map)
{
	int shift = 0;
	unsigned int bits = 0;
	Uint32 work = mask;
	Uint32 max;
	int i;

	if (work == 0) {
		memset(map, 0, sizeof(Uint32) * 256);
		return;
	}

	while ((work & 1) == 0) {
		shift++;
		work >>= 1;
	}
	while (work & 1) {
		bits++;
		work >>= 1;
	}
	max = (bits == 0) ? 0 : ((1u << bits) - 1u);
	for (i = 0; i < 256; i++)
		map[i] = (bits == 0) ? 0 : ((Uint32)((i * max + 127) / 255) << shift);
}

static void AltirraPalHi_UpdatePixelMaps(void)
{
	int format = SDL_VIDEO_GL_pixel_format;

	if (format == pal_hi_pixel_format)
		return;

	pal_hi_pixel_format = format;
	BuildChannelMap(pixel_formats[format].rmask, pal_hi_map_r);
	BuildChannelMap(pixel_formats[format].gmask, pal_hi_map_g);
	BuildChannelMap(pixel_formats[format].bmask, pal_hi_map_b);
}

static int AltirraPalHi_Init(void)
{
	if (pal_hi_engine == NULL) {
		ATC_ColorParams color_params;
		ATC_ArtifactingParams artifact_params;
		pal_hi_engine = atc_artifacting_create();
		if (pal_hi_engine == NULL)
			return FALSE;
		atc_color_params_default_pal(&color_params);
		// Native Altirra defaults would be the following:
		// color_params.mArtifactSat = 0.80f;
		// color_params.mArtifactSharpness = 0.50f;
		// color_params.mSaturation = 0.29f;
		// color_params.mContrast = 1.0f;
		// color_params.mArtifactHue = 80.f;
		/* Soften PAL hi artifacting to better match Altirra's default look. */
		color_params.mArtifactSat = 0.80f;
		color_params.mArtifactSharpness = 0.50f;
		color_params.mSaturation = 0.29f;
		color_params.mContrast = 1.0f;
		color_params.mArtifactHue = 260.f;
		color_params.mHueStart = -12.0f;
		color_params.mHueRange = 18.3f * 15.0f;
		atc_artifacting_params_default(&artifact_params);
		atc_artifacting_set_color_params(pal_hi_engine, &color_params, NULL, NULL, ATC_MONITOR_COLOR, 0);
		atc_artifacting_set_artifacting_params(pal_hi_engine, &artifact_params);
	}

	if (pal_hi_input == NULL) {
		pal_hi_input = (Uint8 *) Util_malloc(ATC_ARTIFACTING_N * ATC_ARTIFACTING_M);
		if (pal_hi_input == NULL)
			return FALSE;
	}

	if (pal_hi_output == NULL) {
		pal_hi_output = (Uint32 *) Util_malloc(sizeof(Uint32) * ATC_ARTIFACTING_N * 2 * ATC_ARTIFACTING_M);
		if (pal_hi_output == NULL)
			return FALSE;
	}

	return TRUE;
}

static void AltirraPalHi_BlitScaled16(Uint16 *dest, int dest_pitch, const Uint32 *src, int src_pitch,
                                      int src_w, int src_h, int out_w, int out_h)
{
	int y = 0;
	if (src_w <= 0 || src_h <= 0 || out_w <= 0 || out_h <= 0)
		return;
	int w = src_w << 16;
	int h = src_h << 16;
	int dx = w / out_w;
	int dy = h / out_h;
	int i;

	for (i = 0; i < out_h; i++) {
		int x = 0;
		const Uint32 *src_line = src + (y >> 16) * src_pitch;
		int pos;
		for (pos = 0; pos < out_w; pos++) {
			Uint32 c = src_line[x >> 16];
			Uint8 b = (Uint8)(c & 0xff);
			Uint8 g = (Uint8)((c >> 8) & 0xff);
			Uint8 r = (Uint8)((c >> 16) & 0xff);
			dest[pos] = (Uint16)(pal_hi_map_r[r] | pal_hi_map_g[g] | pal_hi_map_b[b]);
			x += dx;
		}
		dest += dest_pitch;
		y += dy;
	}
}

static void AltirraPalHi_BlitScaled32(Uint32 *dest, int dest_pitch, const Uint32 *src, int src_pitch,
                                      int src_w, int src_h, int out_w, int out_h)
{
	int y = 0;
	if (src_w <= 0 || src_h <= 0 || out_w <= 0 || out_h <= 0)
		return;
	int w = src_w << 16;
	int h = src_h << 16;
	int dx = w / out_w;
	int dy = h / out_h;
	int i;

	for (i = 0; i < out_h; i++) {
		int x = 0;
		const Uint32 *src_line = src + (y >> 16) * src_pitch;
		int pos;
		for (pos = 0; pos < out_w; pos++) {
			Uint32 c = src_line[x >> 16];
			Uint8 b = (Uint8)(c & 0xff);
			Uint8 g = (Uint8)((c >> 8) & 0xff);
			Uint8 r = (Uint8)((c >> 16) & 0xff);
			dest[pos] = pal_hi_map_r[r] | pal_hi_map_g[g] | pal_hi_map_b[b];
			x += dx;
		}
		dest += dest_pitch;
		y += dy;
	}
}

static void DisplayAltirraPalHi(GLvoid *dest)
{
	int copy_w;
	int copy_h;
	int pad_x;
	int pad_y;
	int output_stride;
	int region_w;
	int region_h;
	Uint8 *src;
	Uint8 *dst;
	int y;

	if (!AltirraPalHi_Init()) {
		DisplayNormal(dest);
		return;
	}

	AltirraPalHi_UpdatePixelMaps();

	copy_w = (int)VIDEOMODE_src_width;
	copy_h = (int)VIDEOMODE_src_height;
	if (copy_w > ATC_ARTIFACTING_N)
		copy_w = ATC_ARTIFACTING_N;
	if (copy_h > ATC_ARTIFACTING_M)
		copy_h = ATC_ARTIFACTING_M;

	pad_x = (ATC_ARTIFACTING_N - copy_w) / 2;
	pad_y = (ATC_ARTIFACTING_M - copy_h) / 2;
	memset(pal_hi_input, GTIA_COLBK, ATC_ARTIFACTING_N * ATC_ARTIFACTING_M);
	memset(pal_hi_scanline_hires, 0, sizeof(pal_hi_scanline_hires));
	src = (Uint8 *)Screen_atari + Screen_WIDTH * VIDEOMODE_src_offset_top + VIDEOMODE_src_offset_left;
	dst = pal_hi_input + pad_y * ATC_ARTIFACTING_N + pad_x;
	for (y = 0; y < copy_h; y++) {
		int line = VIDEOMODE_src_offset_top + y;
		int hires = 0;
		if (line >= 0 && line < Screen_HEIGHT)
			hires = ANTIC_scanline_hires[line] != 0;
		memcpy(dst, src, copy_w);
		pal_hi_scanline_hires[pad_y + y] = (Uint8)hires;
		src += Screen_WIDTH;
		dst += ATC_ARTIFACTING_N;
	}

	atc_artifacting_begin_frame(pal_hi_engine, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0);
	output_stride = ATC_ARTIFACTING_N * 2;
	for (y = 0; y < ATC_ARTIFACTING_M; y++) {
		int scanline_hires = pal_hi_scanline_hires[y] != 0;
		atc_artifacting_artifact8(pal_hi_engine, (uint32_t)y,
		                          pal_hi_output + y * output_stride,
		                          pal_hi_input + y * ATC_ARTIFACTING_N,
		                          scanline_hires, 0, 1);
	}

	region_w = copy_w * 2;
	region_h = copy_h;
	if (region_w <= 0 || region_h <= 0)
		return;
	if (VIDEOMODE_actual_width == 0 || VIDEOMODE_src_height == 0)
		return;

	if (bpp_32) {
		AltirraPalHi_BlitScaled32((Uint32 *)dest, VIDEOMODE_actual_width,
		                          pal_hi_output + pad_y * output_stride + pad_x * 2,
		                          output_stride, region_w, region_h,
		                          VIDEOMODE_actual_width, VIDEOMODE_src_height);
	}
	else {
		AltirraPalHi_BlitScaled16((Uint16 *)dest, VIDEOMODE_actual_width,
		                          pal_hi_output + pad_y * output_stride + pad_x * 2,
		                          output_stride, region_w, region_h,
		                          VIDEOMODE_actual_width, VIDEOMODE_src_height);
	}
}

static void DisplayNormal(GLvoid *dest)
{
	Uint8 *screen = (Uint8 *)Screen_atari + Screen_WIDTH * VIDEOMODE_src_offset_top + VIDEOMODE_src_offset_left;
	if (bpp_32)
		SDL_VIDEO_BlitNormal32((Uint32*)dest, screen, VIDEOMODE_actual_width, VIDEOMODE_src_width, VIDEOMODE_src_height, SDL_PALETTE_buffer.bpp32);
	else {
		int pitch;
		if (VIDEOMODE_actual_width & 0x01)
			pitch = VIDEOMODE_actual_width / 2 + 1;
		else
			pitch = VIDEOMODE_actual_width / 2;
		SDL_VIDEO_BlitNormal16((Uint32*)dest, screen, pitch, VIDEOMODE_src_width, VIDEOMODE_src_height, SDL_PALETTE_buffer.bpp16);
	}
}

#ifdef PAL_BLENDING
static void DisplayPalBlending(GLvoid *dest)
{
	Uint8 *screen = (Uint8 *)Screen_atari + Screen_WIDTH * VIDEOMODE_src_offset_top + VIDEOMODE_src_offset_left;
	if (bpp_32)
		PAL_BLENDING_Blit32((ULONG*)dest, screen, VIDEOMODE_actual_width, VIDEOMODE_src_width, VIDEOMODE_src_height, VIDEOMODE_src_offset_top % 2);
	else {
		int pitch;
		if (VIDEOMODE_actual_width & 0x01)
			pitch = VIDEOMODE_actual_width / 2 + 1;
		else
			pitch = VIDEOMODE_actual_width / 2;
		PAL_BLENDING_Blit16((ULONG*)dest, screen, pitch, VIDEOMODE_src_width, VIDEOMODE_src_height, VIDEOMODE_src_offset_top % 2);
	}
}
#endif /* PAL_BLENDING */

#if NTSC_FILTER
static void DisplayNTSCEmu(GLvoid *dest)
{
	(*pixel_formats[SDL_VIDEO_GL_pixel_format].ntsc_blit_func)(
		FILTER_NTSC_emu,
		(ATARI_NTSC_IN_T *) ((UBYTE *)Screen_atari + Screen_WIDTH * VIDEOMODE_src_offset_top + VIDEOMODE_src_offset_left),
		Screen_WIDTH,
		VIDEOMODE_src_width,
		VIDEOMODE_src_height,
		dest,
		VIDEOMODE_actual_width * (bpp_32 ? 4 : 2));
}
#endif

#ifdef XEP80_EMULATION
static void DisplayXEP80(GLvoid *dest)
{
	static int xep80Frame = 0;
	Uint8 *screen;
	xep80Frame++;
	if (xep80Frame == 60) xep80Frame = 0;
	if (xep80Frame > 29)
		screen = XEP80_screen_1;
	else
		screen = XEP80_screen_2;

	screen += XEP80_SCRN_WIDTH * VIDEOMODE_src_offset_top + VIDEOMODE_src_offset_left;
	if (bpp_32)
		SDL_VIDEO_BlitXEP80_32((Uint32*)dest, screen, VIDEOMODE_actual_width, VIDEOMODE_src_width, VIDEOMODE_src_height, SDL_PALETTE_buffer.bpp32);
	else
		SDL_VIDEO_BlitXEP80_16((Uint32*)dest, screen, VIDEOMODE_actual_width / 2, VIDEOMODE_src_width, VIDEOMODE_src_height, SDL_PALETTE_buffer.bpp16);
}
#endif

#ifdef PBI_PROTO80
static void DisplayProto80(GLvoid *dest)
{
	int first_column = (VIDEOMODE_src_offset_left+7) / 8;
	int last_column = (VIDEOMODE_src_offset_left + VIDEOMODE_src_width) / 8;
	int first_line = VIDEOMODE_src_offset_top;
	int last_line = first_line + VIDEOMODE_src_height;
	if (bpp_32)
		SDL_VIDEO_BlitProto80_32((Uint32*)dest, first_column, last_column, VIDEOMODE_actual_width, first_line, last_line, SDL_PALETTE_buffer.bpp32);
	else
		SDL_VIDEO_BlitProto80_16((Uint32*)dest, first_column, last_column, VIDEOMODE_actual_width/2, first_line, last_line, SDL_PALETTE_buffer.bpp16);
}
#endif

#ifdef AF80
static void DisplayAF80(GLvoid *dest)
{
	int first_column = (VIDEOMODE_src_offset_left+7) / 8;
	int last_column = (VIDEOMODE_src_offset_left + VIDEOMODE_src_width) / 8;
	int first_line = VIDEOMODE_src_offset_top;
	int last_line = first_line + VIDEOMODE_src_height;
	static int AF80Frame = 0;
	int blink;
	AF80Frame++;
	if (AF80Frame == 60) AF80Frame = 0;
	blink = AF80Frame >= 30;
	if (bpp_32)
		SDL_VIDEO_BlitAF80_32((Uint32*)dest, first_column, last_column, VIDEOMODE_actual_width, first_line, last_line, blink, SDL_PALETTE_buffer.bpp32);
	else
		SDL_VIDEO_BlitAF80_16((Uint32*)dest, first_column, last_column, VIDEOMODE_actual_width/2, first_line, last_line, blink, SDL_PALETTE_buffer.bpp16);
}
#endif

#ifdef BIT3
static void DisplayBIT3(GLvoid *dest)
{
	int first_column = (VIDEOMODE_src_offset_left+7) / 8;
	int last_column = (VIDEOMODE_src_offset_left + VIDEOMODE_src_width) / 8;
	int first_line = VIDEOMODE_src_offset_top;
	int last_line = first_line + VIDEOMODE_src_height;
	static int BIT3Frame = 0;
	int blink;
	BIT3Frame++;
	if (BIT3Frame == 60) BIT3Frame = 0;
	blink = BIT3Frame >= 30;
	if (bpp_32)
		SDL_VIDEO_BlitBIT3_32((Uint32*)dest, first_column, last_column, VIDEOMODE_actual_width, first_line, last_line, blink, SDL_PALETTE_buffer.bpp32);
	else
		SDL_VIDEO_BlitBIT3_16((Uint32*)dest, first_column, last_column, VIDEOMODE_actual_width/2, first_line, last_line, blink, SDL_PALETTE_buffer.bpp16);
}
#endif

#if SDL2
static void SDL_VIDEO_GL_DisplayScreenStandard(void)
{
	gl.Clear(GL_COLOR_BUFFER_BIT);
	gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	gl.Enable(GL_BLEND);
	gl.UseProgram(progID);
	gl.Uniform1i(our_texture, 0);
	gl.ActiveTexture(GL_TEXTURE0);
	gl.BindTexture(GL_TEXTURE_2D, textures[0]);
	(*blit_funcs[SDL_VIDEO_current_display_mode])(screen_texture);
	gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, VIDEOMODE_actual_width, VIDEOMODE_src_height,
		pixel_formats[SDL_VIDEO_GL_pixel_format].format, pixel_formats[SDL_VIDEO_GL_pixel_format].type,
		screen_texture);

	gl.BindVertexArray(vaos[0]);
	float sx = 1.0f, sy = 1.0f;
	float vw = VIDEOMODE_custom_horizontal_area;
	float vh = VIDEOMODE_custom_vertical_area;
	// Screen aspect ratio adjustment
	float a = (float)screen_width / screen_height;
	float a0 = currently_rotated ? vh / vw : vw / vh;
	if (a > a0) {
		sx = a0 / a;
	}
	else {
		sy = a / a0;
	}

	int keep = VIDEOMODE_GetKeepAspect();
	if (keep == VIDEOMODE_KEEP_ASPECT_REAL) {
		float ar = VIDEOMODE_GetPixelAspectRatio(VIDEOMODE_MODE_NORMAL);
		if (ar > 1.0) {
			sy /= ar;
		}
		else {
			sx *= ar;
		}
	}

	float proj[4][4];
	SetOrtho(proj, sx * zoom_factor, sy * zoom_factor, currently_rotated ? 90 : 0);

	gl.UniformMatrix4fv(sh_vp_matrix, 1, GL_FALSE, &proj[0][0]);
	gl.Uniform2f(sh_resolution, vw, vh);

	float scanlinesFactor = SDL_VIDEO_interpolate_scanlines ? SDL_VIDEO_scanlines_percentage / 100.0f : 0.0f;
	gl.Uniform1f(sh_scanlines, scanlinesFactor);
	gl.Uniform1f(sh_curvature, SDL_VIDEO_crt_barrel_distortion / 20.0f);
	gl.Uniform1f(sh_pixelSpread, SDL_VIDEO_crt_beam_shape ? pow((27.0f - SDL_VIDEO_crt_beam_shape) / 20.0f, 2) : 0.0f);
	gl.Uniform1f(sh_glow, SDL_VIDEO_crt_phosphor_glow / 20.0f);

	gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[2]);
	gl.DrawElements(GL_TRIANGLES, kIndexCount, GL_UNSIGNED_SHORT, 0);
	SDL_GL_SwapWindow(SDL_VIDEO_wnd);

	gl.BindBuffer(GL_ARRAY_BUFFER, 0);
	gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}
#endif


void SDL_VIDEO_GL_DisplayScreen(void)
{
#if SDL2
	if (!screen_width || !screen_height) return;

	if (SDL_VIDEO_crt_emulation == SDL_VIDEO_CRT_EMULATION_ROYALE) {
		if (CrtRoyale_Display())
			return;
	}
	SDL_VIDEO_GL_DisplayScreenStandard();

#else
	gl.BindTexture(GL_TEXTURE_2D, textures[0]);
	if (SDL_VIDEO_GL_pbo) {
		GLvoid *ptr;
		gl.BindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, screen_pbo);
		ptr = gl.MapBuffer(GL_PIXEL_UNPACK_BUFFER_ARB, GL_WRITE_ONLY_ARB);
		(*blit_funcs[SDL_VIDEO_current_display_mode])(ptr);
		gl.UnmapBuffer(GL_PIXEL_UNPACK_BUFFER_ARB);
		gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, VIDEOMODE_actual_width, VIDEOMODE_src_height,
		                 pixel_formats[SDL_VIDEO_GL_pixel_format].format, pixel_formats[SDL_VIDEO_GL_pixel_format].type,
		                 NULL);
		gl.BindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
	} else {
		(*blit_funcs[SDL_VIDEO_current_display_mode])(screen_texture);
		gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, VIDEOMODE_actual_width, VIDEOMODE_src_height,
		                 pixel_formats[SDL_VIDEO_GL_pixel_format].format, pixel_formats[SDL_VIDEO_GL_pixel_format].type,
		                 screen_texture);
	}
	gl.CallList(screen_dlist);
	SDL_GL_SwapBuffers();
#endif /* SDL2 */
}

int SDL_VIDEO_GL_ReadConfig(char *option, char *parameters)
{
	if (strcmp(option, "PIXEL_FORMAT") == 0) {
		int i = CFG_MatchTextParameter(parameters, pixel_format_cfg_strings, SDL_VIDEO_GL_PIXEL_FORMAT_SIZE);
		if (i < 0)
			return FALSE;
		SDL_VIDEO_GL_pixel_format = i;
	}
	else if (strcmp(option, "BILINEAR_FILTERING") == 0)
		return (SDL_VIDEO_GL_filtering = Util_sscanbool(parameters)) != -1;
	else if (strcmp(option, "OPENGL_PBO") == 0)
		return (SDL_VIDEO_GL_pbo = Util_sscanbool(parameters)) != -1;
	else
		return FALSE;
	return TRUE;
}

void SDL_VIDEO_GL_WriteConfig(FILE *fp)
{
	fprintf(fp, "PIXEL_FORMAT=%s\n", pixel_format_cfg_strings[SDL_VIDEO_GL_pixel_format]);
	fprintf(fp, "BILINEAR_FILTERING=%d\n", SDL_VIDEO_GL_filtering);
	fprintf(fp, "OPENGL_PBO=%d\n", SDL_VIDEO_GL_pbo);
}

/* Loads the OpenGL library. Return TRUE on success, FALSE on failure. */
static int InitGlLibrary(void)
{
	if (SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) != 0) {
		Log_print("Cannot use OpenGL - unable to set GL attribute: %s\n",SDL_GetError());
		return FALSE;
	}
	if (SDL_GL_LoadLibrary(library_path) < 0) {
		Log_print("Cannot use OpenGL - unable to dynamically open OpenGL library: %s\n",SDL_GetError());
		return FALSE;
	}
	return TRUE;
}

void SDL_VIDEO_GL_InitSDL(void)
{
	SDL_VIDEO_opengl_available = InitGlLibrary();
#if SDL2
	// for OpenGL 4.1
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif
}

int SDL_VIDEO_GL_Initialise(int *argc, char *argv[])
{
	int i, j;
	int help_only = FALSE;

	for (i = j = 1; i < *argc; i++) {
		int i_a = (i + 1 < *argc); /* is argument available? */
		int a_m = FALSE;           /* error, argument missing! */
		int a_i = FALSE;           /* error, argument invalid! */

		if (strcmp(argv[i], "-pixel-format") == 0) {
			if (i_a) {
				if ((SDL_VIDEO_GL_pixel_format = CFG_MatchTextParameter(argv[++i], pixel_format_cfg_strings, SDL_VIDEO_GL_PIXEL_FORMAT_SIZE)) < 0)
					a_i = TRUE;
			}
			else a_m = TRUE;
		}
		else if (strcmp(argv[i], "-bilinear-filter") == 0)
			SDL_VIDEO_GL_filtering = TRUE;
		else if (strcmp(argv[i], "-no-bilinear-filter") == 0)
			SDL_VIDEO_GL_filtering = FALSE;
		else if (strcmp(argv[i], "-pbo") == 0)
			SDL_VIDEO_GL_pbo = TRUE;
		else if (strcmp(argv[i], "-no-pbo") == 0)
			SDL_VIDEO_GL_pbo = FALSE;
		else if (strcmp(argv[i], "-opengl-lib") == 0) {
			if (i_a)
				library_path = argv[++i];
			else a_m = TRUE;
		}
		else {
			if (strcmp(argv[i], "-help") == 0) {
				help_only = TRUE;
				Log_print("\t-pixel-format bgr16|rgb16|bgra32|argb32");
				Log_print("\t                     Set internal pixel format (affects performance)");
				Log_print("\t-bilinear-filter     Enable OpenGL bilinear filtering");
				Log_print("\t-no-bilinear-filter  Disable OpenGL bilinear filtering");
				Log_print("\t-pbo                 Use OpenGL Pixel Buffer Objects if available");
				Log_print("\t-no-pbo              Don't use OpenGL Pixel Buffer Objects");
				Log_print("\t-opengl-lib <path>   Use a custom OpenGL shared library");
			}
			argv[j++] = argv[i];
		}
		if (a_m) {
			Log_print("Missing argument for '%s'", argv[i]);
			return FALSE;
		} else if (a_i) {
			Log_print("Invalid argument for '%s'", argv[--i]);
			return FALSE;
		}
	}
	*argc = j;

	if (help_only)
		return TRUE;

	bpp_32 = SDL_VIDEO_GL_pixel_format >= SDL_VIDEO_GL_PIXEL_FORMAT_BGRA32;

	return TRUE;
}

void SDL_VIDEO_GL_SetPixelFormat(int value)
{
	SDL_VIDEO_GL_pixel_format = value;
	if (SDL_VIDEO_screen != NULL && (SDL_VIDEO_screen->flags & SDL_OpenGL_FLAG) == SDL_OpenGL_FLAG) {
		int new_bpp_32 = value >= SDL_VIDEO_GL_PIXEL_FORMAT_BGRA32;
		if (new_bpp_32 != bpp_32)
		{
			FreeTexture();
			bpp_32 = new_bpp_32;
			AllocTexture();
		}
		UpdatePaletteLookup(SDL_VIDEO_current_display_mode);
		InitGlTextures();
		CleanDisplayTexture();
	}
	else
		bpp_32 = value;

}

void SDL_VIDEO_GL_TogglePixelFormat(void)
{
	SDL_VIDEO_GL_SetPixelFormat((SDL_VIDEO_GL_pixel_format + 1) % SDL_VIDEO_GL_PIXEL_FORMAT_SIZE);
}

void SDL_VIDEO_GL_SetFiltering(int value)
{
	SDL_VIDEO_GL_filtering = value;
	if (SDL_VIDEO_screen != NULL && (SDL_VIDEO_screen->flags & SDL_OpenGL_FLAG) == SDL_OpenGL_FLAG) {
		GLint filtering = value ? GL_LINEAR : GL_NEAREST;
		gl.BindTexture(GL_TEXTURE_2D, textures[0]);
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filtering);
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filtering);
		SetSubpixelShifts();
		SetGlDisplayList();
	}
}

void SDL_VIDEO_GL_ToggleFiltering(void)
{
	SDL_VIDEO_GL_SetFiltering(!SDL_VIDEO_GL_filtering);
}

int SDL_VIDEO_GL_SetPbo(int value)
{
	if (SDL_VIDEO_screen != NULL && (SDL_VIDEO_screen->flags & SDL_OpenGL_FLAG) == SDL_OpenGL_FLAG) {
		/* Return false if PBOs are requested but not available. */
		if (value && !pbo_available)
			return FALSE;
		CleanGlContext();
		FreeTexture();
		SDL_VIDEO_GL_pbo = value;
		InitGlContext();
		AllocTexture();
		InitGlTextures();
		SetGlDisplayList();
		CleanDisplayTexture();
	}
	else
		SDL_VIDEO_GL_pbo = value;
	return TRUE;
}

int SDL_VIDEO_GL_TogglePbo(void)
{
	return SDL_VIDEO_GL_SetPbo(!SDL_VIDEO_GL_pbo);
}

void SDL_VIDEO_GL_ScanlinesPercentageChanged(void)
{
#if SDL2
	SDL_VIDEO_GL_DisplayScreen();
 #else
	if (SDL_VIDEO_screen != NULL && (SDL_VIDEO_screen->flags & SDL_OpenGL_FLAG) == SDL_OpenGL_FLAG) {
		SetSubpixelShifts();
		SetGlDisplayList();
	}
#endif
}

void SDL_VIDEO_GL_InterpolateScanlinesChanged(void)
{
#if SDL2
	SDL_VIDEO_GL_DisplayScreen();
 #else
	if (SDL_VIDEO_screen != NULL && (SDL_VIDEO_screen->flags & SDL_OpenGL_FLAG) == SDL_OpenGL_FLAG) {
		GLint filtering = SDL_VIDEO_interpolate_scanlines ? GL_LINEAR : GL_NEAREST;
		gl.BindTexture(GL_TEXTURE_2D, textures[1]);
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filtering);
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filtering);
		SetSubpixelShifts();
		SetGlDisplayList();
	}
#endif
}
