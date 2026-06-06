/*****************************************************************************/
/*                                                                           */
/*                  AmigaAMP-Isometric  v1.0                                    */
/*                                                                           */
/*  True 2:1 isometric 16x16 spectrum tower visualizer for AmigaAMP.          */
/*  Draws a black/grey LED grid with green/yellow/orange/red blocks and       */
/*  red peak-hold markers, inspired by classic equalizer displays.            */
/*                                                                           */
/*  Opens a borderless full-size window on the current Workbench/Public       */
/*  Screen, so the active Picasso96/RTG Workbench resolution is used.         */
/*                                                                           */
/*  Built as a normal AmigaDOS executable using the AmigaAMP PDK message      */
/*  port protocol.                                                           */
/*                                                                           */
/*****************************************************************************/

#include <string.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <exec/tasks.h>
#include <dos/dosextens.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <graphics/text.h>
#include <diskfont/diskfont.h>
#include <devices/timer.h>
#include <utility/tagitem.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/diskfont.h>

#include "TrackInfo.h"

#define PLUGIN_PORT_NAME       "AmigaAMP plugin port"
#define ESC_RAWKEY             0x45
#define INVALID_PEN            0xffffffffUL

#define NUM_BANDS              16
#define HISTORY_DEPTH          16
#define FRAME_USEC             16666UL
#define TITLE_MAX              256
#define INFO_MAX               256

const char VersionString[] = "$VER: AmigaAMP-Isometric 1.0 AmigaAmp Isometric by Andreas 'Andiweli' Stuermer (06.06.2026)";

char BinaryVisibleCreditString[] = "AmigaAmp Isometric by Andreas 'Andiweli' Stuermer";

/* AmigaAmp Isometric by Andreas 'Andiweli' Stuermer */
/* Real compiled data string: visible in the final binary with a hex/text editor. */
char BinaryCreditString[] = "AmigaAmp Isometric by Andreas 'Andiweli' Stuermer";

WORD PluginInit(void);
void PluginExit(void);
void PluginLoop(void);
void ShowRequester(char *Text, char *Button);

static WORD OpenLibraries(void);
static void CloseLibraries(void);
static WORD OpenVisualWindow(void);
static void CloseVisualWindow(void);
static void AllocatePens(void);
static void ReleasePens(void);
static WORD CreateBackBuffer(void);
static void DestroyBackBuffer(void);
static void OpenPreferredFonts(void);
static void ClosePreferredFonts(void);
static struct TextFont *TryOpenPreferredFont(const char *name, UWORD size);
static WORD OpenFontSet(const char *name, UWORD mainSize, UWORD titleSize, UWORD infoSize);
static void CopyBackBufferToWindow(void);
static WORD OpenFrameTimer(void);
static void CloseFrameTimer(void);
static void StartFrameTimer(void);
static void StopFrameTimer(void);
static WORD FinishFrameTimer(void);
static void BuildSpectrumBands(void);
static void UpdateLevelsFromSpectrum(void);
static void UpdateLevelsFromSamples(void);
static void UpdateRealtimeLevels(void);
static void DecayLevels(void);
static void SmoothLevel(LONG i, LONG target);
static void UpdateTrackInfo(void);
static void PushHistoryFromLevels(void);
static void RenderFrame(void);
static void CalculateIsoLayout(void);
static void DrawIsometricSpectrum(void);
static void DrawTrackOverlay(void);
static void DrawCenteredText(char *text, LONG y, ULONG pen, struct TextFont *font);
static void DrawCenteredTextAt(char *text, LONG cx, LONG y, ULONG pen, struct TextFont *font);
static void FillQuad(LONG x1, LONG y1, LONG x2, LONG y2, LONG x3, LONG y3, LONG x4, LONG y4, ULONG pen);
static void DrawIsoTile(LONG cx, LONG cy, LONG height, ULONG topPen, ULONG leftPen, ULONG rightPen);
static ULONG PenBandTop(LONG band);
static ULONG PenBandLeft(LONG band);
static ULONG PenBandRight(LONG band);
static ULONG PenBandFloor(LONG band);
static UBYTE ClampByte(LONG v);
static ULONG Color32(UBYTE v);
static void BuildBandRGB(LONG band, LONG *r, LONG *g, LONG *b);
static void SafeCopy(char *dst, const char *src, LONG dstSize);
static LONG SafeLen(const char *s);
static LONG AbsLong(LONG v);
static LONG ClampLong(LONG v, LONG lo, LONG hi);
static LONG MinLong(LONG a, LONG b);
static LONG MaxLong(LONG a, LONG b);

/***************************************************************************/
/* AmigaAMP PDK globals                                                     */
/***************************************************************************/

BYTE             PluginSignal;
ULONG            PluginMask;
WORD             Accepted;
struct Process   *PluginTask;
struct MsgPort   *PluginMP;
struct MsgPort   *PluginRP;
BYTE             InfoSignal;
ULONG            InfoMask;
struct TrackInfo *tinfo;

UWORD *SpecRawL;
UWORD *SpecRawR;
WORD  *SampleRaw;

struct PluginMessage {
	struct Message msg;
	ULONG          PluginMask;
	struct Process *PluginTask;
	UWORD          **SpecRawL;
	UWORD          **SpecRawR;
	WORD           Accepted;
	WORD           reserved0;

	ULONG            InfoMask;
	struct TrackInfo **tinfo;
	struct MsgPort   *PluginWP;

	WORD             **SampleRaw;
};

/***************************************************************************/
/* Visualizer globals                                                       */
/***************************************************************************/

struct Screen   *PluginScreen = NULL;
struct Window   *PluginWin    = NULL;
struct RastPort *DrawRP       = NULL;
struct RastPort *WindowRP     = NULL;
struct RastPort  BackRP;
struct BitMap   *BackBM       = NULL;
struct TextFont *MainFont     = NULL;
struct TextFont *TitleFont    = NULL;
struct TextFont *InfoFont     = NULL;
WORD             BackReady    = FALSE;
ULONG            WinMask      = 0;

struct MsgPort     *TimerMP      = NULL;
struct timerequest *TimerReq     = NULL;
WORD                TimerOpen    = FALSE;
WORD                TimerPending = FALSE;
ULONG               TimerMask    = 0;
LONG                FramesSinceAudio = 64;

