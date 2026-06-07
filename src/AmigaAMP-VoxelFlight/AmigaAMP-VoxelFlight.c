/*****************************************************************************/
/*                                                                           */
/*                  AmigaAMP-VoxelFlight  v1.0                                    */
/*                                                                           */
/*  P96/Workbench fullscreen VU meter / spectrum bars for AmigaAMP.         */
/*  Draws a clean VU bar with green/yellow/orange/red blocks and       */
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

#define NUM_BANDS              32
#define MAX_GRID_ROWS          44
#define MIN_GRID_ROWS          20
#define FRAME_USEC             16666UL
#define TITLE_MAX              256
#define INFO_MAX               256

const char VersionString[] = "$VER: AmigaAMP-VoxelFlight 1.0 AmigaAmp VoxelFlight by Andreas 'Andiweli' Stuermer (07.06.2026)";

char BinaryVisibleCreditString[] = "AmigaAmp VoxelFlight by Andreas 'Andiweli' Stuermer";

/* AmigaAmp VoxelFlight by Andreas 'Andiweli' Stuermer */
/* Real compiled data string: visible in the final binary with a hex/text editor. */
char BinaryCreditString[] = "AmigaAmp VoxelFlight by Andreas 'Andiweli' Stuermer";

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
static void RenderFrame(void);
static void CalculateGridLayout(void);
static void DrawSpectrumGrid(void);
static void DrawTrackOverlay(void);
static void DrawCenteredText(char *text, LONG y, ULONG pen, struct TextFont *font);
static void DrawCenteredTextAt(char *text, LONG cx, LONG y, ULONG pen, struct TextFont *font);
static ULONG PenForGridRow(LONG row);
static LONG TerrainHeight(LONG x, LONG z);
static LONG TriWave(LONG v, LONG period);
static void SafeCopy(char *dst, const char *src, LONG dstSize);
static LONG SafeLen(const char *s);
static LONG AbsLong(LONG v);
static LONG ClampLong(LONG v, LONG lo, LONG hi);
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

ULONG BgPen      = INVALID_PEN;
ULONG BluePen    = INVALID_PEN;
ULONG CyanPen    = INVALID_PEN;
ULONG PurplePen  = INVALID_PEN;
ULONG GreenPen   = INVALID_PEN;
ULONG GreenPen2  = INVALID_PEN;
ULONG YellowPen  = INVALID_PEN;
ULONG OrangePen  = INVALID_PEN;
ULONG RedPen     = INVALID_PEN;
ULONG PeakPen    = INVALID_PEN;
ULONG TextPen    = INVALID_PEN;
ULONG SubTextPen = INVALID_PEN;
WORD  PensReady  = FALSE;

LONG VuLevel[NUM_BANDS];
LONG LiveLevel[NUM_BANDS];
LONG VuPeak[NUM_BANDS];
LONG VuVelocity[NUM_BANDS];
LONG PeakHold[NUM_BANDS];
LONG BandStart[NUM_BANDS];
LONG BandEnd[NUM_BANDS];
LONG FrameCounter = 0;

LONG GridLeft = 0;
LONG GridTop = 0;
LONG GridBottom = 0;
LONG CellW = 0;
LONG CellH = 0;
LONG CellGapX = 0;
LONG CellGapY = 0;
LONG CellPitchX = 0;
LONG CellPitchY = 0;
LONG GridRows = 0;

