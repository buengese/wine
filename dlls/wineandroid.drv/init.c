/*
 * Android driver initialisation functions
 *
 * Copyright 1996, 2013, 2017 Alexandre Julliard
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

#define NONAMELESSSTRUCT
#define NONAMELESSUNION
#include "config.h"
#include "wine/port.h"

#include <stdarg.h>
#include <string.h>

#include "windef.h"
#include "winbase.h"
#include "android.h"
#include "wine/server.h"
#include "wine/library.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(android);

unsigned int screen_width = 0;
unsigned int screen_height = 0;
RECT virtual_screen_rect = { 0, 0, 0, 0 };

MONITORINFOEXW default_monitor =
{
    sizeof(default_monitor),    /* cbSize */
    { 0, 0, 0, 0 },             /* rcMonitor */
    { 0, 0, 0, 0 },             /* rcWork */
    MONITORINFOF_PRIMARY,       /* dwFlags */
    { '\\','\\','.','\\','D','I','S','P','L','A','Y','1',0 }   /* szDevice */
};

static const unsigned int screen_bpp = 32;  /* we don't support other modes */

static int device_init_done;

typedef struct
{
    struct gdi_physdev dev;
} ANDROID_PDEVICE;

static const struct gdi_dc_funcs android_drv_funcs;


/******************************************************************************
 *           init_monitors
 */
void init_monitors( int width, int height )
{
    static const WCHAR trayW[] = {'S','h','e','l','l','_','T','r','a','y','W','n','d',0};
    RECT rect;
    HWND hwnd = FindWindowW( trayW, NULL );

    virtual_screen_rect.right = width;
    virtual_screen_rect.bottom = height;
    default_monitor.rcMonitor = default_monitor.rcWork = virtual_screen_rect;

    if (!hwnd || !IsWindowVisible( hwnd )) return;
    if (!GetWindowRect( hwnd, &rect )) return;
    if (rect.top) default_monitor.rcWork.bottom = rect.top;
    else default_monitor.rcWork.top = rect.bottom;
    TRACE( "found tray %p %s work area %s\n", hwnd,
           wine_dbgstr_rect( &rect ), wine_dbgstr_rect( &default_monitor.rcWork ));
}


/**********************************************************************
 *	     fetch_display_metrics
 */
static void fetch_display_metrics(void)
{
    if (wine_get_java_vm()) return;  /* for Java threads it will be set when the top view is created */

    SERVER_START_REQ( get_window_rectangles )
    {
        req->handle = wine_server_user_handle( GetDesktopWindow() );
        req->relative = COORDS_CLIENT;
        if (!wine_server_call( req ))
        {
            screen_width  = reply->window.right;
            screen_height = reply->window.bottom;
        }
    }
    SERVER_END_REQ;

    init_monitors( screen_width, screen_height );
    TRACE( "screen %ux%u\n", screen_width, screen_height );
}


/**********************************************************************
 *           device_init
 *
 * Perform initializations needed upon creation of the first device.
 */
static void device_init(void)
{
    device_init_done = TRUE;
    fetch_display_metrics();
}


/******************************************************************************
 *           create_android_physdev
 */
static ANDROID_PDEVICE *create_android_physdev(void)
{
    ANDROID_PDEVICE *physdev;

    if (!device_init_done) device_init();

    if (!(physdev = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*physdev) ))) return NULL;
    return physdev;
}

/**********************************************************************
 *           ANDROID_CreateDC
 */
static BOOL ANDROID_CreateDC( PHYSDEV *pdev, LPCWSTR driver, LPCWSTR device,
                              LPCWSTR output, const DEVMODEW* initData )
{
    ANDROID_PDEVICE *physdev = create_android_physdev();

    if (!physdev) return FALSE;

    push_dc_driver( pdev, &physdev->dev, &android_drv_funcs );
    return TRUE;
}


/**********************************************************************
 *           ANDROID_CreateCompatibleDC
 */
static BOOL ANDROID_CreateCompatibleDC( PHYSDEV orig, PHYSDEV *pdev )
{
    ANDROID_PDEVICE *physdev = create_android_physdev();

    if (!physdev) return FALSE;

    push_dc_driver( pdev, &physdev->dev, &android_drv_funcs );
    return TRUE;
}


/**********************************************************************
 *           ANDROID_DeleteDC
 */
static BOOL ANDROID_DeleteDC( PHYSDEV dev )
{
    HeapFree( GetProcessHeap(), 0, dev );
    return TRUE;
}


/***********************************************************************
 *           ANDROID_GetDeviceCaps
 */