LONG WinW = 0;
LONG WinH = 0;
LONG CenterX = 0;
LONG CenterY = 0;

ULONG BgPen       = INVALID_PEN;
ULONG TextPen     = INVALID_PEN;
ULONG SubTextPen  = INVALID_PEN;
ULONG FloorDimPen = INVALID_PEN;
ULONG BandPen[NUM_BANDS];
ULONG BandTopPen[NUM_BANDS];
ULONG BandSidePen[NUM_BANDS];
ULONG BandFloorPen[NUM_BANDS];
WORD  PensReady   = FALSE;

LONG VuLevel[NUM_BANDS];
LONG VuPeak[NUM_BANDS];
LONG VuVelocity[NUM_BANDS];
LONG PeakHold[NUM_BANDS];
LONG BandStart[NUM_BANDS];
LONG BandEnd[NUM_BANDS];
LONG SpectrumHistory[HISTORY_DEPTH][NUM_BANDS];
LONG FrameCounter = 0;

LONG IsoOriginX = 0;
LONG IsoOriginY = 0;
LONG IsoDX = 0;
LONG IsoDY = 0;
LONG IsoTileHalfW = 0;
LONG IsoTileHalfH = 0;
LONG IsoHeightScale = 0;
LONG IsoCellX[HISTORY_DEPTH][NUM_BANDS];
LONG IsoCellY[HISTORY_DEPTH][NUM_BANDS];
LONG LayoutReady = FALSE;
LONG LastLayoutW = -1;
LONG LastLayoutH = -1;

char MainArtistLine[TITLE_MAX];
char TitleLine[TITLE_MAX];
char ArtistLine[INFO_MAX];
char AlbumLine[INFO_MAX];
WORD HaveTrackInfo = FALSE;

struct AreaInfo BackAreaInfo;
WORD BackAreaBuffer[512];

#ifndef __SASC
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase      *GfxBase      = NULL;
struct Library      *DiskfontBase = NULL;
#endif


/***************************************************************************/
/* Main / AmigaAMP registration                                             */
/***************************************************************************/

int main(void)
{
	int res;
	struct PluginMessage *PluginMsg;
	struct PluginMessage *ReplyMsg;

	res = 0;
	PluginMsg = NULL;
	ReplyMsg = NULL;
	PluginSignal = -1;
	InfoSignal = -1;
	PluginMask = 0;
	InfoMask = 0;
	Accepted = FALSE;
	PluginTask = NULL;
	PluginMP = NULL;
	PluginRP = NULL;
	SpecRawL = NULL;
	SpecRawR = NULL;
	SampleRaw = NULL;
	tinfo = NULL;

	if(!OpenLibraries()) {
		return(20);
	}

	Forbid();
	PluginMP = FindPort(PLUGIN_PORT_NAME);
	Permit();
	if(!PluginMP) {
		ShowRequester("Could not find message port!\nAmigaAMP is probably not running.", "Abort");
		CloseLibraries();
		return(5);
	}

	if(PluginInit()) {
		PluginTask   = (struct Process *)FindTask(NULL);
		PluginSignal = AllocSignal(-1);
		InfoSignal   = AllocSignal(-1);

		if(PluginSignal != -1 && InfoSignal != -1) {
			PluginMask = 1L << PluginSignal;
			InfoMask   = 1L << InfoSignal;

			PluginMsg = (struct PluginMessage *)AllocVec(sizeof(struct PluginMessage), MEMF_PUBLIC | MEMF_CLEAR);
			PluginRP  = CreateMsgPort();

			if(PluginMsg && PluginRP) {
				PluginMsg->msg.mn_Node.ln_Type = NT_MESSAGE;
				PluginMsg->msg.mn_Length       = sizeof(struct PluginMessage);
				PluginMsg->msg.mn_ReplyPort    = PluginRP;
				PluginMsg->PluginMask          = PluginMask;
				PluginMsg->PluginTask          = PluginTask;
				PluginMsg->SpecRawL            = &SpecRawL;
				PluginMsg->SpecRawR            = &SpecRawR;
				PluginMsg->InfoMask            = InfoMask;
				PluginMsg->tinfo               = &tinfo;
				PluginMsg->PluginWP            = NULL;
				PluginMsg->SampleRaw           = &SampleRaw;

				Forbid();
				PluginMP = FindPort(PLUGIN_PORT_NAME);
				if(PluginMP) {
					PutMsg(PluginMP, (struct Message *)PluginMsg);
					Permit();

					WaitPort(PluginRP);
					ReplyMsg = (struct PluginMessage *)GetMsg(PluginRP);
					if(ReplyMsg) Accepted = ReplyMsg->Accepted;
					else Accepted = FALSE;

					if(Accepted) {
						PluginLoop();

						PluginMsg->PluginMask = 0;
						PluginMsg->PluginTask = NULL;
						PluginMsg->SpecRawL   = NULL;
						PluginMsg->SpecRawR   = NULL;
						PluginMsg->InfoMask   = 0;
						PluginMsg->tinfo      = NULL;
						PluginMsg->PluginWP   = NULL;
						PluginMsg->SampleRaw  = NULL;

						Forbid();
						PluginMP = FindPort(PLUGIN_PORT_NAME);
						if(PluginMP) {
							PutMsg(PluginMP, (struct Message *)PluginMsg);
							Permit();
							WaitPort(PluginRP);
							GetMsg(PluginRP);
						}
						else {
							Permit();
						}
					}
					else {
						ShowRequester("Plugin rejected by AmigaAMP!\nPerhaps another visual plugin is already running.", "Abort");
						res = 5;
					}
				}
				else {
					Permit();
					ShowRequester("Could not find message port!\nAmigaAMP is probably not running anymore.", "Abort");
					res = 5;
				}
			}
			else {
				ShowRequester("Could not create message or reply port!", "Abort");
				res = 5;
			}

			if(PluginMsg) FreeVec(PluginMsg);
			if(PluginRP) DeleteMsgPort(PluginRP);
		}
		else {
			ShowRequester("Signal allocation failure!", "Abort");
			res = 5;
		}

		if(PluginSignal != -1) FreeSignal(PluginSignal);
		if(InfoSignal   != -1) FreeSignal(InfoSignal);
	}
	else {
		ShowRequester("Plugin initialisation failed!", "Ok");
		res = 5;
	}

	PluginExit();
	CloseLibraries();
	return(res);
}

