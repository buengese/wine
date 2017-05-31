/*
 * Android driver definitions
 *
 * Copyright 2013 Alexandre Julliard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __WINE_ANDROID_H
#define __WINE_ANDROID_H

#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <jni.h>

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "wine/gdi_driver.h"


/**************************************************************************
 * USER driver
 */

extern unsigned int screen_width DECLSPEC_HIDDEN;
extern unsigned int screen_height DECLSPEC_HIDDEN;
extern RECT virtual_screen_rect DECLSPEC_HIDDEN;
extern MONITORINFOEXW default_monitor DECLSPEC_HIDDEN;

extern void init_monitors( int width, int height ) DECLSPEC_HIDDEN;

extern JavaVM *wine_get_java_vm(void);
extern jobject wine_get_java_object(void);

#endif  /* __WINE_ANDROID_H */
