/*****************************************************************************/
/*                                                                           */
/*                  AmigaAMP-VUMeter  v1.0                                    */
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

const char VersionString[] = "$VER: AmigaAMP-VUMeter 1.0 AmigaAmp VUMeter by Andreas 'Andiweli' Stuermer (06.06.2026)";

char BinaryVisibleCreditString[] = "AmigaAmp VUMeter by Andreas 'Andiweli' Stuermer";

/* AmigaAmp VUMeter by Andreas 'Andiweli' Stuermer */
/* Real compiled data string: visible in the final binary with a hex/text editor. */
char BinaryCreditString[] = "AmigaAmp VUMeter by Andreas 'Andiweli' Stuermer";

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
		WA_Title,         (ULONG)"AmigaAMP-VUMeter",
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

	BgPen      = ObtainBestPen(cm, 0x00000000, 0x00000000, 0x00000000, TAG_END);
	GreenPen   = ObtainBestPen(cm, 0x00000000, 0xd0d0d0d0, 0x00000000, TAG_END);
	GreenPen2  = ObtainBestPen(cm, 0x70700000, 0xffffffff, 0x00000000, TAG_END);
	YellowPen  = ObtainBestPen(cm, 0xffffffff, 0xffff0000, 0x00000000, TAG_END);
	OrangePen  = ObtainBestPen(cm, 0xffffffff, 0x80800000, 0x00000000, TAG_END);
	RedPen     = ObtainBestPen(cm, 0xffffffff, 0x20202020, 0x20202020, TAG_END);
	PeakPen    = ObtainBestPen(cm, 0xffffffff, 0x30303030, 0x30303030, TAG_END);
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
		avgTarget = (avg * 145L) / 32768L;
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
	LONG marginX;
	LONG topMargin;
	LONG bottomMargin;
	LONG usableH;
	LONG usableW;
	LONG rows;
	LONG pitchY;
	LONG pitchX;

	marginX = MaxLong(6, WinW / 36);
	topMargin = MaxLong(18, WinH / 16);
	bottomMargin = MaxLong(8, WinH / 28);

	usableH = WinH - topMargin - bottomMargin;
	if(usableH < 80) usableH = MaxLong(20, WinH - 20);

	rows = usableH / 7;
	rows = ClampLong(rows, MIN_GRID_ROWS, MAX_GRID_ROWS);
	pitchY = usableH / rows;
	if(pitchY < 4) pitchY = 4;

	CellGapY = MaxLong(1, pitchY / 4);
	CellH = pitchY - CellGapY;
	if(CellH < 2) CellH = 2;
	CellPitchY = CellH + CellGapY;
	GridRows = rows;

	GridBottom = WinH - bottomMargin;
	GridTop = GridBottom - (GridRows * CellPitchY);
	if(GridTop < topMargin) GridTop = topMargin;

	usableW = WinW - (marginX * 2);
	pitchX = usableW / NUM_BANDS;
	if(pitchX < 4) pitchX = 4;
	CellGapX = MaxLong(1, pitchX / 5);
	CellW = pitchX - CellGapX;
	if(CellW < 2) CellW = 2;
	CellPitchX = CellW + CellGapX;
	GridLeft = (WinW - (NUM_BANDS * CellPitchX) + CellGapX) >> 1;
	if(GridLeft < 0) GridLeft = 0;
}

static void DrawSpectrumGrid(void)
{
	LONG b;
	LONG r;
	LONG x;
	LONG y;
	LONG x2;
	LONG y2;
	LONG filled;
	LONG peakRow;
	LONG activeRows;
	ULONG pen;

	SetDrMd(DrawRP, JAM1);

	/* v0.9: keep the maximum bar/peak position 2-3 rows below the
	   previous absolute top. */
	activeRows = GridRows - 3;
	if(activeRows < 1) activeRows = 1;

	/* v0.1: no grey background LED blocks.
	   Only active VU/spectrum blocks are drawn onto the black background. */
	for(b = 0; b < NUM_BANDS; b++) {
		x = GridLeft + (b * CellPitchX);
		x2 = x + CellW - 1;
		if(x2 >= WinW) x2 = WinW - 1;

		filled = (VuLevel[b] * activeRows) / 255;
		if(VuLevel[b] > 0 && filled < 1) filled = 1;
		filled = ClampLong(filled, 0, activeRows);

		for(r = 0; r < filled; r++) {
			y = GridBottom - ((r + 1) * CellPitchY);
			y2 = y + CellH - 1;
			if(y < 0 || y2 < 0 || y >= WinH) continue;

			pen = PenForGridRow(r);
			SetAPen(DrawRP, pen);
			RectFill(DrawRP, x, y, x2, y2);
		}

		peakRow = (VuPeak[b] * activeRows) / 255;
		if(VuPeak[b] > 0 && peakRow < 1) peakRow = 1;
		peakRow = ClampLong(peakRow, 1, activeRows);
		if(VuPeak[b] > 0) {
			y = GridBottom - (peakRow * CellPitchY);
			y2 = y + CellH - 1;
			if(y >= 0 && y < WinH) {
				SetAPen(DrawRP, PeakPen);
				RectFill(DrawRP, x, y, x2, y2);
			}
		}
	}
}

static ULONG PenForGridRow(LONG row)
{
	LONG p;

	/* v0.8 colour ramp:
	   - green up to around the middle
	   - then yellow
	   - then orange
	   - red near the top
	   The peak-hold marker remains PeakPen, which is red. */
	p = (row * 100) / MaxLong(1, GridRows - 1);

	if(p < 25) return(GreenPen);
	if(p < 50) return(GreenPen2);
	if(p < 68) return(YellowPen);
	if(p < 84) return(OrangePen);
	return(RedPen);
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
	es.es_Title        = "AmigaAMP-VUMeter";
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