/***************************************************************************/
/* Init / Exit                                                              */
/***************************************************************************/

static WORD OpenLibraries(void)
{
#ifndef __SASC
	IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
	if(!IntuitionBase) return(FALSE);

	GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
	if(!GfxBase) {
		CloseLibrary((struct Library *)IntuitionBase);
		IntuitionBase = NULL;
		return(FALSE);
	}

	/* Optional: used only for nicer title/artist fonts. If diskfont.library
	   is unavailable, the plugin simply falls back to the screen font. */
	DiskfontBase = OpenLibrary("diskfont.library", 37);
#endif
	return(TRUE);
}

static void CloseLibraries(void)
{
#ifndef __SASC
	if(DiskfontBase) {
		CloseLibrary(DiskfontBase);
		DiskfontBase = NULL;
	}
	if(GfxBase) {
		CloseLibrary((struct Library *)GfxBase);
		GfxBase = NULL;
	}
	if(IntuitionBase) {
		CloseLibrary((struct Library *)IntuitionBase);
		IntuitionBase = NULL;
	}
#endif
}

WORD PluginInit(void)
{
	if(BinaryVisibleCreditString[0] == 0) return(FALSE);
	LONG i;
	LONG z;

	SafeCopy(MainArtistLine, "", TITLE_MAX);
	SafeCopy(TitleLine, "", TITLE_MAX);
	SafeCopy(ArtistLine, "", INFO_MAX);
	SafeCopy(AlbumLine, "", INFO_MAX);
	HaveTrackInfo = FALSE;

	for(i = 0; i < NUM_BANDS; i++) {
		VuLevel[i] = 0;
		VuPeak[i] = 0;
		VuVelocity[i] = 0;
		PeakHold[i] = 0;
		BandStart[i] = 0;
		BandEnd[i] = 1;
	}
	for(z = 0; z < HISTORY_DEPTH; z++) {
		for(i = 0; i < NUM_BANDS; i++) {
			SpectrumHistory[z][i] = 0;
		}
	}
	for(i = 0; i < NUM_BANDS; i++) {
		BandPen[i] = INVALID_PEN;
		BandTopPen[i] = INVALID_PEN;
		BandSidePen[i] = INVALID_PEN;
		BandFloorPen[i] = INVALID_PEN;
	}

	if(!OpenVisualWindow()) return(FALSE);
	OpenPreferredFonts();
	AllocatePens();
	BuildSpectrumBands();
	OpenFrameTimer();
	return(TRUE);
}

void PluginExit(void)
{
	StopFrameTimer();
	CloseFrameTimer();
	ReleasePens();
	ClosePreferredFonts();
	CloseVisualWindow();
}

static WORD OpenVisualWindow(void)
{
	PluginScreen = LockPubScreen(NULL);
	if(!PluginScreen) return(FALSE);

	WinW = PluginScreen->Width;
	WinH = PluginScreen->Height;
	CenterX = WinW >> 1;
	CenterY = WinH >> 1;
	LayoutReady = FALSE;

	PluginWin = OpenWindowTags(NULL,
		WA_CustomScreen,  (ULONG)PluginScreen,
		WA_Left,          0,
		WA_Top,           0,
		WA_Width,         WinW,
		WA_Height,        WinH,
		WA_Title,         (ULONG)"AmigaAMP-Isometric",
		WA_Borderless,    TRUE,
		WA_Activate,      TRUE,
		WA_RMBTrap,       TRUE,
		WA_IDCMP,         IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS,
		WA_SimpleRefresh, TRUE,
	TAG_END);

	if(!PluginWin) {
		UnlockPubScreen(NULL, PluginScreen);
		PluginScreen = NULL;
		return(FALSE);
	}

	WindowRP = PluginWin->RPort;
	DrawRP = WindowRP;
	CreateBackBuffer();
	WinMask = 1L << PluginWin->UserPort->mp_SigBit;

	ScreenToFront(PluginScreen);
	WindowToFront(PluginWin);
	ActivateWindow(PluginWin);
	return(TRUE);
}

static void CloseVisualWindow(void)
{
	DestroyBackBuffer();

	if(PluginWin) {
		CloseWindow(PluginWin);
		PluginWin = NULL;
	}
	if(PluginScreen) {
		UnlockPubScreen(NULL, PluginScreen);
		PluginScreen = NULL;
	}
	DrawRP = NULL;
	WindowRP = NULL;
	WinMask = 0;
	LayoutReady = FALSE;
}

static WORD CreateBackBuffer(void)
{
	UBYTE depth;

	BackReady = FALSE;
	BackBM = NULL;

	if(!PluginScreen || !PluginWin || !PluginScreen->RastPort.BitMap) return(FALSE);
	depth = PluginScreen->RastPort.BitMap->Depth;
	if(depth == 0) return(FALSE);

	BackBM = AllocBitMap(WinW, WinH, depth, BMF_CLEAR, PluginScreen->RastPort.BitMap);
	if(!BackBM) return(FALSE);

	InitRastPort(&BackRP);
	BackRP.BitMap = BackBM;
	InitArea(&BackAreaInfo, BackAreaBuffer, 128);
	BackRP.AreaInfo = &BackAreaInfo;
	if(PluginWin->RPort && PluginWin->RPort->Font) SetFont(&BackRP, PluginWin->RPort->Font);

	DrawRP = &BackRP;
	BackReady = TRUE;
	return(TRUE);
}

static void DestroyBackBuffer(void)
{
	if(BackBM) {
		FreeBitMap(BackBM);
		BackBM = NULL;
	}
	BackReady = FALSE;
	BackRP.AreaInfo = NULL;
	BackRP.BitMap = NULL;
	if(PluginWin) DrawRP = PluginWin->RPort;
	else DrawRP = NULL;
}

static void CopyBackBufferToWindow(void)
{
	if(BackReady && BackBM && PluginWin && PluginWin->RPort) {
		BltBitMapRastPort(BackBM, 0, 0, PluginWin->RPort, 0, 0, WinW, WinH, 0xc0);
	}
}

