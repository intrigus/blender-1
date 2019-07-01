/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2013 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup GHOST
 *
 * Definition of GHOST_Context class.
 */

#include "GHOST_Context.h"

#ifdef _WIN32
#  include <GL/wglew.h>  // only for symbolic constants, do not use API functions
#  include <tchar.h>
#
#  ifndef ERROR_PROFILE_DOES_NOT_MATCH_DEVICE
#    define ERROR_PROFILE_DOES_NOT_MATCH_DEVICE 0x7E7
#  endif
#endif

#include <cstdio>
#include <cstring>

#ifdef _WIN32

bool win32_silent_chk(bool result)
{
  if (!result) {
    SetLastError(NO_ERROR);
  }

  return result;
}

bool win32_chk(bool result, const char *file, int line, const char *text)
{
  if (!result) {
    LPTSTR formattedMsg = NULL;

    DWORD error = GetLastError();

    const char *msg;

    DWORD count = 0;

    /* Some drivers returns a HRESULT instead of a standard error message.
     * i.e: 0xC0072095 instead of 0x2095 for ERROR_INVALID_VERSION_ARB
     * So strip down the error to the valid error code range. */
    switch (error & 0x0000FFFF) {
      case ERROR_INVALID_VERSION_ARB:
        msg =
            "The specified OpenGL version and feature set are either invalid or not supported.\n";
        break;

      case ERROR_INVALID_PROFILE_ARB:
        msg =
            "The specified OpenGL profile and feature set are either invalid or not supported.\n";
        break;

      case ERROR_INVALID_PIXEL_TYPE_ARB:
        msg = "The specified pixel type is invalid.\n";
        break;

      case ERROR_INCOMPATIBLE_DEVICE_CONTEXTS_ARB:
        msg =
            ("The device contexts specified are not compatible. "
             "This can occur if the device contexts are managed by "
             "different drivers or possibly on different graphics adapters.\n");
        break;

#  ifdef WITH_GLEW_ES
      case ERROR_INCOMPATIBLE_AFFINITY_MASKS_NV:
        msg = "The device context(s) and rendering context have non-matching affinity masks.\n";
        break;

      case ERROR_MISSING_AFFINITY_MASK_NV:
        msg = "The rendering context does not have an affinity mask set.\n";
        break;
#  endif

      case ERROR_PROFILE_DOES_NOT_MATCH_DEVICE:
        msg =
            ("The specified profile is intended for a device of a "
             "different type than the specified device.\n");
        break;

      default: {
        count = FormatMessage((FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                               FORMAT_MESSAGE_IGNORE_INSERTS),
                              NULL,
                              error,
                              MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                              (LPTSTR)(&formattedMsg),
                              0,
                              NULL);

        msg = count > 0 ? formattedMsg : "<no system message>\n";
        break;
      }
    }

#  ifndef NDEBUG
    _ftprintf(stderr,
              "%s(%d):[%s] -> Win32 Error# (%lu): %s",
              file,
              line,
              text,
              (unsigned long)error,
              msg);
#  else
    _ftprintf(stderr, "Win32 Error# (%lu): %s", (unsigned long)error, msg);
#  endif

    SetLastError(NO_ERROR);

    if (count != 0)
      LocalFree(formattedMsg);
  }

  return result;
}

#endif  // _WIN32

void GHOST_Context::initContextGLEW()
{
  GLEW_CHK(glewInit());
}

void GHOST_Context::initClearGL()
{
  glClearColor(0.447, 0.447, 0.447, 0.000);
  glClear(GL_COLOR_BUFFER_BIT);
  glClearColor(0.000, 0.000, 0.000, 0.000);
}

GHOST_TSuccess GHOST_Context::blitOpenGLOffscreenContext(GHOST_Context *offscreen,
                                                         GHOST_TInt32 width,
                                                         GHOST_TInt32 height)
{
  GLuint fbo_offscreen;
  GLuint fbo_onscreen;
  GLuint render_buf_shared;
  GLint fbo_prev_draw;

  if ((m_type != GHOST_kDrawingContextTypeOpenGL) ||
      (offscreen->m_type != GHOST_kDrawingContextTypeOpenGL)) {
    return GHOST_kFailure;
  }

  offscreen->setDefaultFramebufferSize(width, height);

  /* Logic here:
   * We can't simply blit from one context's framebuffer into the other. Unlike Framebuffers/FBOs,
   * Renderbuffers can be shared though. So create one, and blit the offscreen context framebuffer
   * contents into it. To share it, an FBO has to be created for each context though, as the
   * default framebuffer doesn't allow any attachments. */

  offscreen->activateDrawingContext();

  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo_prev_draw);

  /* Create shared renderbuffer */
  glGenRenderbuffers(1, &render_buf_shared);
  glBindRenderbuffer(GL_RENDERBUFFER, render_buf_shared);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);

  /* Create offscreen FBO and assign renderbuffer */
  glGenFramebuffers(1, &fbo_offscreen);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_offscreen);
  glFramebufferRenderbuffer(
      GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, render_buf_shared);

  /* Blit offscreen framebuffer into renderbuffer */
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_prev_draw);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_offscreen);
  glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);

  glDeleteFramebuffers(1, &fbo_offscreen); /* Can delete already. Do before context change. */

  activateDrawingContext();

  /* Create onscreen FBO and assign renderbuffer */
  glGenFramebuffers(1, &fbo_onscreen);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_onscreen);
  glFramebufferRenderbuffer(
      GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, render_buf_shared);

  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_onscreen);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, getDefaultFramebuffer());
  /* Finally, blit to onscreen buffer. */
  glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);

  glDeleteFramebuffers(1, &fbo_onscreen);
  glDeleteRenderbuffers(1, &render_buf_shared);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  return GHOST_kSuccess;
}