static INT ANDROID_GetDeviceCaps( PHYSDEV dev, INT cap )
{
    switch(cap)
    {
    case HORZRES:        return screen_width;
    case VERTRES:        return screen_height;
    case DESKTOPHORZRES: return virtual_screen_rect.right - virtual_screen_rect.left;
    case DESKTOPVERTRES: return virtual_screen_rect.bottom - virtual_screen_rect.top;
    case BITSPIXEL:      return screen_bpp;
    default:
        dev = GET_NEXT_PHYSDEV( dev, pGetDeviceCaps );
        return dev->funcs->pGetDeviceCaps( dev, cap );
    }
}


/***********************************************************************
 *           ANDROID_GetMonitorInfo
 */
BOOL CDECL ANDROID_GetMonitorInfo( HMONITOR handle, LPMONITORINFO info )
{
    if (handle != (HMONITOR)1)
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return FALSE;
    }
    info->rcMonitor = default_monitor.rcMonitor;
    info->rcWork = default_monitor.rcWork;
    info->dwFlags = default_monitor.dwFlags;
    if (info->cbSize >= sizeof(MONITORINFOEXW))
        lstrcpyW( ((MONITORINFOEXW *)info)->szDevice, default_monitor.szDevice );
    return TRUE;
}


/***********************************************************************
 *           ANDROID_EnumDisplayMonitors
 */
BOOL CDECL ANDROID_EnumDisplayMonitors( HDC hdc, LPRECT rect, MONITORENUMPROC proc, LPARAM lp )
{
    if (hdc)
    {
        POINT origin;
        RECT limit, monrect;

        if (!GetDCOrgEx( hdc, &origin )) return FALSE;
        if (GetClipBox( hdc, &limit ) == ERROR) return FALSE;

        if (rect && !IntersectRect( &limit, &limit, rect )) return TRUE;

        monrect = default_monitor.rcMonitor;
        OffsetRect( &monrect, -origin.x, -origin.y );
        if (IntersectRect( &monrect, &monrect, &limit ))
            if (!proc( (HMONITOR)1, hdc, &monrect, lp ))
                return FALSE;
    }
    else
    {
        RECT unused;
        if (!rect || IntersectRect( &unused, &default_monitor.rcMonitor, rect ))
            if (!proc( (HMONITOR)1, 0, &default_monitor.rcMonitor, lp ))
                return FALSE;
    }
    return TRUE;
}


