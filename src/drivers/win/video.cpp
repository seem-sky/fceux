/* FCE Ultra - NES/Famicom Emulator
*
* Copyright notice for this file:
*  Copyright (C) 2002 Xodnizel
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "video.h"
#include "../../drawing.h"
#include "gui.h"
#include "../../fceu.h"
#include "../../video.h"
#include "input.h"
#include "mapinput.h"

extern bool fullscreenByDoubleclick;

static int RecalcCustom(void);
void InputScreenChanged(int fs);
void UpdateRendBounds();

static DDCAPS caps;
static int mustrestore=0;
static DWORD CBM[3];

static int bpp;
static int vflags;
static int veflags;

int disvaccel = 1;      //Disable video hardware acceleration.  1 by default (meaning disable in windowed but not Fullscreen)

int fssync=0;
int winsync=0;

int winspecial = 0;
int NTSCwinspecial = 0;
int vmod = 0;

vmdef vmodes[11] =
{
	{0,0,0,VMDF_DXBLT|VMDF_STRFS,1,1,0},	// Custom - set to current resolution at the first launch
	{320,240,8,0,1,1,0}, //1
	{512,384,8,0,1,1,0}, //2
	{640,480,32,0,1,1,0}, //3
	{640,480,32,0,1,1,0}, //4
	{640,480,32,0,1,1,0}, //5
	{640,480,32,VMDF_DXBLT,2,2,0}, //6
	{1024,768,32,VMDF_DXBLT,4,3,0}, //7
	{1280,1024,32,VMDF_DXBLT,5,4,0}, //8
	{1600,1200,32,VMDF_DXBLT,6,5,0}, //9
	{800,600,32,VMDF_DXBLT|VMDF_STRFS,0,0}    //10
};

extern uint8 PALRAM[0x20];

PALETTEENTRY *color_palette;

static int PaletteChanged=0;

LPDIRECTDRAWCLIPPER lpClipper=0;
LPDIRECTDRAW  lpDD=0;
LPDIRECTDRAW7 lpDD7=0;
LPDIRECTDRAWPALETTE lpddpal = 0;

DDSURFACEDESC2 ddsd;
DDSURFACEDESC2 ddsdback;
DDSURFACEDESC2 ddsd_Resizable;

LPDIRECTDRAWSURFACE7  lpDDSPrimary=0;
LPDIRECTDRAWSURFACE7  lpDDSDBack=0;
LPDIRECTDRAWSURFACE7  lpDDSBack=0;
LPDIRECTDRAWSURFACE7  lpDDSResizable=0;

DDBLTFX blitfx = { sizeof(DDBLTFX) };

RECT resizable_surface_rect = {0};

#define RELEASE(x) if(x) { x->Release(); x = 0; }

static void ShowDDErr(char *s)
{
	char tempo[512];
	sprintf(tempo,"DirectDraw: %s",s);
	FCEUD_PrintError(tempo);
}

int RestoreDD(int w)
{
	if (w == 2)	// lpDDSResizable
	{
		if(!lpDDSResizable) return 0;
		if(IDirectDrawSurface7_Restore(lpDDSResizable)!=DD_OK) return 0;
	} else if (w == 1)	// lpDDSBack
	{
		if(!lpDDSBack) return 0;
		if(IDirectDrawSurface7_Restore(lpDDSBack)!=DD_OK) return 0;
	} else	// 0 means lpDDSPrimary
	{
		if(!lpDDSPrimary) return 0;
		if(IDirectDrawSurface7_Restore(lpDDSPrimary)!=DD_OK) return 0;
	}
	veflags|=1;
	return 1;
}

void FCEUD_SetPalette(unsigned char index, unsigned char r, unsigned char g, unsigned char b)
{
	if (force_grayscale)
	{
		// convert the palette entry to grayscale
		int gray = ((float)r * 0.299 + (float)g * 0.587 + (float)b * 0.114);
		color_palette[index].peRed = gray;
		color_palette[index].peGreen = gray;
		color_palette[index].peBlue = gray;
	} else
	{
		color_palette[index].peRed = r;
		color_palette[index].peGreen = g;
		color_palette[index].peBlue = b;
	}
	PaletteChanged=1;
}

void FCEUD_GetPalette(unsigned char i, unsigned char *r, unsigned char *g, unsigned char *b)
{
	*r=color_palette[i].peRed;
	*g=color_palette[i].peGreen;
	*b=color_palette[i].peBlue;
}

static bool firstInitialize = true;
static int InitializeDDraw(int fs)
{
	//only init the palette the first time through
	if(firstInitialize) {
		firstInitialize = false;
    color_palette = (PALETTEENTRY*)malloc(256 * sizeof(PALETTEENTRY));
	}

	//(disvaccel&(1<<(fs?1:0)))?(GUID FAR *)DDCREATE_EMULATIONONLY:
	ddrval = DirectDrawCreate((disvaccel&(1<<(fs?1:0)))?(GUID FAR *)DDCREATE_EMULATIONONLY:NULL, &lpDD, NULL);
	if (ddrval != DD_OK)
	{
		//ShowDDErr("Error creating DirectDraw object.");
		FCEU_printf("Error creating DirectDraw object.\n");
		return 0;
	}

	//mbg merge 7/17/06 changed:
	ddrval = IDirectDraw_QueryInterface(lpDD,IID_IDirectDraw7,(LPVOID *)&lpDD7);
	//ddrval = IDirectDraw_QueryInterface(lpDD,&IID_IDirectDraw7,(LPVOID *)&lpDD7);
	IDirectDraw_Release(lpDD);

	if (ddrval != DD_OK)
	{
		//ShowDDErr("Error querying interface.");
		FCEU_printf("Error querying interface.\n");
		return 0;
	}

	caps.dwSize=sizeof(caps);
	if(IDirectDraw7_GetCaps(lpDD7,&caps,0)!=DD_OK)
	{
		//ShowDDErr("Error getting capabilities.");
		FCEU_printf("Error getting capabilities.\n");
		return 0;
	}
	return 1;
}

static int GetBPP(void)
{
	DDPIXELFORMAT ddpix;

	memset(&ddpix,0,sizeof(ddpix));
	ddpix.dwSize=sizeof(ddpix);

	ddrval=IDirectDrawSurface7_GetPixelFormat(lpDDSPrimary,&ddpix);
	if (ddrval != DD_OK)
	{
		//ShowDDErr("Error getting primary surface pixel format.");
		FCEU_printf("Error getting primary surface pixel format.\n");
		return 0;
	}

	if(ddpix.dwFlags&DDPF_RGB)
	{
		//mbg merge 7/17/06 removed silly dummy union stuff now that we have c++
		bpp=ddpix.dwRGBBitCount;
		CBM[0]=ddpix.dwRBitMask;
		CBM[1]=ddpix.dwGBitMask;
		CBM[2]=ddpix.dwBBitMask;
	}
	else
	{
		//ShowDDErr("RGB data not valid.");
		FCEU_printf("RGB data not valid.\n");
		return 0;
	}
	if(bpp==15) bpp=16;

	return 1;
}

static int InitBPPStuff(int fs)
{

	int specfilteropt = 0;
	switch (winspecial)
	{
	case 3:
	specfilteropt = NTSCwinspecial;
	break;
	}

	if(bpp >= 16)
	{
		InitBlitToHigh(bpp >> 3, CBM[0], CBM[1], CBM[2], 0, fs?vmodes[vmod].special:winspecial,specfilteropt);
	}
	else if(bpp==8)
	{
		ddrval=IDirectDraw7_CreatePalette( lpDD7, DDPCAPS_8BIT|DDPCAPS_ALLOW256|DDPCAPS_INITIALIZE,color_palette,&lpddpal,NULL);
		if (ddrval != DD_OK)
		{
			//ShowDDErr("Error creating palette object.");
			FCEU_printf("Error creating palette object.\n");
			return 0;
		}
		ddrval=IDirectDrawSurface7_SetPalette(lpDDSPrimary, lpddpal);
		if (ddrval != DD_OK)
		{
			//ShowDDErr("Error setting palette object.");
			FCEU_printf("Error setting palette object.\n");
			return 0;
		}
	}
	return 1;
}

void RecreateResizableSurface(int width, int height)
{
	if (!lpDD7)
		return;	// DirectDraw isn't initialized yet
	// delete old surface
	RELEASE(lpDDSResizable);
	// create new surface
	memset(&ddsd_Resizable, 0, sizeof(ddsd_Resizable));
	ddsd_Resizable.dwSize = sizeof(ddsd_Resizable);
	ddsd_Resizable.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
	ddsd_Resizable.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
	ddsd_Resizable.dwWidth = width;
	ddsd_Resizable.dwHeight = height;
	ddrval = IDirectDraw7_CreateSurface(lpDD7, &ddsd_Resizable, &lpDDSResizable, (IUnknown FAR*)NULL);
	if (ddrval != DD_OK)
	{
		//ShowDDErr("Error creating resizable surface.");
		FCEU_printf("Error creating resizable surface.\n");
		return;
	}
	RecolorResizableSurface();
	// calculate resizable_surface_rect
	double current_aspectratio = (double)width / (double)height;
	double needed_aspectratio = (double)(VNSWID) / (double)(FSettings.TotalScanlines());
	if (current_aspectratio == needed_aspectratio)
	{
		resizable_surface_rect.left = 0;
		resizable_surface_rect.right = width;
		resizable_surface_rect.top = 0;
		resizable_surface_rect.bottom = height;
	} else if (current_aspectratio > needed_aspectratio)
	{
		// the window is wider than emulated screen
		resizable_surface_rect.top = 0;
		resizable_surface_rect.bottom = height;
		int center_x = width / 2;
		width = (double)((double)height * needed_aspectratio);
		resizable_surface_rect.left = center_x - (width / 2);
		resizable_surface_rect.right = center_x + (width / 2);
	} else
	{
		// the window is taller than emulated screen
		resizable_surface_rect.left = 0;
		resizable_surface_rect.right = width;
		int center_y = height / 2;
		height = (double)((double)width / needed_aspectratio);
		resizable_surface_rect.top = center_y - (height / 2);
		resizable_surface_rect.bottom = center_y + (height / 2);
	}
}

void RecolorResizableSurface()
{
	if (eoptions & EO_BGCOLOR)
	{
		// fill the surface using BG color from PPU
		unsigned char r, g, b;
		FCEUD_GetPalette(0x80 | PALRAM[0], &r, &g, &b);
		blitfx.dwFillColor = (r << 16) + (g << 8) + b;
		ddrval = IDirectDrawSurface7_Blt(lpDDSResizable, NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_ASYNC, &blitfx);
	} else
	{
		// fill the surface with black color
		blitfx.dwFillColor = 0;
		ddrval = IDirectDrawSurface7_Blt(lpDDSResizable, NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &blitfx);
	}
}

int SetVideoMode(int fs)
{
	int specmul = 1;    // Special scaler size multiplier

	if(fs)
		if(!vmod)
			if(!RecalcCustom())
				return(0);

	vflags=0;
	veflags=1;
	PaletteChanged=1;

	ResetVideo();
	fullscreen=fs;

	if(!InitializeDDraw(fs)) return(1);     // DirectDraw not initialized


	if(!fs)
	{ 
		// -Video Modes Tag-
		if(winspecial <= 3 && winspecial >= 1)
			specmul = 2;
		else if(winspecial >= 4 && winspecial <= 5)
			specmul = 3;
		else
			specmul = 1;

		ShowCursorAbs(1);
		windowedfailed=1;
		HideFWindow(0);

		ddrval = IDirectDraw7_SetCooperativeLevel ( lpDD7, hAppWnd, DDSCL_NORMAL);
		if (ddrval != DD_OK)
		{
			//ShowDDErr("Error setting cooperative level.");
			FCEU_printf("Error setting cooperative level.\n");
			return 1;
		}

		//Beginning
		memset(&ddsd,0,sizeof(ddsd));
		ddsd.dwSize = sizeof(ddsd);
		ddsd.dwFlags = DDSD_CAPS;
		ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

		ddrval = IDirectDraw7_CreateSurface ( lpDD7, &ddsd, &lpDDSPrimary,(IUnknown FAR*)NULL);
		if (ddrval != DD_OK)
		{
			//ShowDDErr("Error creating primary surface.");
			FCEU_printf("Error creating primary surface.\n");
			return 1;
		}

		memset(&ddsdback,0,sizeof(ddsdback));
		ddsdback.dwSize=sizeof(ddsdback);
		ddsdback.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
		ddsdback.ddsCaps.dwCaps= DDSCAPS_OFFSCREENPLAIN;

		ddsdback.dwWidth=256 * specmul;
		ddsdback.dwHeight=FSettings.TotalScanlines() * specmul;

		//If the blit hardware can't stretch, assume it's cheap(and slow)
		//and create the buffer in system memory.

		if(!(caps.dwCaps&DDCAPS_BLTSTRETCH))
			ddsdback.ddsCaps.dwCaps|=DDSCAPS_SYSTEMMEMORY;

		ddrval = IDirectDraw7_CreateSurface ( lpDD7, &ddsdback, &lpDDSBack, (IUnknown FAR*)NULL);
		if (ddrval != DD_OK)
		{
			//ShowDDErr("Error creating secondary surface.");
			FCEU_printf("Error creating secondary surface.\n");
			return 0;
		}
		
		if(!GetBPP())
			return 0;

		if(bpp!=16 && bpp!=24 && bpp!=32)
		{
			//ShowDDErr("Current bit depth not supported!");
			FCEU_printf("Current bit depth not supported!\n");
			return 0;
		}

		if(!InitBPPStuff(fs))
			return 0;

		ddrval=IDirectDraw7_CreateClipper(lpDD7,0,&lpClipper,0);
		if (ddrval != DD_OK)
		{
			//ShowDDErr("Error creating clipper.");
			FCEU_printf("Error creating clipper.\n");
			return 0;
		}

		ddrval=IDirectDrawClipper_SetHWnd(lpClipper,0,hAppWnd);
		if (ddrval != DD_OK)
		{
			//ShowDDErr("Error setting clipper window.");
			FCEU_printf("Error setting clipper window.\n");
			return 0;
		}
		ddrval=IDirectDrawSurface7_SetClipper(lpDDSPrimary,lpClipper);
		if (ddrval != DD_OK)
		{
			//ShowDDErr("Error attaching clipper to primary surface.");
			FCEU_printf("Error attaching clipper to primary surface.\n");
			return 0;
		}

		windowedfailed=0;
		SetMainWindowStuff();
	} else
	{
		//Following is full-screen
		if(vmod == 0)	// Custom mode
		{
			// -Video Modes Tag-
			if(vmodes[0].special <= 3 && vmodes[0].special >= 1)
				specmul = 2;
			else if(vmodes[0].special >= 4 && vmodes[0].special <= 5)
				specmul = 3;
			else
				specmul = 1;
		}
		HideFWindow(1);

		if ((vmodes[vmod].flags & VMDF_STRFS) && (eoptions & EO_BESTFIT))
		{
			ddrval = IDirectDraw7_SetCooperativeLevel ( lpDD7, hAppWnd, DDSCL_NORMAL);
			if (ddrval != DD_OK)
			{
				//ShowDDErr("Error setting cooperative level.");
				FCEU_printf("Error setting cooperative level.\n");
				return 0;
			}
			RecreateResizableSurface(vmodes[vmod].x, vmodes[vmod].y);
		}

		ddrval = IDirectDraw7_SetCooperativeLevel ( lpDD7, hAppWnd,DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN | DDSCL_ALLOWREBOOT);
		if (ddrval != DD_OK)
		{
			//ShowDDErr("Error setting cooperative level.");
			FCEU_printf("Error setting cooperative level.\n");
			return 0;
		}

		ddrval = IDirectDraw7_SetDisplayMode(lpDD7, vmodes[vmod].x, vmodes[vmod].y,vmodes[vmod].bpp,0,0);
		if (ddrval != DD_OK)
		{
			//ShowDDErr("Error setting display mode.");
			FCEU_printf("Error setting display mode.\n");
			return 0;
		}
		if(vmodes[vmod].flags&VMDF_DXBLT)
		{
			memset(&ddsdback,0,sizeof(ddsdback));
			ddsdback.dwSize=sizeof(ddsdback);
			ddsdback.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
			ddsdback.ddsCaps.dwCaps= DDSCAPS_OFFSCREENPLAIN;

			ddsdback.dwWidth=256 * specmul; //vmodes[vmod].srect.right;
			ddsdback.dwHeight=FSettings.TotalScanlines() * specmul; //vmodes[vmod].srect.bottom;

			if(!(caps.dwCaps&DDCAPS_BLTSTRETCH))
				ddsdback.ddsCaps.dwCaps|=DDSCAPS_SYSTEMMEMORY; 

			ddrval = IDirectDraw7_CreateSurface ( lpDD7, &ddsdback, &lpDDSBack, (IUnknown FAR*)NULL);
			if(ddrval!=DD_OK)
			{
				//ShowDDErr("Error creating secondary surface.");
				FCEU_printf("Error creating secondary surface.\n");
				return 0;
			}
		}

		// create foreground surface

		memset(&ddsd,0,sizeof(ddsd));
		ddsd.dwSize = sizeof(ddsd);

		ddsd.dwFlags = DDSD_CAPS;
		ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

		if(fssync==3) // Double buffering.
		{
			ddsd.dwFlags |= DDSD_BACKBUFFERCOUNT;
			ddsd.dwBackBufferCount = 1;
			ddsd.ddsCaps.dwCaps |= DDSCAPS_COMPLEX | DDSCAPS_FLIP;
		}

		ddrval = IDirectDraw7_CreateSurface ( lpDD7, &ddsd, &lpDDSPrimary,(IUnknown FAR*)NULL);
		if (ddrval != DD_OK)
		{
			//ShowDDErr("Error creating primary surface.");
			FCEU_printf("Error creating primary surface.\n");
			return 0;
		} 

		if(fssync==3)
		{
			DDSCAPS2 tmp;

			memset(&tmp,0,sizeof(tmp));
			tmp.dwCaps=DDSCAPS_BACKBUFFER;

			if(IDirectDrawSurface7_GetAttachedSurface(lpDDSPrimary,&tmp,&lpDDSDBack)!=DD_OK)
			{
				//ShowDDErr("Error getting attached surface.");
				FCEU_printf("Error getting attached surface.\n");
				return 0;
			}
		}

		if(!GetBPP())
			return 0;
		if(!InitBPPStuff(fs))
			return 0;

		mustrestore=1;

		if (eoptions & EO_HIDEMOUSE)
			ShowCursorAbs(0);
	}

	fullscreen=fs;
	return 1;
}

//draw input aids if we are fullscreen
bool FCEUD_ShouldDrawInputAids()
{
	return fullscreen!=0;
}

static void BlitScreenWindow(uint8 *XBuf);
static void BlitScreenFull(uint8 *XBuf);

static void FCEUD_VerticalSync()
{
	if(!NoWaiting)
	{
		int ws;

		if(fullscreen) ws=fssync;
		else ws = winsync;

		if(ws==1)
			IDirectDraw7_WaitForVerticalBlank(lpDD7,DDWAITVB_BLOCKBEGIN,0);
		else if(ws == 2)   
		{
			BOOL invb = 0;

			while((DD_OK == IDirectDraw7_GetVerticalBlankStatus(lpDD7,&invb)) && !invb)
				Sleep(0);
		}
	}
}

//static uint8 *XBSave;
void FCEUD_BlitScreen(uint8 *XBuf)
{
	xbsave = XBuf;

	if(fullscreen)
	{
		BlitScreenFull(XBuf);
	}
	else
	{
		if(!windowedfailed)
			BlitScreenWindow(XBuf);
	}
}

static void FixPaletteHi(void)
{
	SetPaletteBlitToHigh((uint8*)color_palette); //mbg merge 7/17/06 added cast
}

static void BlitScreenWindow(unsigned char *XBuf)
{
	int pitch;
	unsigned char *ScreenLoc;
	static RECT srect;
	RECT wrect;
	int specialmul;

	if (!lpDDSBack) return;

	// -Video Modes Tag-
	if(winspecial <= 3 && winspecial >= 1)
		specialmul = 2;
	else if(winspecial >= 4 && winspecial <= 5)
		specialmul = 3;
	else specialmul = 1;

	srect.top=srect.left=0;
	srect.right=VNSWID * specialmul;
	srect.bottom=FSettings.TotalScanlines() * specialmul;

	if(PaletteChanged==1)
	{
		FixPaletteHi();
		PaletteChanged=0;
	}

	if(!GetClientAbsRect(&wrect)) return;

	ddrval=IDirectDrawSurface7_Lock(lpDDSBack,NULL,&ddsdback, 0, NULL);
	if(ddrval!=DD_OK)
	{
		if(ddrval==DDERR_SURFACELOST) RestoreDD(1);
		return;
	}

	//mbg merge 7/17/06 removing dummyunion stuff
	pitch=ddsdback.lPitch;
	ScreenLoc=(unsigned char*)ddsdback.lpSurface; //mbg merge 7/17/06 added cst
	if(veflags&1)
	{
		memset(ScreenLoc,0,pitch*ddsdback.dwHeight);
		veflags&=~1;
	}
	Blit8ToHigh(XBuf+FSettings.FirstSLine*256+VNSCLIP,ScreenLoc, VNSWID, FSettings.TotalScanlines(), pitch,specialmul,specialmul);

	IDirectDrawSurface7_Unlock(lpDDSBack, NULL);

	if (eoptions & EO_BESTFIT && (resizable_surface_rect.top || resizable_surface_rect.left))
	{
		// clear lpDDSResizable surface 
		if (eoptions & EO_BGCOLOR)
			RecolorResizableSurface();
		// blit from lpDDSBack to lpDDSResizable using best fit
		if (IDirectDrawSurface7_Blt(lpDDSResizable, &resizable_surface_rect, lpDDSBack, &srect, DDBLT_ASYNC, 0) != DD_OK)
		{
			ddrval = IDirectDrawSurface7_Blt(lpDDSResizable, &resizable_surface_rect, lpDDSBack, &srect, DDBLT_WAIT, 0);
			if(ddrval != DD_OK)
			{
				if(ddrval == DDERR_SURFACELOST)
				{
					RestoreDD(2);
					RestoreDD(1);
				}
				return;
			}
		}
		// blit from lpDDSResizable to screen (lpDDSPrimary)
		FCEUD_VerticalSync();		// aquanull 2011-11-28 fix tearing
		if (IDirectDrawSurface7_Blt(lpDDSPrimary, &wrect, lpDDSResizable, NULL, DDBLT_ASYNC, 0) != DD_OK)
		{
			ddrval = IDirectDrawSurface7_Blt(lpDDSPrimary, &wrect, lpDDSResizable, NULL, DDBLT_WAIT, 0);
			if(ddrval != DD_OK)
			{
				if(ddrval == DDERR_SURFACELOST)
				{
					RestoreDD(2);
					RestoreDD(0);
				}
				return;
			}
		}
	} else
	{
		// blit directly from lpDDSBack to screen (lpDDSPrimary)
		FCEUD_VerticalSync();		// aquanull 2011-11-28 fix tearing
		if(IDirectDrawSurface7_Blt(lpDDSPrimary, &wrect, lpDDSBack, &srect, DDBLT_ASYNC, 0) != DD_OK)
		{
			ddrval = IDirectDrawSurface7_Blt(lpDDSPrimary, &wrect, lpDDSBack, &srect, DDBLT_WAIT, 0);
			if(ddrval != DD_OK)
			{
				if(ddrval == DDERR_SURFACELOST)
				{
					RestoreDD(1);
					RestoreDD(0);
				}
				return;
			}
		}
	}
}

static void DD_FillRect(LPDIRECTDRAWSURFACE7 surf, int left, int top, int right, int bottom, DWORD color)
{
	RECT r;
	SetRect(&r,left,top,right,bottom);
	DDBLTFX fx;
	memset(&fx,0,sizeof(DDBLTFX));
	fx.dwSize = sizeof(DDBLTFX);
	//fx.dwFillColor = color;
	fx.dwFillColor = 0; //color is just for debug
	surf->Blt(&r,NULL,NULL,DDBLT_COLORFILL | DDBLT_WAIT,&fx);
}


static void BlitScreenFull(uint8 *XBuf)
{
	static int pitch;
	char *ScreenLoc;
	//unsigned long x; //mbg merge 7/17/06 removed
	//uint8 y; //mbg merge 7/17/06 removed
	RECT srect,drect;
	LPDIRECTDRAWSURFACE7 lpDDSVPrimary;
	int specmul;    // Special scaler size multiplier
	// -Video Modes Tag-
	if(vmodes[0].special <= 3 && vmodes[0].special >= 1)
		specmul = 2;
	else if(vmodes[0].special >= 4 && vmodes[0].special <= 5)
		specmul = 3;
	else
		specmul = 1;

	if(fssync==3)
		lpDDSVPrimary=lpDDSDBack;
	else
		lpDDSVPrimary=lpDDSPrimary;

	if (!lpDDSVPrimary) return;

	if(PaletteChanged==1)
	{
		if(bpp>=16)
			FixPaletteHi();
		else
		{
			ddrval=IDirectDrawPalette_SetEntries(lpddpal,0,0,256,color_palette);
			if(ddrval!=DD_OK)
			{
				if(ddrval==DDERR_SURFACELOST) RestoreDD(0);
				return;
			}   
		}
		PaletteChanged=0;
	}

	if(vmodes[vmod].flags&VMDF_DXBLT)
	{
		// start rendering into backbuffer
 		ddrval=IDirectDrawSurface7_Lock(lpDDSBack,NULL,&ddsdback, 0, NULL);
		if(ddrval!=DD_OK)
		{
			if(ddrval==DDERR_SURFACELOST) RestoreDD(1);
			return;
		}
		ScreenLoc=(char *)ddsdback.lpSurface; //mbg merge 7/17/06 added cast
		pitch=ddsdback.lPitch; //mbg merge 7/17/06 removed dummyunion stuff

		srect.top=0;
		srect.left=0;
		srect.right=VNSWID * specmul;
		srect.bottom=FSettings.TotalScanlines() * specmul;

		if(vmodes[vmod].flags&VMDF_STRFS)
		{
			drect.top=0;
			drect.left=0;
			drect.right=vmodes[vmod].x;
			drect.bottom=vmodes[vmod].y;
		}
		else
		{
			drect.top=(vmodes[vmod].y-(FSettings.TotalScanlines()*vmodes[vmod].yscale))>>1;
			drect.bottom=drect.top+(FSettings.TotalScanlines()*vmodes[vmod].yscale);
			drect.left=(vmodes[vmod].x-VNSWID*vmodes[vmod].xscale)>>1;
			drect.right=drect.left+VNSWID*vmodes[vmod].xscale;


			RECT fullScreen;
			fullScreen.left = fullScreen.top = 0;
			fullScreen.right = vmodes[vmod].x;
			fullScreen.bottom = vmodes[vmod].y;
			RECT r;
			r = drect;
			int left = r.left;
			int top = r.top;
			int right = r.right;
			int bottom = r.bottom;
			DD_FillRect(lpDDSVPrimary,0,0,left,top,RGB(255,0,0)); //topleft
			DD_FillRect(lpDDSVPrimary,left,0,right,top,RGB(128,0,0)); //topcenter
			DD_FillRect(lpDDSVPrimary,right,0,fullScreen.right,top,RGB(0,255,0)); //topright
			DD_FillRect(lpDDSVPrimary,0,top,left,bottom,RGB(0,128,0));  //left
			DD_FillRect(lpDDSVPrimary,right,top,fullScreen.right,bottom,RGB(0,0,255)); //right
			DD_FillRect(lpDDSVPrimary,0,bottom,left,fullScreen.bottom,RGB(0,0,128)); //bottomleft
			DD_FillRect(lpDDSVPrimary,left,bottom,right,fullScreen.bottom,RGB(255,0,255)); //bottomcenter
			DD_FillRect(lpDDSVPrimary,right,bottom,fullScreen.right,fullScreen.bottom,RGB(0,255,255)); //bottomright
		}
	} else
	{
		// start rendering directly to screen
		FCEUD_VerticalSync();
		ddrval=IDirectDrawSurface7_Lock(lpDDSVPrimary,NULL,&ddsd, 0, NULL);
		if(ddrval!=DD_OK)
		{
			if(ddrval==DDERR_SURFACELOST) RestoreDD(0);
			return;
		}

		ScreenLoc=(char*)ddsd.lpSurface; //mbg merge 7/17/06 added cast
		pitch=ddsd.lPitch; //mbg merge 7/17/06 removing dummyunion stuff
	}

	if(veflags&1)
	{
		if(vmodes[vmod].flags&VMDF_DXBLT)
		{
			veflags|=2;
			memset((char *)ScreenLoc,0,pitch*srect.bottom);
		}
		else
		{
			memset((char *)ScreenLoc,0,pitch*vmodes[vmod].y);
		}
		PaletteChanged=1;
		veflags&=~1;
	}

	//mbg 6/29/06 merge
#ifndef MSVC
	if(vmod==5)
	{
		if(eoptions&EO_CLIPSIDES)
		{
			asm volatile(
				"xorl %%edx, %%edx\n\t"
				"akoop1:\n\t"
				"movb $120,%%al     \n\t"
				"akoop2:\n\t"
				"movb 1(%%esi),%%dl\n\t"
				"shl  $16,%%edx\n\t"
				"movb (%%esi),%%dl\n\t"
				"movl %%edx,(%%edi)\n\t"
				"addl $2,%%esi\n\t"
				"addl $4,%%edi\n\t"
				"decb %%al\n\t"
				"jne akoop2\n\t"
				"addl $16,%%esi\n\t"
				"addl %%ecx,%%edi\n\t"
				"decb %%bl\n\t"
				"jne akoop1\n\t"
				:
			: "S" (XBuf+FSettings.FirstSLine*256+VNSCLIP), "D" (ScreenLoc+((240-FSettings.TotalScanlines())/2)*pitch+(640-(VNSWID<<1))/2),"b" (FSettings.TotalScanlines()), "c" ((pitch-VNSWID)<<1)
				: "%al", "%edx", "%cc" );
		}
		else
		{
			asm volatile(
				"xorl %%edx, %%edx\n\t"
				"koop1:\n\t"
				"movb $128,%%al     \n\t"
				"koop2:\n\t"
				"movb 1(%%esi),%%dl\n\t"
				"shl  $16,%%edx\n\t"
				"movb (%%esi),%%dl\n\t"
				"movl %%edx,(%%edi)\n\t"
				"addl $2,%%esi\n\t"
				"addl $4,%%edi\n\t"
				"decb %%al\n\t"
				"jne koop2\n\t"
				"addl %%ecx,%%edi\n\t"
				"decb %%bl\n\t"
				"jne koop1\n\t"
				:
			: "S" (XBuf+FSettings.FirstSLine*256), "D" (ScreenLoc+((240-FSettings.TotalScanlines())/2)*pitch+(640-512)/2),"b" (FSettings.TotalScanlines()), "c" (pitch-512+pitch)
				: "%al", "%edx", "%cc" );
		}
	}
	else if(vmod==4)
	{
		if(eoptions&EO_CLIPSIDES)
		{
			asm volatile(
				"ayoop1:\n\t"
				"movb $120,%%al     \n\t"
				"ayoop2:\n\t"
				"movb 1(%%esi),%%dh\n\t"
				"movb %%dh,%%dl\n\t"
				"shl  $16,%%edx\n\t"
				"movb (%%esi),%%dl\n\t"
				"movb %%dl,%%dh\n\t"               // Ugh
				"movl %%edx,(%%edi)\n\t"
				"addl $2,%%esi\n\t"
				"addl $4,%%edi\n\t"
				"decb %%al\n\t"
				"jne ayoop2\n\t"
				"addl $16,%%esi\n\t"
				"addl %%ecx,%%edi\n\t"
				"decb %%bl\n\t"
				"jne ayoop1\n\t"
				:
			: "S" (XBuf+FSettings.FirstSLine*256+VNSCLIP), "D" (ScreenLoc+((240-FSettings.TotalScanlines())/2)*pitch+(640-(VNSWID<<1))/2),"b" (FSettings.TotalScanlines()), "c" ((pitch-VNSWID)<<1)
				: "%al", "%edx", "%cc" );
		}
		else
		{
			asm volatile(
				"yoop1:\n\t"
				"movb $128,%%al     \n\t"
				"yoop2:\n\t"
				"movb 1(%%esi),%%dh\n\t"
				"movb %%dh,%%dl\n\t"
				"shl  $16,%%edx\n\t"
				"movb (%%esi),%%dl\n\t"
				"movb %%dl,%%dh\n\t"               // Ugh
				"movl %%edx,(%%edi)\n\t"
				"addl $2,%%esi\n\t"
				"addl $4,%%edi\n\t"
				"decb %%al\n\t"
				"jne yoop2\n\t"
				"addl %%ecx,%%edi\n\t"
				"decb %%bl\n\t"
				"jne yoop1\n\t"
				:
			: "S" (XBuf+FSettings.FirstSLine*256), "D" (ScreenLoc+((240-FSettings.TotalScanlines())/2)*pitch+(640-512)/2),"b" (FSettings.TotalScanlines()), "c" (pitch-512+pitch)
				: "%al", "%edx", "%cc" );
		}
	}
	else
#endif 
		//mbg 6/29/06 merge
	{
		if(!(vmodes[vmod].flags&VMDF_DXBLT))
		{  
			// -Video Modes Tag-
			if(vmodes[vmod].special)
				ScreenLoc += (vmodes[vmod].drect.left*(bpp>>3)) + ((vmodes[vmod].drect.top)*pitch);   
			else
				ScreenLoc+=((vmodes[vmod].x-VNSWID)>>1)*(bpp>>3)+(((vmodes[vmod].y-FSettings.TotalScanlines())>>1))*pitch;
		}

		if(bpp>=16)
		{
			Blit8ToHigh(XBuf+FSettings.FirstSLine*256+VNSCLIP,(uint8*)ScreenLoc, VNSWID, FSettings.TotalScanlines(), pitch,specmul,specmul); //mbg merge 7/17/06 added cast
		}
		else
		{
			XBuf+=FSettings.FirstSLine*256+VNSCLIP;
			// -Video Modes Tag-
			if(vmodes[vmod].special)
				Blit8To8(XBuf,(uint8*)ScreenLoc, VNSWID, FSettings.TotalScanlines(), pitch,vmodes[vmod].xscale,vmodes[vmod].yscale,0,vmodes[vmod].special); //mbg merge 7/17/06 added cast
			else
				Blit8To8(XBuf,(uint8*)ScreenLoc, VNSWID, FSettings.TotalScanlines(), pitch,1,1,0,0); //mbg merge 7/17/06 added cast
		}
	}

	if(vmodes[vmod].flags&VMDF_DXBLT)
	{ 
		IDirectDrawSurface7_Unlock(lpDDSBack, NULL);

		if (eoptions & EO_BESTFIT && (resizable_surface_rect.top || resizable_surface_rect.left) && !vmod)
		{
			// clear lpDDSResizable surface 
			RecolorResizableSurface();
			// blit from lpDDSBack to lpDDSResizable using best fit
			if (IDirectDrawSurface7_Blt(lpDDSResizable, &resizable_surface_rect, lpDDSBack, &srect, DDBLT_ASYNC, 0) != DD_OK)
			{
				ddrval = IDirectDrawSurface7_Blt(lpDDSResizable, &resizable_surface_rect, lpDDSBack, &srect, DDBLT_WAIT, 0);
				if(ddrval != DD_OK)
				{
					if(ddrval == DDERR_SURFACELOST)
					{
						RestoreDD(2);
						RestoreDD(1);
					}
					return;
				}
			}
			// blit from lpDDSResizable to screen
			RECT fullScreen;
			fullScreen.left = fullScreen.top = 0;
			fullScreen.right = vmodes[vmod].x;
			fullScreen.bottom = vmodes[vmod].y;
			FCEUD_VerticalSync();
			if (IDirectDrawSurface7_Blt(lpDDSVPrimary, &fullScreen, lpDDSResizable, &fullScreen, DDBLT_ASYNC, 0) != DD_OK)
			{
				ddrval = IDirectDrawSurface7_Blt(lpDDSVPrimary, &fullScreen, lpDDSResizable, &fullScreen, DDBLT_WAIT, 0);
				if(ddrval != DD_OK)
				{
					if(ddrval == DDERR_SURFACELOST)
					{
						RestoreDD(2);
						RestoreDD(0);
					}
					return;
				}
			}
		} else
		{
			// blit directly from lpDDSBack to screen
			FCEUD_VerticalSync();
			if(veflags&2)
			{
				// clear screen surface (is that really necessary?)
				if(IDirectDrawSurface7_Lock(lpDDSVPrimary,NULL,&ddsd, 0, NULL)==DD_OK)
				{
					memset(ddsd.lpSurface,0,ddsd.lPitch*vmodes[vmod].y); //mbg merge 7/17/06 removing dummyunion stuff
					IDirectDrawSurface7_Unlock(lpDDSVPrimary, NULL);
					veflags&=~2;
				}
			}
			if(IDirectDrawSurface7_Blt(lpDDSVPrimary, &drect,lpDDSBack,&srect,DDBLT_ASYNC,0)!=DD_OK)
			{
				ddrval=IDirectDrawSurface7_Blt(lpDDSVPrimary, &drect,lpDDSBack,&srect,DDBLT_WAIT,0);
				if(ddrval!=DD_OK)
				{
					if(ddrval==DDERR_SURFACELOST)
					{
						RestoreDD(0);
						RestoreDD(1);
					}
					return;
				}
			}
		}
	} else
	{
		IDirectDrawSurface7_Unlock(lpDDSVPrimary, NULL);
	}

	if(fssync==3)
	{
		IDirectDrawSurface7_Flip(lpDDSPrimary,0,0);
	}
}

void ResetVideo(void)
{
	ShowCursorAbs(1);
	KillBlitToHigh();
	if(lpDD7)
		if(mustrestore)
		{
			IDirectDraw7_RestoreDisplayMode(lpDD7);
			mustrestore=0;
		}

	RELEASE(lpddpal);
	RELEASE(lpDDSBack);
	RELEASE(lpDDSPrimary);
	RELEASE(lpDDSResizable);
	RELEASE(lpClipper);
	RELEASE(lpDD7);
}

int specialmlut[5] = {1,2,2,3,3};

void ResetCustomMode()
{
	// use current display settings
	if (!lpDD7)
		return;

	vmdef *cmode = &vmodes[0];

	DDSURFACEDESC2 temp_ddsd;
	temp_ddsd.dwSize = sizeof(DDSURFACEDESC2);
	IDirectDraw7_GetDisplayMode(lpDD7, &temp_ddsd);
	if (FAILED(ddrval))
		return;
	cmode->x = temp_ddsd.dwWidth;
	cmode->y = temp_ddsd.dwHeight;
	cmode->bpp = temp_ddsd.ddpfPixelFormat.dwRGBBitCount;
	cmode->xscale = cmode->yscale = 1;
	cmode->flags = VMDF_DXBLT|VMDF_STRFS;
}

static int RecalcCustom(void)
{
	vmdef *cmode = &vmodes[0];

	if ((cmode->x <= 0) || (cmode->y <= 0))
		ResetCustomMode();

	if(cmode->flags&VMDF_STRFS)
	{
		cmode->flags |= VMDF_DXBLT;
	} else if(cmode->xscale!=1 || cmode->yscale!=1 || cmode->special) 
	{
		cmode->flags &= ~VMDF_DXBLT;
		if(cmode->special)
		{
			int mult = specialmlut[cmode->special];

			if(cmode->xscale < mult)
				cmode->xscale = mult;
			if(cmode->yscale < mult)
				cmode->yscale = mult;

			if(cmode->xscale != mult || cmode->yscale != mult)
				cmode->flags|=VMDF_DXBLT;
		}
		else
			cmode->flags|=VMDF_DXBLT;


		if(VNSWID*cmode->xscale>cmode->x)
		{
			if(cmode->special)
			{
				FCEUD_PrintError("Scaled width is out of range.");
				return(0);
			}
			else
			{
				FCEUD_PrintError("Scaled width is out of range.  Reverting to no horizontal scaling.");
				cmode->xscale=1;
			}
		}
		if(FSettings.TotalScanlines()*cmode->yscale>cmode->y)
		{
			if(cmode->special)
			{
				FCEUD_PrintError("Scaled height is out of range.");
				return(0);
			}
			else
			{
				FCEUD_PrintError("Scaled height is out of range.  Reverting to no vertical scaling.");
				cmode->yscale=1;
			}
		}

		cmode->srect.left=VNSCLIP;
		cmode->srect.top=FSettings.FirstSLine;
		cmode->srect.right=256-VNSCLIP;
		cmode->srect.bottom=FSettings.LastSLine+1;

		cmode->drect.top=(cmode->y-(FSettings.TotalScanlines()*cmode->yscale))>>1;
		cmode->drect.bottom=cmode->drect.top+FSettings.TotalScanlines()*cmode->yscale;

		cmode->drect.left=(cmode->x-(VNSWID*cmode->xscale))>>1;
		cmode->drect.right=cmode->drect.left+VNSWID*cmode->xscale;
	}

	// -Video Modes Tag-
	if((cmode->special == 1 || cmode->special == 4) && cmode->bpp == 8)
	{
		cmode->bpp = 32;
		//FCEUD_PrintError("HQ2x/HQ3x requires 16bpp or 32bpp(best).");
		//return(0);
	}

	if(cmode->x<VNSWID)
	{
		FCEUD_PrintError("Horizontal resolution is too low.");
		return(0);
	}
	if(cmode->y<FSettings.TotalScanlines() && !(cmode->flags&VMDF_STRFS))
	{
		FCEUD_PrintError("Vertical resolution must not be less than the total number of drawn scanlines.");
		return(0);
	}

	return(1);
}

BOOL SetDlgItemDouble(HWND hDlg, int item, double value)
{
	char buf[9]; //mbg merge 7/19/06 changed to 9 to leave room for \0
	sprintf(buf,"%.6f",value);
	return SetDlgItemText(hDlg, item, buf); //mbg merge 7/17/06 added this return value
}

double GetDlgItemDouble(HWND hDlg, int item)
{
	char buf[8];
	double ret = 0;

	GetDlgItemText(hDlg, item, buf, 8);
	sscanf(buf,"%lf",&ret);
	return(ret);
}

BOOL CALLBACK VideoConCallB(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static char *vmstr[11]={
		"Custom",
		"320x240 Full Screen",
		"512x384 Centered",
		"640x480 Centered",
		"640x480 Scanlines",
		"640x480 \"4 per 1\"",
		"640x480 2x,2y",
		"1024x768 4x,3y",
		"1280x1024 5x,4y",
		"1600x1200 6x,5y",
		"800x600 Stretched"
	};
	int x;

	switch(uMsg)
	{
	case WM_INITDIALOG:
	{
		/*
		for(x=0;x<11;x++)
			SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_MODE,CB_ADDSTRING,0,(LPARAM)(LPSTR)vmstr[x]);
		SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_MODE,CB_SETCURSEL,vmod,(LPARAM)(LPSTR)0);
		*/

		SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_BPP,CB_ADDSTRING,0,(LPARAM)(LPSTR)"8");
		SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_BPP,CB_ADDSTRING,0,(LPARAM)(LPSTR)"16");
		SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_BPP,CB_ADDSTRING,0,(LPARAM)(LPSTR)"24");
		SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_BPP,CB_ADDSTRING,0,(LPARAM)(LPSTR)"32");
		SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_BPP,CB_SETCURSEL,(vmodes[0].bpp>>3)-1,(LPARAM)(LPSTR)0);

		SetDlgItemInt(hwndDlg,IDC_VIDEOCONFIG_XRES,vmodes[0].x,0);
		SetDlgItemInt(hwndDlg,IDC_VIDEOCONFIG_YRES,vmodes[0].y,0);

		//SetDlgItemInt(hwndDlg,IDC_VIDEOCONFIG_XSCALE,vmodes[0].xscale,0);
		//SetDlgItemInt(hwndDlg,IDC_VIDEOCONFIG_YSCALE,vmodes[0].yscale,0);
		//CheckRadioButton(hwndDlg,IDC_RADIO_SCALE,IDC_RADIO_STRETCH,(vmodes[0].flags&VMDF_STRFS)?IDC_RADIO_STRETCH:IDC_RADIO_SCALE);

		// -Video Modes Tag-
		char *str[]={"<none>","hq2x","Scale2x","NTSC 2x","hq3x","Scale3x"};
		int x;
		for(x=0;x<6;x++)
		{
			SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_SCALER_FS,CB_ADDSTRING,0,(LPARAM)(LPSTR)str[x]);
			SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_SCALER_WIN,CB_ADDSTRING,0,(LPARAM)(LPSTR)str[x]);
		}

		SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_SCALER_FS,CB_SETCURSEL,vmodes[0].special,(LPARAM)(LPSTR)0);
		SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_SCALER_WIN,CB_SETCURSEL,winspecial,(LPARAM)(LPSTR)0);

		if(eoptions&EO_FSAFTERLOAD)
			CheckDlgButton(hwndDlg,IDC_VIDEOCONFIG_AUTO_FS,BST_CHECKED);

		if(eoptions&EO_HIDEMOUSE)
			CheckDlgButton(hwndDlg,IDC_VIDEOCONFIG_HIDEMOUSE,BST_CHECKED);

		if(eoptions&EO_CLIPSIDES)
			CheckDlgButton(hwndDlg,IDC_VIDEOCONFIG_CLIPSIDES,BST_CHECKED);

		if(eoptions&EO_BESTFIT)
			CheckDlgButton(hwndDlg, IDC_VIDEOCONFIG_BESTFIT, BST_CHECKED);

		if(eoptions&EO_BGCOLOR)
			CheckDlgButton(hwndDlg,IDC_VIDEOCONFIG_CONSOLE_BGCOLOR,BST_CHECKED);

		if(disvaccel&1)
			CheckDlgButton(hwndDlg,IDC_DISABLE_HW_ACCEL_WIN,BST_CHECKED);

		if(disvaccel&2)
			CheckDlgButton(hwndDlg,IDC_DISABLE_HW_ACCEL_FS,BST_CHECKED);

		if(eoptions&EO_FORCEISCALE)
			CheckDlgButton(hwndDlg,IDC_FORCE_INT_VIDEO_SCALARS,BST_CHECKED);

		if(eoptions&EO_FORCEASPECT)
			CheckDlgButton(hwndDlg,IDC_FORCE_ASPECT_CORRECTION,BST_CHECKED);

		SetDlgItemInt(hwndDlg,IDC_SCANLINE_FIRST_NTSC,srendlinen,0);
		SetDlgItemInt(hwndDlg,IDC_SCANLINE_LAST_NTSC,erendlinen,0);

		SetDlgItemInt(hwndDlg,IDC_SCANLINE_FIRST_PAL,srendlinep,0);
		SetDlgItemInt(hwndDlg,IDC_SCANLINE_LAST_PAL,erendlinep,0);


		SetDlgItemDouble(hwndDlg, IDC_WINSIZE_MUL_X, winsizemulx);
		SetDlgItemDouble(hwndDlg, IDC_WINSIZE_MUL_Y, winsizemuly);
		//SetDlgItemDouble(hwndDlg, IDC_VIDEOCONFIG_ASPECT_X, saspectw);
		//SetDlgItemDouble(hwndDlg, IDC_VIDEOCONFIG_ASPECT_Y, saspecth);

		SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_SYNC_METHOD_WIN,CB_ADDSTRING,0,(LPARAM)(LPSTR)"<none>");
		SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_SYNC_METHOD_FS,CB_ADDSTRING,0,(LPARAM)(LPSTR)"<none>");

		SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_SYNC_METHOD_WIN,CB_ADDSTRING,0,(LPARAM)(LPSTR)"Wait for VBlank");
		SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_SYNC_METHOD_WIN,CB_ADDSTRING,0,(LPARAM)(LPSTR)"Lazy wait for VBlank");

		SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_SYNC_METHOD_FS,CB_ADDSTRING,0,(LPARAM)(LPSTR)"Wait for VBlank");
		SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_SYNC_METHOD_FS,CB_ADDSTRING,0,(LPARAM)(LPSTR)"Lazy wait for VBlank");
		SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_SYNC_METHOD_FS,CB_ADDSTRING,0,(LPARAM)(LPSTR)"Double Buffering");

		SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_SYNC_METHOD_WIN,CB_SETCURSEL,winsync,(LPARAM)(LPSTR)0);
		SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_SYNC_METHOD_FS,CB_SETCURSEL,fssync,(LPARAM)(LPSTR)0);

		if(eoptions&EO_NOSPRLIM)
			CheckDlgButton(hwndDlg,IDC_VIDEOCONFIG_NO8LIM,BST_CHECKED);

		char buf[1024] = "Full Screen";
		int c = FCEUD_CommandMapping[EMUCMD_MISC_TOGGLEFULLSCREEN];
		if (c)
		{
			strcat(buf, " (");
			strcat(buf, GetKeyComboName(c));
			if (fullscreenByDoubleclick)
				strcat(buf, " or double-click)");
			else
				strcat(buf, ")");
		} else if (fullscreenByDoubleclick)
		{
			strcat(buf, " (double-click anywhere)");
		}
		SetDlgItemText(hwndDlg, IDC_VIDEOCONFIG_FS, buf);
		break;
	}
	case WM_CLOSE:
	case WM_QUIT: goto gornk;
	case WM_COMMAND:
		if(!(wParam>>16))
			switch(wParam&0xFFFF)
		{
			case ID_CANCEL:
gornk:

				if(IsDlgButtonChecked(hwndDlg,IDC_VIDEOCONFIG_CLIPSIDES)==BST_CHECKED)
				{
					eoptions|=EO_CLIPSIDES;
					ClipSidesOffset = 8;
				}
				else
				{
					eoptions&=~EO_CLIPSIDES;
					ClipSidesOffset = 0;
				}

				if (IsDlgButtonChecked(hwndDlg, IDC_VIDEOCONFIG_BESTFIT) == BST_CHECKED)
					eoptions |= EO_BESTFIT;
				else
					eoptions &= ~EO_BESTFIT;

				if (IsDlgButtonChecked(hwndDlg, IDC_VIDEOCONFIG_CONSOLE_BGCOLOR) == BST_CHECKED)
					eoptions |= EO_BGCOLOR;
				else
					eoptions &= ~EO_BGCOLOR;

				if(IsDlgButtonChecked(hwndDlg,IDC_VIDEOCONFIG_NO8LIM)==BST_CHECKED)
					eoptions|=EO_NOSPRLIM;
				else
					eoptions&=~EO_NOSPRLIM;

				srendlinen=GetDlgItemInt(hwndDlg,IDC_SCANLINE_FIRST_NTSC,0,0);
				erendlinen=GetDlgItemInt(hwndDlg,IDC_SCANLINE_LAST_NTSC,0,0);
				srendlinep=GetDlgItemInt(hwndDlg,IDC_SCANLINE_FIRST_PAL,0,0);
				erendlinep=GetDlgItemInt(hwndDlg,IDC_SCANLINE_LAST_PAL,0,0);


				if(erendlinen>239) erendlinen=239;
				if(srendlinen>erendlinen) srendlinen=erendlinen;

				if(erendlinep>239) erendlinep=239;
				if(srendlinep>erendlinen) srendlinep=erendlinep;

				UpdateRendBounds();

				/*
				if(IsDlgButtonChecked(hwndDlg,IDC_RADIO_STRETCH)==BST_CHECKED)
					vmodes[0].flags |= VMDF_STRFS|VMDF_DXBLT;
				else
					vmodes[0].flags &= ~(VMDF_STRFS|VMDF_DXBLT);
				vmod=SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_MODE,CB_GETCURSEL,0,(LPARAM)(LPSTR)0);
				*/
				vmodes[0].x=GetDlgItemInt(hwndDlg,IDC_VIDEOCONFIG_XRES,0,0);
				vmodes[0].y=GetDlgItemInt(hwndDlg,IDC_VIDEOCONFIG_YRES,0,0);
				vmodes[0].bpp=(SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_BPP,CB_GETCURSEL,0,(LPARAM)(LPSTR)0)+1)<<3;

				//vmodes[0].xscale=GetDlgItemInt(hwndDlg,IDC_VIDEOCONFIG_XSCALE,0,0);
				//vmodes[0].yscale=GetDlgItemInt(hwndDlg,IDC_VIDEOCONFIG_YSCALE,0,0);
				vmodes[0].special=SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_SCALER_FS,CB_GETCURSEL,0,(LPARAM)(LPSTR)0);

				winspecial = SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_SCALER_WIN,CB_GETCURSEL,0,(LPARAM)(LPSTR)0);
				disvaccel = 0;

				if(IsDlgButtonChecked(hwndDlg,IDC_DISABLE_HW_ACCEL_WIN)==BST_CHECKED)
					disvaccel |= 1;
				if(IsDlgButtonChecked(hwndDlg,IDC_DISABLE_HW_ACCEL_FS)==BST_CHECKED)
					disvaccel |= 2;


				if(IsDlgButtonChecked(hwndDlg,IDC_VIDEOCONFIG_FS)==BST_CHECKED)
					fullscreen=1;
				else
					fullscreen=0;

				if(IsDlgButtonChecked(hwndDlg,IDC_VIDEOCONFIG_AUTO_FS)==BST_CHECKED)
					eoptions|=EO_FSAFTERLOAD;
				else
					eoptions&=~EO_FSAFTERLOAD;

				if(IsDlgButtonChecked(hwndDlg,IDC_VIDEOCONFIG_HIDEMOUSE)==BST_CHECKED)
					eoptions|=EO_HIDEMOUSE;
				else
					eoptions&=~EO_HIDEMOUSE;

				eoptions &= ~(EO_FORCEISCALE | EO_FORCEASPECT);
				if(IsDlgButtonChecked(hwndDlg,IDC_FORCE_INT_VIDEO_SCALARS)==BST_CHECKED)
					eoptions|=EO_FORCEISCALE;
				if(IsDlgButtonChecked(hwndDlg,IDC_FORCE_ASPECT_CORRECTION)==BST_CHECKED)
					eoptions|=EO_FORCEASPECT;

				winsizemulx=GetDlgItemDouble(hwndDlg, IDC_WINSIZE_MUL_X);
				winsizemuly=GetDlgItemDouble(hwndDlg, IDC_WINSIZE_MUL_Y);
				//saspectw=GetDlgItemDouble(hwndDlg, IDC_VIDEOCONFIG_ASPECT_X);
				//saspecth=GetDlgItemDouble(hwndDlg, IDC_VIDEOCONFIG_ASPECT_Y);
				FixWXY(0);

				winsync=SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_SYNC_METHOD_WIN,CB_GETCURSEL,0,(LPARAM)(LPSTR)0);
				fssync=SendDlgItemMessage(hwndDlg,IDC_VIDEOCONFIG_SYNC_METHOD_FS,CB_GETCURSEL,0,(LPARAM)(LPSTR)0);
				EndDialog(hwndDlg,0);
				break;
		}
	}
	return 0;
}

void SetFSVideoMode()
{
	changerecursive=1;
	if(!SetVideoMode(1))
		SetVideoMode(0);
	changerecursive=0;
}


void DoVideoConfigFix(void)
{
	FCEUI_DisableSpriteLimitation(eoptions&EO_NOSPRLIM);
	UpdateRendBounds();
}

void PushCurrentVideoSettings()
{
	if(fullscreen)
	{
		SetFSVideoMode();
	}
	else
	{
		changerecursive = 1;
		SetVideoMode(0);
		changerecursive = 0;
		//SetMainWindowStuff();		// it's already called inside SetVideoMode()
	}
}

//Shows the Video configuration dialog.
void ConfigVideo(void)
{
	if ((vmodes[0].x <= 0) || (vmodes[0].y <= 0))
		ResetCustomMode();
	DialogBox(fceu_hInstance, "VIDEOCONFIG", hAppWnd, VideoConCallB); 
	DoVideoConfigFix();
	PushCurrentVideoSettings();
}

void FCEUD_VideoChanged()
{
	PushCurrentVideoSettings();
}