static void AllocatePens(void)
{
	struct ColorMap *cm;
	LONG i;
	LONG r;
	LONG g;
	LONG b;

	if(!PluginScreen) return;
	cm = PluginScreen->ViewPort.ColorMap;
	if(!cm) return;

	BgPen        = ObtainBestPen(cm, 0x00000000, 0x00000000, 0x18181818, TAG_END);
	TextPen      = ObtainBestPen(cm, 0xffffffff, 0xffffffff, 0xffffffff, TAG_END);
	SubTextPen   = ObtainBestPen(cm, 0xd8d8d8d8, 0xd8d8d8d8, 0xd8d8d8d8, TAG_END);
	FloorDimPen  = ObtainBestPen(cm, 0x14141414, 0x14141414, 0x14141414, TAG_END);

	for(i = 0; i < NUM_BANDS; i++) {
		BuildBandRGB(i, &r, &g, &b);

		BandPen[i] = ObtainBestPen(cm,
			Color32(ClampByte(r)),
			Color32(ClampByte(g)),
			Color32(ClampByte(b)),
			TAG_END);

		BandTopPen[i] = ObtainBestPen(cm,
			Color32(ClampByte(r + 58)),
			Color32(ClampByte(g + 58)),
			Color32(ClampByte(b + 58)),
			TAG_END);

		BandSidePen[i] = ObtainBestPen(cm,
			Color32(ClampByte((r * 58) / 100)),
			Color32(ClampByte((g * 58) / 100)),
			Color32(ClampByte((b * 58) / 100)),
			TAG_END);

		BandFloorPen[i] = ObtainBestPen(cm,
			Color32(ClampByte((r * 36) / 100)),
			Color32(ClampByte((g * 36) / 100)),
			Color32(ClampByte((b * 36) / 100)),
			TAG_END);
	}

	PensReady = TRUE;
}

static void ReleasePens(void)
{
	struct ColorMap *cm;
	LONG i;

	if(!PluginScreen || !PensReady) return;
	cm = PluginScreen->ViewPort.ColorMap;
	if(!cm) return;

	if(BgPen       != INVALID_PEN) ReleasePen(cm, BgPen);
	if(TextPen     != INVALID_PEN) ReleasePen(cm, TextPen);
	if(SubTextPen  != INVALID_PEN) ReleasePen(cm, SubTextPen);
	if(FloorDimPen != INVALID_PEN) ReleasePen(cm, FloorDimPen);

	for(i = 0; i < NUM_BANDS; i++) {
		if(BandPen[i]      != INVALID_PEN) ReleasePen(cm, BandPen[i]);
		if(BandTopPen[i]   != INVALID_PEN) ReleasePen(cm, BandTopPen[i]);
		if(BandSidePen[i]  != INVALID_PEN) ReleasePen(cm, BandSidePen[i]);
		if(BandFloorPen[i] != INVALID_PEN) ReleasePen(cm, BandFloorPen[i]);
		BandPen[i] = INVALID_PEN;
		BandTopPen[i] = INVALID_PEN;
		BandSidePen[i] = INVALID_PEN;
		BandFloorPen[i] = INVALID_PEN;
	}

	PensReady = FALSE;
}


/***************************************************************************/
/* Preferred fonts                                                          */
/***************************************************************************/

static void OpenPreferredFonts(void)
{
	MainFont = NULL;
	TitleFont = NULL;
	InfoFont = NULL;

	/* Final v1.0 font order:
	   Tahoma > Verdana > TrebuchetMS > Arial.
	   For these proportional fonts:
	     line 1 = size 18
	     line 2+ = size 16
	
	   If none of them exists:
	     NewTopaz 8/8/8
	   If NewTopaz is not installed:
	     TOPAZ/topaz.font 8/8/8
	*/

	if(OpenFontSet("Tahoma.font",     18, 16, 16)) return;
	if(OpenFontSet("Verdana.font",    18, 16, 16)) return;
	if(OpenFontSet("TrebuchetMS.font",18, 16, 16)) return;
	if(OpenFontSet("Arial.font",      18, 16, 16)) return;

	if(OpenFontSet("NewTopaz.font",    8,  8,  8)) return;
	if(OpenFontSet("topaz.font",       8,  8,  8)) return;
}

static void ClosePreferredFonts(void)
{
	if(InfoFont && InfoFont != TitleFont && InfoFont != MainFont) {
		CloseFont(InfoFont);
	}
	InfoFont = NULL;

	if(TitleFont && TitleFont != MainFont) {
		CloseFont(TitleFont);
	}
	TitleFont = NULL;

	if(MainFont) {
		CloseFont(MainFont);
	}
	MainFont = NULL;
}

static WORD OpenFontSet(const char *name, UWORD mainSize, UWORD titleSize, UWORD infoSize)
{
	MainFont  = TryOpenPreferredFont(name, mainSize);
	TitleFont = TryOpenPreferredFont(name, titleSize);
	InfoFont  = TryOpenPreferredFont(name, infoSize);

	if(MainFont && TitleFont && InfoFont) return(TRUE);

	ClosePreferredFonts();
	return(FALSE);
}

static struct TextFont *TryOpenPreferredFont(const char *name, UWORD size)
{
	struct TextAttr ta;
	struct TextFont *font;

	if(!name || size == 0) return(NULL);

	ta.ta_Name  = (STRPTR)name;
	ta.ta_YSize = size;
	ta.ta_Style = FS_NORMAL;
	ta.ta_Flags = FPF_DISKFONT;

	font = NULL;
#ifndef __SASC
	if(DiskfontBase) font = OpenDiskFont(&ta);
#else
	font = OpenDiskFont(&ta);
#endif
	if(font) return(font);

	/* TOPAZ is normally a ROM font and should exist on every Amiga. */
	ta.ta_Flags = FPF_ROMFONT;
	font = OpenFont(&ta);
	return(font);
}


/***************************************************************************/
/* Main plugin loop                                                         */
/***************************************************************************/