static const struct gdi_dc_funcs android_drv_funcs =
{
    NULL,                               /* pAbortDoc */
    NULL,                               /* pAbortPath */
    NULL,                               /* pAlphaBlend */
    NULL,                               /* pAngleArc */
    NULL,                               /* pArc */
    NULL,                               /* pArcTo */
    NULL,                               /* pBeginPath */
    NULL,                               /* pBlendImage */
    NULL,                               /* pChord */
    NULL,                               /* pCloseFigure */
    ANDROID_CreateCompatibleDC,         /* pCreateCompatibleDC */
    ANDROID_CreateDC,                   /* pCreateDC */
    ANDROID_DeleteDC,                   /* pDeleteDC */
    NULL,                               /* pDeleteObject */
    NULL,                               /* pDeviceCapabilities */
    NULL,                               /* pEllipse */
    NULL,                               /* pEndDoc */
    NULL,                               /* pEndPage */
    NULL,                               /* pEndPath */
    NULL,                               /* pEnumFonts */
    NULL,                               /* pEnumICMProfiles */
    NULL,                               /* pExcludeClipRect */
    NULL,                               /* pExtDeviceMode */
    NULL,                               /* pExtEscape */
    NULL,                               /* pExtFloodFill */
    NULL,                               /* pExtSelectClipRgn */
    NULL,                               /* pExtTextOut */
    NULL,                               /* pFillPath */
    NULL,                               /* pFillRgn */
    NULL,                               /* pFlattenPath */
    NULL,                               /* pFontIsLinked */
    NULL,                               /* pFrameRgn */
    NULL,                               /* pGdiComment */
    NULL,                               /* pGetBoundsRect */
    NULL,                               /* pGetCharABCWidths */
    NULL,                               /* pGetCharABCWidthsI */
    NULL,                               /* pGetCharWidth */
    ANDROID_GetDeviceCaps,              /* pGetDeviceCaps */
    NULL,                               /* pGetDeviceGammaRamp */
    NULL,                               /* pGetFontData */
    NULL,                               /* pGetFontRealizationInfo */
    NULL,                               /* pGetFontUnicodeRanges */
    NULL,                               /* pGetGlyphIndices */
    NULL,                               /* pGetGlyphOutline */
    NULL,                               /* pGetICMProfile */
    NULL,                               /* pGetImage */
    NULL,                               /* pGetKerningPairs */
    NULL,                               /* pGetNearestColor */
    NULL,                               /* pGetOutlineTextMetrics */
    NULL,                               /* pGetPixel */
    NULL,                               /* pGetSystemPaletteEntries */
    NULL,                               /* pGetTextCharsetInfo */
    NULL,                               /* pGetTextExtentExPoint */
    NULL,                               /* pGetTextExtentExPointI */
    NULL,                               /* pGetTextFace */
    NULL,                               /* pGetTextMetrics */
    NULL,                               /* pGradientFill */
    NULL,                               /* pIntersectClipRect */
    NULL,                               /* pInvertRgn */
    NULL,                               /* pLineTo */
    NULL,                               /* pModifyWorldTransform */
    NULL,                               /* pMoveTo */
    NULL,                               /* pOffsetClipRgn */
    NULL,                               /* pOffsetViewportOrg */
    NULL,                               /* pOffsetWindowOrg */
    NULL,                               /* pPaintRgn */
    NULL,                               /* pPatBlt */
    NULL,                               /* pPie */
    NULL,                               /* pPolyBezier */
    NULL,                               /* pPolyBezierTo */
    NULL,                               /* pPolyDraw */
    NULL,                               /* pPolyPolygon */
    NULL,                               /* pPolyPolyline */
    NULL,                               /* pPolygon */
    NULL,                               /* pPolyline */
    NULL,                               /* pPolylineTo */
    NULL,                               /* pPutImage */
    NULL,                               /* pRealizeDefaultPalette */
    NULL,                               /* pRealizePalette */
    NULL,                               /* pRectangle */
    NULL,                               /* pResetDC */
    NULL,                               /* pRestoreDC */
    NULL,                               /* pRoundRect */
    NULL,                               /* pSaveDC */
    NULL,                               /* pScaleViewportExt */
    NULL,                               /* pScaleWindowExt */
    NULL,                               /* pSelectBitmap */
    NULL,                               /* pSelectBrush */
    NULL,                               /* pSelectClipPath */
    NULL,                               /* pSelectFont */
    NULL,                               /* pSelectPalette */
    NULL,                               /* pSelectPen */
    NULL,                               /* pSetArcDirection */
    NULL,                               /* pSetBkColor */
    NULL,                               /* pSetBkMode */
    NULL,                               /* pSetBoundsRect */
    NULL,                               /* pSetDCBrushColor */
    NULL,                               /* pSetDCPenColor */
    NULL,                               /* pSetDIBitsToDevice */
    NULL,                               /* pSetDeviceClipping */
    NULL,                               /* pSetDeviceGammaRamp */
    NULL,                               /* pSetLayout */
    NULL,                               /* pSetMapMode */
    NULL,                               /* pSetMapperFlags */
    NULL,                               /* pSetPixel */
    NULL,                               /* pSetPolyFillMode */
    NULL,                               /* pSetROP2 */
    NULL,                               /* pSetRelAbs */
    NULL,                               /* pSetStretchBltMode */
    NULL,                               /* pSetTextAlign */
    NULL,                               /* pSetTextCharacterExtra */
    NULL,                               /* pSetTextColor */
    NULL,                               /* pSetTextJustification */
    NULL,                               /* pSetViewportExt */
    NULL,                               /* pSetViewportOrg */
    NULL,                               /* pSetWindowExt */
    NULL,                               /* pSetWindowOrg */
    NULL,                               /* pSetWorldTransform */
    NULL,                               /* pStartDoc */
    NULL,                               /* pStartPage */
    NULL,                               /* pStretchBlt */
    NULL,                               /* pStretchDIBits */
    NULL,                               /* pStrokeAndFillPath */
    NULL,                               /* pStrokePath */
    NULL,                               /* pUnrealizePalette */
    NULL,                               /* pWidenPath */
    NULL,                               /* wine_get_wgl_driver */
    GDI_PRIORITY_GRAPHICS_DRV           /* priority */
};


/******************************************************************************
 *           ANDROID_get_gdi_driver
 */
const struct gdi_dc_funcs * CDECL ANDROID_get_gdi_driver( unsigned int version )
{
    if (version != WINE_GDI_DRIVER_VERSION)
    {
        ERR( "version mismatch, gdi32 wants %u but wineandroid has %u\n", version, WINE_GDI_DRIVER_VERSION );
        return NULL;
    }
    return &android_drv_funcs;
}