char TitleLine[TITLE_MAX];
char InfoLine[INFO_MAX];
WORD HaveTrackInfo = FALSE;

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

	SafeCopy(TitleLine, "AmigaAMP", TITLE_MAX);
	SafeCopy(InfoLine, "", INFO_MAX);
	HaveTrackInfo = FALSE;

	for(i = 0; i < NUM_BANDS; i++) {
		VuLevel[i] = 0;
		LiveLevel[i] = 0;
		VuPeak[i] = 0;
		VuVelocity[i] = 0;
		PeakHold[i] = 0;
		BandStart[i] = 0;
		BandEnd[i] = 1;
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

	PluginWin = OpenWindowTags(NULL,
		WA_CustomScreen,  (ULONG)PluginScreen,
		WA_Left,          0,
		WA_Top,           0,
		WA_Width,         WinW,
		WA_Height,        WinH,
		WA_Title,         (ULONG)"AmigaAMP-VoxelFlight",
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

	if(!PluginScreen) return;
	cm = PluginScreen->ViewPort.ColorMap;
	if(!cm) return;

	/* v0.3 Mars palette:
	   dark sky/background, readable brown/rust/sand voxel hills.
	   No blue/red horizon stripes. */
	BgPen      = ObtainBestPen(cm, 0x06060000, 0x04040000, 0x03030000, TAG_END);
	BluePen    = ObtainBestPen(cm, 0x20100000, 0x10080000, 0x08040000, TAG_END);
	CyanPen    = ObtainBestPen(cm, 0x34200000, 0x180c0000, 0x08040000, TAG_END);
	PurplePen  = ObtainBestPen(cm, 0x48300000, 0x20100000, 0x0c060000, TAG_END);
	GreenPen   = ObtainBestPen(cm, 0x50300000, 0x24120000, 0x08040000, TAG_END);
	GreenPen2  = ObtainBestPen(cm, 0x70440000, 0x2f180000, 0x0c060000, TAG_END);
	YellowPen  = ObtainBestPen(cm, 0x98600000, 0x44240000, 0x10080000, TAG_END);
	OrangePen  = ObtainBestPen(cm, 0xb8800000, 0x60380000, 0x18100000, TAG_END);
	RedPen     = ObtainBestPen(cm, 0xd0a00000, 0x80500000, 0x30200000, TAG_END);
	PeakPen    = ObtainBestPen(cm, 0xf0c00000, 0xb0800000, 0x60400000, TAG_END);
	TextPen    = ObtainBestPen(cm, 0xffffffff, 0xffffffff, 0xffffffff, TAG_END);
	SubTextPen = ObtainBestPen(cm, 0xd8d8d8d8, 0xd8d8d8d8, 0xd8d8d8d8, TAG_END);

	PensReady = TRUE;
}

static void ReleasePens(void)
{
	struct ColorMap *cm;

	if(!PluginScreen || !PensReady) return;
	cm = PluginScreen->ViewPort.ColorMap;
	if(!cm) return;

	if(BgPen      != INVALID_PEN) ReleasePen(cm, BgPen);
	if(BluePen    != INVALID_PEN) ReleasePen(cm, BluePen);
	if(CyanPen    != INVALID_PEN) ReleasePen(cm, CyanPen);
	if(PurplePen  != INVALID_PEN) ReleasePen(cm, PurplePen);
	if(GreenPen   != INVALID_PEN) ReleasePen(cm, GreenPen);
	if(GreenPen2  != INVALID_PEN) ReleasePen(cm, GreenPen2);
	if(YellowPen  != INVALID_PEN) ReleasePen(cm, YellowPen);
	if(OrangePen  != INVALID_PEN) ReleasePen(cm, OrangePen);
	if(RedPen     != INVALID_PEN) ReleasePen(cm, RedPen);
	if(PeakPen    != INVALID_PEN) ReleasePen(cm, PeakPen);
	if(TextPen    != INVALID_PEN) ReleasePen(cm, TextPen);
	if(SubTextPen != INVALID_PEN) ReleasePen(cm, SubTextPen);

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

	/* Same final font order as AmigaAMP-Isometric:
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
			/* v0.6: for VU metering prefer raw samples over AmigaAMP's
			   spectrum data. The spectrum buffer can be windowed/averaged and
			   therefore feels late. */
			UpdateRealtimeLevels();
			RenderFrame();
		}

		if(TimerOpen && (signals & TimerMask)) {
			if(FinishFrameTimer()) {
				StartFrameTimer();

				/* v0.7: poll the current raw sample buffer at 60 FPS.
				   This keeps the display responsive without the nervous 10 ms
				   overdrive from v0.6. */
				UpdateRealtimeLevels();
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
	/* Raw samples are the fastest data path for a VU meter.
	   Only fall back to spectrum data if no sample buffer is available. */
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
	LONG currentLive;
	LONG diff;
	LONG step;

	/* v0.7:
	   Level processing now follows the stable VUMeter/Isometric method:
	   use the newest half of SampleRaw, average level plus a small capped
	   transient component, then soft-limit the top. This avoids the v0.6
	   problem where the newest 1/8 buffer sometimes read too low and made the
	   terrain peaks drop in the middle of music. */
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

		/* Same principle as the finished plugins, with a little VoxelFlight gain. */
		avgTarget = (avg * 150L) / 32768L;
		avgTarget = (avgTarget * 255L) / 100L;

		peakTarget = (peak * 255L) / 32768L;
		peakTarget = peakTarget >> 3;
		if(peakTarget > 16) peakTarget = 16;

		target = avgTarget + peakTarget;
		if(target > 206) target = 206 + ((target - 206) >> 2);
		target = ClampLong(target, 0, 255);

		/* LiveLevel is what the bright/elevated hills use.
		   Rise immediately, but fall slowly so a short low buffer cannot pull all
		   terrain peaks down at once. */
		currentLive = LiveLevel[b];
		if(target >= currentLive) {
			currentLive = target;
		}
		else {
			diff = currentLive - target;
			step = diff >> 4;
			if(step < 1) step = 1;
			if(step > 5) step = 5;
			currentLive -= step;
			if(currentLive < target) currentLive = target;
		}
		LiveLevel[b] = ClampLong(currentLive, 0, 255);

		SmoothLevel(b, target);
	}
}

static void DecayLevels(void)
{
	LONG i;

	for(i = 0; i < NUM_BANDS; i++) {
		SmoothLevel(i, 0);

		if(LiveLevel[i] > 0) {
			LiveLevel[i] -= 4;
			if(LiveLevel[i] < 0) LiveLevel[i] = 0;
		}
	}
}

static void SmoothLevel(LONG i, LONG target)
{
	LONG current;
	LONG diff;
	LONG vel;
	LONG next;

	current = VuLevel[i];

	/* v0.7:
	   Same stable level smoothing style as the finished VUMeter/Isometric:
	   instant attack, spring-damped release. */
	if(target >= current) {
		current = target;
		VuVelocity[i] = 0;
	}
	else {
		diff = current - target;
		vel = VuVelocity[i];

		vel += MaxLong(2, diff >> 3);
		vel = (vel * 11) >> 4;

		if(vel < 1) vel = 1;
		if(vel > 18) vel = 18;

		next = current - vel;

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

	/* Same text content as AmigaAMP-Isometric:
	   line 1 = ID3title, fallback TrackInfoText, fallback AmigaAMP
	   line 2 = ID3artist */
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
		SafeCopy(InfoLine, tinfo->ID3artist, INFO_MAX);
	}
	else {
		SafeCopy(InfoLine, "", INFO_MAX);
	}
}

/***************************************************************************/
/* Rendering                                                                */
/***************************************************************************/

static void RenderFrame(void)
{
	if(!DrawRP || !PluginWin) return;

	FrameCounter++;
	CalculateGridLayout();

	SetDrMd(DrawRP, JAM1);
	SetAPen(DrawRP, BgPen);
	RectFill(DrawRP, 0, 0, WinW - 1, WinH - 1);

	DrawSpectrumGrid();
	DrawTrackOverlay();
	CopyBackBufferToWindow();
}

static void CalculateGridLayout(void)
{
	LONG topMargin;
	LONG bottomMargin;

	/* v0.4:
	   More empty/dark space and less filled terrain. This makes individual
	   voxel hills readable instead of one large yellow/brown wall. */
	topMargin = MaxLong(62, (WinH * 27) / 100);
	bottomMargin = MaxLong(12, WinH / 32);

	GridLeft = 0;
	GridTop = topMargin;
	GridBottom = WinH - bottomMargin;
	GridRows = ClampLong(WinH / 13, 28, 56);

	CellPitchX = ClampLong(WinW / 46, 8, 26);
	CellW = CellPitchX;
	CellH = MaxLong(2, WinH / 150);
	CellGapX = 0;
	CellGapY = 0;
}

static void DrawSpectrumGrid(void)
{
	LONG rows;
	LONG cols;
	LONG r;
	LONG c;
	LONG t;
	LONG tNext;
	LONG yBase;
	LONG yNext;
	LONG yBaseCol;
	LONG yNextCol;
	LONG width;
	LONG x;
	LONG x2;
	LONG gap;
	LONG topY;
	LONG capY;
	LONG h;
	LONG terrain;
	LONG worldX;
	LONG worldZ;
	LONG band;
	LONG audio;
	LONG audioLift;
	LONG flight;
	LONG nearBoost;
	LONG horizon;
	LONG voxelW;
	LONG voxelH;
	LONG ridge;
	LONG movingSeed;
	LONG brightness;
	LONG side;
	LONG sideFade;
	LONG sideCurve;
	ULONG pen;
	ULONG capPen;
	ULONG sidePen;

	SetDrMd(DrawRP, JAM1);

	rows = GridRows;
	cols = ClampLong(WinW / 28, 24, 48);
	horizon = GridTop;

	/* v0.5:
	   Slightly faster forward flow so the flight direction is easier to read. */
	flight = FrameCounter * 12;

	/* Far to near. The centre stays high/detail-rich, left/right edges fade
	   and flatten downwards, giving a rounded planet-surface feel. */
	for(r = 0; r < rows; r++) {
		t = (r * 256) / MaxLong(1, rows - 1);
		tNext = ((r + 1) * 256) / MaxLong(1, rows);

		yBase = horizon + (((t * t) * (GridBottom - horizon)) >> 16);
		yNext = horizon + (((tNext * tNext) * (GridBottom - horizon)) >> 16);
		if(yNext <= yBase) yNext = yBase + 2;
		if(yNext > GridBottom) yNext = GridBottom;

		width = (WinW / 7) + ((WinW * 6 / 5) * t / 256);
		nearBoost = 42 + ((t * 150) >> 8);
		voxelW = MaxLong(4, width / cols);
		gap = MaxLong(2, voxelW / 3);

		for(c = 0; c < cols; c++) {
			x = CenterX - (width >> 1) + ((c * width) / cols);
			x2 = CenterX - (width >> 1) + (((c + 1) * width) / cols) - gap;
			if(x2 <= x) x2 = x + 1;
			if(x2 < 0 || x >= WinW) continue;
			if(x < 0) x = 0;
			if(x2 >= WinW) x2 = WinW - 1;

			/* Planet curvature / side fade.
			   sideFade is strongest in the centre and weaker near left/right.
			   At the edges terrain becomes darker, lower and gently drops down. */
			side = AbsLong((c * 2) - cols);
			sideFade = 256 - ((side * side * 185) / MaxLong(1, cols * cols));
			sideFade = ClampLong(sideFade, 54, 256);
			sideCurve = ((256 - sideFade) * (8 + (t >> 2))) >> 8;

			yBaseCol = yBase + sideCurve;
			yNextCol = yNext + (sideCurve >> 1);
			if(yBaseCol > GridBottom) yBaseCol = GridBottom;
			if(yNextCol <= yBaseCol) yNextCol = yBaseCol + 1;
			if(yNextCol > GridBottom) yNextCol = GridBottom;

			worldX = ((c - (cols >> 1)) * 14);
			worldZ = (r * 18) - flight;

			band = (c * NUM_BANDS) / MaxLong(1, cols);
			if(band < 0) band = 0;
			if(band >= NUM_BANDS) band = NUM_BANDS - 1;

			/* v0.7:
			   Use LiveLevel[] for the visible hill lift. It is fed by the stable
			   VUMeter/Isometric level method and falls slowly, so peaks no longer
			   collapse in the middle of music. */
			audio = LiveLevel[band];
			if(audio < 48) audio = 0;
			else {
				audio -= 48;
				if(audio > 145) audio = 145;
			}

			terrain = TerrainHeight(worldX, worldZ);

			ridge = TriWave(worldZ + (worldX >> 1), 180);
			if(ridge > 198) terrain += (ridge - 198) >> 1;

			movingSeed = TriWave((worldX * 3) + worldZ, 128);
			if(movingSeed < 28 && audio < 18 && terrain < 92) continue;

			audioLift = ((audio * nearBoost) >> 8);
			h = terrain + audioLift;

			/* Fade/flatten the edges into the dark background. */
			h = (h * sideFade) >> 8;
			h = ClampLong(h, 0, 210);

			voxelH = (h * (6 + (t >> 2))) / 170;
			if(voxelH < 2 && h > 40) voxelH = 2;

			topY = yBaseCol - voxelH;
			if(topY < horizon + 5) topY = horizon + 5;
			if(topY > yNextCol) topY = yNextCol;

			brightness = ((h + (t >> 2)) * sideFade) >> 8;
			pen = PenForGridRow(brightness);

			capPen = (brightness > 150) ? PeakPen : OrangePen;
			if(capPen == INVALID_PEN) capPen = pen;

			sidePen = (brightness < 120) ? BluePen : GreenPen;
			if(sidePen == INVALID_PEN) sidePen = pen;

			SetAPen(DrawRP, pen);
			RectFill(DrawRP, x, topY, x2, yNextCol);

			capY = topY + MaxLong(1, (yNextCol - topY) / 6);
			if(capY > yNextCol) capY = yNextCol;
			SetAPen(DrawRP, capPen);
			RectFill(DrawRP, x, topY, x2, capY);

			if((x2 - x) > 4) {
				SetAPen(DrawRP, sidePen);
				RectFill(DrawRP, x2 - 1, capY, x2, yNextCol);
			}
		}
	}
}

static ULONG PenForGridRow(LONG row)
{
	/* v0.4 darker Mars terrain ramp.
	   Yellow/sand only appears on true peaks, not the whole field. */
	if(row < 50) return(BluePen    != INVALID_PEN ? BluePen    : TextPen);
	if(row < 92) return(CyanPen    != INVALID_PEN ? CyanPen    : TextPen);
	if(row < 132) return(PurplePen  != INVALID_PEN ? PurplePen  : TextPen);
	if(row < 174) return(GreenPen2 != INVALID_PEN ? GreenPen2 : TextPen);
	if(row < 222) return(OrangePen != INVALID_PEN ? OrangePen : TextPen);
	return(RedPen != INVALID_PEN ? RedPen : TextPen);
}

static LONG TerrainHeight(LONG x, LONG z)
{
	LONG h;
	LONG ridge;

	/* Lower base terrain than v0.3. Individual peaks should stand out. */
	h  = 12;
	h += TriWave(x + (z >> 1), 132) >> 2;
	h += TriWave((x * 2) - z, 196) >> 3;
	h += TriWave((x >> 1) + (z * 2), 260) >> 3;

	ridge = TriWave(x - (z >> 2), 320);
	if(ridge > 205) h += (ridge - 205) >> 1;

	return(ClampLong(h, 0, 118));
}

static LONG TriWave(LONG v, LONG period)
{
	LONG p;

	if(period <= 0) return(0);

	p = v % period;
	if(p < 0) p += period;

	if(p > (period >> 1)) p = period - p;

	return((p * 255) / MaxLong(1, period >> 1));
}


static void DrawTrackOverlay(void)
{
	LONG y;
	LONG mainH;

	if(!DrawRP) return;

	mainH = MainFont ? MainFont->tf_YSize : DrawRP->TxHeight;

	/* Same visual text layout as AmigaAMP-Isometric v1.0:
	   first visible line = TitleLine, MainFont
	   second visible line = InfoLine, TitleFont
	   complete text block one text line lower. */
	y = MaxLong(mainH + 2, WinH / 36);
	y += mainH + 2;

	if(SafeLen(TitleLine) > 0) {
		DrawCenteredText(TitleLine, y, TextPen, MainFont);
		if(SafeLen(InfoLine) > 0) DrawCenteredText(InfoLine, y + mainH + 4, SubTextPen, TitleFont);
	}
	else {
		if(SafeLen(InfoLine) > 0) DrawCenteredText(InfoLine, y, TextPen, MainFont);
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
	es.es_Title        = "AmigaAMP-VoxelFlight";
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


static LONG MaxLong(LONG a, LONG b)
{
	if(a > b) return(a);
	return(b);
}