void PluginLoop(void)
{
	ULONG signals;
	ULONG waitMask;
	WORD done;
	struct IntuiMessage *imsg;
	ULONG imsgClass;
	UWORD imsgCode;

	done = FALSE;
	FramesSinceAudio = 64;

	/* After registration AmigaAMP may already have filled tinfo. Render the
	   current title/artist immediately instead of waiting for the next info
	   signal. */
	UpdateTrackInfo();
	RenderFrame();
	StartFrameTimer();

	while(!done) {
		waitMask = SIGBREAKF_CTRL_C | PluginMask | InfoMask | WinMask;
		if(TimerOpen) waitMask |= TimerMask;

		signals = Wait(waitMask);

		if(signals & SIGBREAKF_CTRL_C) done = TRUE;

		if((signals & WinMask) && PluginWin) {
			while((imsg = (struct IntuiMessage *)GetMsg(PluginWin->UserPort))) {
				imsgClass = imsg->Class;
				imsgCode  = imsg->Code;
				ReplyMsg((struct Message *)imsg);

				if(imsgClass == IDCMP_RAWKEY) {
					if(((imsgCode & 0x80) == 0) && ((imsgCode & 0x7f) == ESC_RAWKEY)) done = TRUE;
				}
			}
		}

		if(signals & InfoMask) {
			UpdateTrackInfo();
		}

		if(signals & PluginMask) {
			/* v1.0: update immediately on AmigaAMP audio signal. */
			UpdateRealtimeLevels();
			PushHistoryFromLevels();
			RenderFrame();
		}

		if(TimerOpen && (signals & TimerMask)) {
			if(FinishFrameTimer()) {
				StartFrameTimer();

				/* v1.0: keep polling at 60 FPS for lower visible latency.
				   If AmigaAMP does not provide SampleRaw, the old spectrum path
				   remains the fallback. */
				UpdateRealtimeLevels();
				PushHistoryFromLevels();
				RenderFrame();
			}
		}
	}

	StopFrameTimer();
}

/***************************************************************************/
/* Timer                                                                    */
/***************************************************************************/

static WORD OpenFrameTimer(void)
{
	TimerMP = CreateMsgPort();
	if(!TimerMP) return(FALSE);

	TimerReq = (struct timerequest *)CreateIORequest(TimerMP, sizeof(struct timerequest));
	if(!TimerReq) {
		DeleteMsgPort(TimerMP);
		TimerMP = NULL;
		return(FALSE);
	}

	if(OpenDevice(TIMERNAME, UNIT_MICROHZ, (struct IORequest *)TimerReq, 0) != 0) {
		DeleteIORequest((struct IORequest *)TimerReq);
		TimerReq = NULL;
		DeleteMsgPort(TimerMP);
		TimerMP = NULL;
		return(FALSE);
	}

	TimerOpen = TRUE;
	TimerPending = FALSE;
	TimerMask = 1L << TimerMP->mp_SigBit;
	return(TRUE);
}

static void CloseFrameTimer(void)
{
	if(TimerOpen && TimerReq) CloseDevice((struct IORequest *)TimerReq);
	TimerOpen = FALSE;

	if(TimerReq) {
		DeleteIORequest((struct IORequest *)TimerReq);
		TimerReq = NULL;
	}
	if(TimerMP) {
		DeleteMsgPort(TimerMP);
		TimerMP = NULL;
	}
	TimerMask = 0;
}

static void StartFrameTimer(void)
{
	if(!TimerOpen || !TimerReq || TimerPending) return;

	TimerReq->tr_node.io_Command = TR_ADDREQUEST;
	TimerReq->tr_time.tv_secs = 0;
	TimerReq->tr_time.tv_micro = FRAME_USEC;
	SendIO((struct IORequest *)TimerReq);
	TimerPending = TRUE;
}

static void StopFrameTimer(void)
{
	if(TimerOpen && TimerReq && TimerPending) {
		if(!CheckIO((struct IORequest *)TimerReq)) AbortIO((struct IORequest *)TimerReq);
		WaitIO((struct IORequest *)TimerReq);
		TimerPending = FALSE;
	}
}

static WORD FinishFrameTimer(void)
{
	if(!TimerOpen || !TimerReq || !TimerPending) return(FALSE);
	if(!CheckIO((struct IORequest *)TimerReq)) return(FALSE);
	WaitIO((struct IORequest *)TimerReq);
	TimerPending = FALSE;
	return(TRUE);
}

/***************************************************************************/
/* Audio level processing                                                   */
/***************************************************************************/

static void BuildSpectrumBands(void)
{
	LONG b;
	LONG start;
	LONG end;
	LONG denom;

	denom = NUM_BANDS * NUM_BANDS;
	for(b = 0; b < NUM_BANDS; b++) {
		start = ((b * b) * 512L) / denom;
		end = (((b + 1) * (b + 1)) * 512L) / denom;
		if(b == 0) start = 0;
		if(end <= start) end = start + 1;
		if(end > 512) end = 512;
		BandStart[b] = start;
		BandEnd[b] = end;
	}
}

static void UpdateRealtimeLevels(void)
{
	/* Raw samples give a more immediate visual response than the spectrum
	   buffer. Keep spectrum as fallback for AmigaAMP setups without SampleRaw. */
	if(SampleRaw) {
		UpdateLevelsFromSamples();
		FramesSinceAudio = 0;
	}
	else if(SpecRawL && SpecRawR) {
		UpdateLevelsFromSpectrum();
		FramesSinceAudio = 0;
	}
	else {
		if(FramesSinceAudio > 2) DecayLevels();
		else FramesSinceAudio++;
	}
}

static void UpdateLevelsFromSpectrum(void)
{
	LONG b;
	LONG i;
	LONG start;
	LONG end;
	LONG count;
	LONG sum;
	LONG maxv;
	LONG v;
	LONG avg;
	LONG target;

	for(b = 0; b < NUM_BANDS; b++) {
		start = BandStart[b];
		end   = BandEnd[b];
		count = MaxLong(1, end - start);
		sum = 0;
		maxv = 0;

		for(i = start; i < end; i++) {
			v = (((LONG)SpecRawL[i] + (LONG)SpecRawR[i]) >> 1);
			sum += v;
			if(v > maxv) maxv = v;
		}

		avg = sum / count;
		target = (((avg >> 8) * 3) + (maxv >> 8)) >> 2;
		target = (target * 115) / 100;
		target = ClampLong(target, 0, 255);

		SmoothLevel(b, target);
	}
}

static void UpdateLevelsFromSamples(void)
{
	LONG b;
	LONG i;
	LONG start;
	LONG end;
	LONG base;
	LONG span;
	LONG sampleL;
	LONG sampleR;
	LONG sample;
	LONG sum;
	LONG peak;
	LONG count;
	LONG avg;
	LONG avgTarget;
	LONG peakTarget;
	LONG target;

	/* 1.0: clean latency trim.
	   Keep the flicker-free backbuffer path, but analyse the newest half of
	   SampleRaw instead of spreading the bars over the whole 512-sample block.
	   This avoids showing older samples as the current visual peak. */
	base = 256;
	span = 256;

	for(b = 0; b < NUM_BANDS; b++) {
		start = base + ((b * span) / NUM_BANDS);
		end = base + (((b + 1) * span) / NUM_BANDS);
		sum = 0;
		peak = 0;
		count = 0;

		for(i = start; i < end; i++) {
			sampleL = (LONG)SampleRaw[i << 1];
			sampleR = (LONG)SampleRaw[(i << 1) + 1];
			sample = AbsLong((sampleL + sampleR) >> 1);
			sum += sample;
			if(sample > peak) peak = sample;
			count++;
		}

		avg = sum / MaxLong(1, count);

		/* Average gives stable level, a small capped peak component makes short
		   transients appear immediately without returning to the v0.6 Dauerrot. */
		avgTarget = (avg * 115L) / 32768L;
		avgTarget = (avgTarget * 255L) / 100L;

		peakTarget = (peak * 255L) / 32768L;
		peakTarget = peakTarget >> 3;
		if(peakTarget > 14) peakTarget = 14;

		target = avgTarget + peakTarget;

		/* 1.0: soft top limiter.
		   Keep strong peaks visible, but prevent every normal impulse from
		   slamming all bars into the red zone. */
		if(target > 205) target = 205 + ((target - 205) >> 2);

		target = ClampLong(target, 0, 255);

		SmoothLevel(b, target);
	}
}

static void DecayLevels(void)
{
	LONG i;

	for(i = 0; i < NUM_BANDS; i++) {
		SmoothLevel(i, 0);
	}
}

static void SmoothLevel(LONG i, LONG target)
{
	LONG current;
	LONG diff;
	LONG vel;
	LONG next;

	current = VuLevel[i];

	/* 1.0: fast sync on attack, spring-damped release.
	   Rising levels stay immediate, so hits line up with the music.
	   Falling levels use a small velocity buffer, so the bars feel like they
	   cushion/spring back instead of dropping mechanically. */
	if(target >= current) {
		current = target;
		VuVelocity[i] = 0;
	}
	else {
		diff = current - target;
		vel = VuVelocity[i];

		/* Add downward pull, then damp it. Fixed-point-ish but cheap. */
		vel += MaxLong(2, diff >> 3);
		vel = (vel * 11) >> 4;

		if(vel < 1) vel = 1;
		if(vel > 18) vel = 18;

		next = current - vel;

		/* Cushion near the target: do not slam through it. */
		if(next < target) {
			next = target + ((current - target) >> 3);
			if(next > current) next = target;
			vel = vel >> 1;
		}

		current = next;
		VuVelocity[i] = vel;
	}

	current = ClampLong(current, 0, 255);
	VuLevel[i] = current;

	if(current > VuPeak[i]) {
		VuPeak[i] = current;
		PeakHold[i] = 7;
	}
	else {
		if(PeakHold[i] > 0) PeakHold[i]--;
		else if(VuPeak[i] > 0) VuPeak[i] -= 7;
		if(VuPeak[i] < current) VuPeak[i] = current;
	}
}

/***************************************************************************/
/* Track info                                                               */
/***************************************************************************/

static void UpdateTrackInfo(void)
{
	if(!tinfo) return;

	if(tinfo->ID3title && SafeLen(tinfo->ID3title) > 0) {
		SafeCopy(TitleLine, tinfo->ID3title, TITLE_MAX);
		HaveTrackInfo = TRUE;
	}
	else if(tinfo->TrackInfoText && SafeLen(tinfo->TrackInfoText) > 0) {
		SafeCopy(TitleLine, tinfo->TrackInfoText, TITLE_MAX);
		HaveTrackInfo = TRUE;
	}
	else {
		SafeCopy(TitleLine, "AmigaAMP", TITLE_MAX);
		HaveTrackInfo = FALSE;
	}

	if(tinfo->ID3artist && SafeLen(tinfo->ID3artist) > 0) {
		SafeCopy(ArtistLine, tinfo->ID3artist, INFO_MAX);
	}
	else {
		SafeCopy(ArtistLine, "", INFO_MAX);
	}
}

static void PushHistoryFromLevels(void)
{
	LONG z;
	LONG x;

	for(z = HISTORY_DEPTH - 1; z > 0; z--) {
		for(x = 0; x < NUM_BANDS; x++) {
			SpectrumHistory[z][x] = SpectrumHistory[z - 1][x];
		}
	}
	for(x = 0; x < NUM_BANDS; x++) {
		SpectrumHistory[0][x] = VuLevel[x];
	}
}

/***************************************************************************/
/* Rendering                                                                */
/***************************************************************************/

static void RenderFrame(void)
{
	if(!DrawRP || !PluginWin) return;

	FrameCounter++;
	if(!LayoutReady || LastLayoutW != WinW || LastLayoutH != WinH) {
		CalculateIsoLayout();
	}

	SetDrMd(DrawRP, JAM1);
	SetAPen(DrawRP, BgPen);
	RectFill(DrawRP, 0, 0, WinW - 1, WinH - 1);

	DrawIsometricSpectrum();
	DrawTrackOverlay();
	CopyBackBufferToWindow();
}

static void CalculateIsoLayout(void)
{
	LONG availableW;
	LONG availableH;
	LONG tileFromW;
	LONG tileFromH;
	LONG fieldH;
	LONG visualCenterY;
	LONG x;
	LONG z;

	/* v0.6:
	   - more air between the individual towers
	   - slightly more top-down view
	
	   The grid pitch stays regular, but the visible tile footprint is reduced
	   further. For the higher viewpoint the vertical base step (IsoDY) is a
	   bit flatter than in v0.5, and the tower height is toned down slightly,
	   so the top faces become more visible. */
	availableW = WinW - MaxLong(28, WinW / 14);
	availableH = WinH - MaxLong(76, WinH / 5);

	if(availableW < 160) availableW = WinW - 16;
	if(availableH < 120) availableH = WinH - 48;

	tileFromW = availableW / MaxLong(1, (NUM_BANDS + HISTORY_DEPTH + 1));
	tileFromH = (availableH / MaxLong(1, (NUM_BANDS + HISTORY_DEPTH + 1))) << 1;

	IsoDX = MinLong(tileFromW, tileFromH);
	IsoDX = ClampLong(IsoDX, 7, 30);

	/* Flatter depth step than strict 2:1 so the camera feels a bit more
	   elevated / top-down. */
	IsoDY = MaxLong(3, (IsoDX * 42) / 100);

	/* More spacing between bars: smaller visible footprint per grid cell. */
	IsoTileHalfW = MaxLong(3, (IsoDX * 58) / 100);
	IsoTileHalfH = MaxLong(2, (IsoDY * 58) / 100);

	/* Slightly lower towers improve the more top-down look. */
	IsoHeightScale = MaxLong(32, WinH / 4);

	CenterX = WinW >> 1;
	visualCenterY = (WinH * 57) / 100;
	fieldH = (NUM_BANDS + HISTORY_DEPTH - 2) * IsoDY;

	IsoOriginX = CenterX;
	IsoOriginY = visualCenterY + (fieldH >> 1);

	if(IsoOriginY > WinH - IsoTileHalfH - 6) {
		IsoOriginY = WinH - IsoTileHalfH - 6;
	}

	/* v1.0: move the whole tower field about two isometric rows lower. */
	IsoOriginY += (IsoDY * 2);

	for(z = 0; z < HISTORY_DEPTH; z++) {
		for(x = 0; x < NUM_BANDS; x++) {
			IsoCellX[z][x] = IsoOriginX + ((x - z) * IsoDX);
			IsoCellY[z][x] = IsoOriginY - ((x + z) * IsoDY);
		}
	}

	LastLayoutW = WinW;
	LastLayoutH = WinH;
	LayoutReady = TRUE;
}

static void DrawIsometricSpectrum(void)
{
	LONG diag;
	LONG z;
	LONG x;
	LONG cx;
	LONG cy;
	LONG h;
	LONG raw;

	SetDrMd(DrawRP, JAM1);

	/* Draw from back to front. Coordinates are precomputed in
	   CalculateIsoLayout(), so the render loop spends less time multiplying
	   and more time just drawing. */
	for(diag = (NUM_BANDS - 1) + (HISTORY_DEPTH - 1); diag >= 0; diag--) {
		for(z = HISTORY_DEPTH - 1; z >= 0; z--) {
			x = diag - z;
			if(x < 0 || x >= NUM_BANDS) continue;

			cx = IsoCellX[z][x];
			cy = IsoCellY[z][x];

			/* Cheap coarse clipping before doing AreaFill polygons. */
			if(cx < -IsoDX || cx > WinW + IsoDX) continue;
			if(cy < -IsoHeightScale - IsoDY || cy > WinH + IsoDY) continue;

			DrawIsoTile(cx, cy, 0, (BandFloorPen[x] != INVALID_PEN ? BandFloorPen[x] : FloorDimPen), FloorDimPen, FloorDimPen);

			raw = SpectrumHistory[z][x];
			if(raw <= 3) continue;

			h = (raw * IsoHeightScale) / 255;
			if(h < (IsoTileHalfH + 1)) h = IsoTileHalfH + 1;

			DrawIsoTile(cx, cy, h,
				(BandTopPen[x]  != INVALID_PEN ? BandTopPen[x]  : TextPen),
				(BandPen[x]     != INVALID_PEN ? BandPen[x]     : TextPen),
				(BandSidePen[x] != INVALID_PEN ? BandSidePen[x] : FloorDimPen));
		}
	}
}

static void FillQuad(LONG x1, LONG y1, LONG x2, LONG y2, LONG x3, LONG y3, LONG x4, LONG y4, ULONG pen)
{
	if(!DrawRP) return;
	SetAPen(DrawRP, pen);
	AreaMove(DrawRP, x1, y1);
	AreaDraw(DrawRP, x2, y2);
	AreaDraw(DrawRP, x3, y3);
	AreaDraw(DrawRP, x4, y4);
	AreaEnd(DrawRP);
}

static void DrawIsoTile(LONG cx, LONG cy, LONG height, ULONG topPen, ULONG leftPen, ULONG rightPen)
{
	LONG w;
	LONG h;
	LONG topY;

	LONG tX, tY;
	LONG rX, rY;
	LONG bX, bY;
	LONG lX, lY;

	LONG brX, brY;
	LONG bbX, bbY;
	LONG blX, blY;

	w = IsoTileHalfW;
	h = IsoTileHalfH;
	topY = cy - height;

	/* Top diamond */
	tX = cx;     tY = topY - h;
	rX = cx + w; rY = topY;
	bX = cx;     bY = topY + h;
	lX = cx - w; lY = topY;

	/* Bottom diamond points for extrusion */
	brX = cx + w; brY = cy;
	bbX = cx;     bbY = cy + h;
	blX = cx - w; blY = cy;

	if(height > 0) {
		/* Left/front face and right/front face. */
		FillQuad(lX, lY, bX, bY, bbX, bbY, blX, blY, leftPen);
		FillQuad(bX, bY, rX, rY, brX, brY, bbX, bbY, rightPen);
	}

	FillQuad(tX, tY, rX, rY, bX, bY, lX, lY, topPen);
}

static ULONG PenBandTop(LONG band)
{
	band = ClampLong(band, 0, NUM_BANDS - 1);
	if(BandTopPen[band] != INVALID_PEN) return(BandTopPen[band]);
	return(TextPen);
}

static ULONG PenBandLeft(LONG band)
{
	band = ClampLong(band, 0, NUM_BANDS - 1);
	if(BandPen[band] != INVALID_PEN) return(BandPen[band]);
	return(TextPen);
}

static ULONG PenBandRight(LONG band)
{
	band = ClampLong(band, 0, NUM_BANDS - 1);
	if(BandSidePen[band] != INVALID_PEN) return(BandSidePen[band]);
	return(FloorDimPen);
}

static ULONG PenBandFloor(LONG band)
{
	band = ClampLong(band, 0, NUM_BANDS - 1);
	if(BandFloorPen[band] != INVALID_PEN) return(BandFloorPen[band]);
	return(FloorDimPen);
}

static UBYTE ClampByte(LONG v)
{
	if(v < 0) return(0);
	if(v > 255) return(255);
	return((UBYTE)v);
}

static ULONG Color32(UBYTE v)
{
	return(((ULONG)v << 24) | ((ULONG)v << 16) | ((ULONG)v << 8) | (ULONG)v);
}

static void BuildBandRGB(LONG band, LONG *r, LONG *g, LONG *b)
{
	LONG p;
	LONG seg;
	LONG t;
	LONG r1, g1, b1;
	LONG r2, g2, b2;

	/* Six colour stops across the 16 columns:
	   blue -> cyan -> green -> yellow -> orange -> red.
	   This gives every spectrum column its own visibly stepped colour. */
	static const LONG stopR[6] = {  0,   0,   0, 230, 255, 255 };
	static const LONG stopG[6] = { 70, 210, 255, 255, 140,  20 };
	static const LONG stopB[6] = {255, 255,  20,   0,   0,   0 };

	p = (band * 500) / MaxLong(1, NUM_BANDS - 1);
	seg = p / 100;
	if(seg > 4) seg = 4;
	t = p - (seg * 100);

	r1 = stopR[seg];     g1 = stopG[seg];     b1 = stopB[seg];
	r2 = stopR[seg + 1]; g2 = stopG[seg + 1]; b2 = stopB[seg + 1];

	*r = r1 + ((r2 - r1) * t) / 100;
	*g = g1 + ((g2 - g1) * t) / 100;
	*b = b1 + ((b2 - b1) * t) / 100;
}

static void DrawTrackOverlay(void)
{
	LONG y;
	LONG mainH;
	LONG titleH;
	LONG infoH;
	LONG gap;

	if(!DrawRP) return;

	mainH  = MainFont  ? MainFont->tf_YSize  : DrawRP->TxHeight;
	titleH = TitleFont ? TitleFont->tf_YSize : DrawRP->TxHeight;
	infoH  = InfoFont  ? InfoFont->tf_YSize  : DrawRP->TxHeight;
	gap = infoH + 2;

	/* v1.0:
	   Keep the exact text content from v1.0:
	   - first visible line = TitleLine (ID3title or TrackInfoText)
	   - next line = ArtistLine
	   Only the first visible line now uses MainFont, so Tahoma/Verdana/
	   TrebuchetMS/Arial render it in size 18. */
	y = MaxLong(mainH + 2, WinH / 36);

	/* v1.0: move the complete two-line text block one text line lower. */
	y += mainH + 2;

	if(SafeLen(TitleLine) > 0) {
		DrawCenteredText(TitleLine, y, TextPen, MainFont);
		if(SafeLen(ArtistLine) > 0) DrawCenteredText(ArtistLine, y + mainH + 4, SubTextPen, TitleFont);
		if(SafeLen(AlbumLine) > 0) DrawCenteredText(AlbumLine, y + mainH + 4 + gap, SubTextPen, InfoFont);
	}
	else {
		if(SafeLen(ArtistLine) > 0) DrawCenteredText(ArtistLine, y, TextPen, MainFont);
		if(SafeLen(AlbumLine) > 0) DrawCenteredText(AlbumLine, y + mainH + 4, SubTextPen, TitleFont);
	}
}

static void DrawCenteredText(char *text, LONG y, ULONG pen, struct TextFont *font)
{
	DrawCenteredTextAt(text, CenterX, y, pen, font);
}

static void DrawCenteredTextAt(char *text, LONG cx, LONG y, ULONG pen, struct TextFont *font)
{
	LONG len;
	LONG tw;
	LONG x;

	if(!text || !DrawRP) return;
	len = SafeLen(text);
	if(len <= 0) return;

	if(font) SetFont(DrawRP, font);
	else if(PluginWin && PluginWin->RPort && PluginWin->RPort->Font) SetFont(DrawRP, PluginWin->RPort->Font);

	tw = TextLength(DrawRP, text, len);
	x = cx - (tw >> 1);
	if(x < 4) x = 4;
	if(x + tw > WinW - 4) x = WinW - tw - 4;

	SetDrMd(DrawRP, JAM1);
	SetAPen(DrawRP, pen);
	Move(DrawRP, x, y);
	Text(DrawRP, text, len);
}

/***************************************************************************/
/* Helpers                                                                  */
/***************************************************************************/

void ShowRequester(char *Text, char *Button)
{
	struct EasyStruct es;

	es.es_StructSize   = sizeof(struct EasyStruct);
	es.es_Flags        = 0;
	es.es_Title        = "AmigaAMP-Isometric";
	es.es_TextFormat   = Text;
	es.es_GadgetFormat = Button;

	EasyRequestArgs(NULL, &es, NULL, NULL);
}

static void SafeCopy(char *dst, const char *src, LONG dstSize)
{
	LONG i;

	if(!dst || dstSize <= 0) return;
	if(!src) src = "";

	for(i = 0; i < dstSize - 1 && src[i]; i++) dst[i] = src[i];
	dst[i] = 0;
}

static LONG SafeLen(const char *s)
{
	LONG i;

	if(!s) return(0);
	for(i = 0; s[i]; i++) {
		if(i > 4096) break;
	}
	return(i);
}

static LONG AbsLong(LONG v)
{
	if(v < 0) return(-v);
	return(v);
}

static LONG ClampLong(LONG v, LONG lo, LONG hi)
{
	if(v < lo) return(lo);
	if(v > hi) return(hi);
	return(v);
}

static LONG MinLong(LONG a, LONG b)
{
	if(a < b) return(a);
	return(b);
}

static LONG MaxLong(LONG a, LONG b)
{
	if(a > b) return(a);
	return(b);
}
