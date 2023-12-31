///////////////////////////////////////////////////////////////////////
//
// ctwain.cpp - implementation of application i/f to TWAIN
//
// Version 1.51  1999.01.31
// Copyright (C) 1998-1999 Dosadi.  All rights reserved.
//
///////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "ctwain.h"

#ifdef _WIN32
  #define DSM_FILENAME "TWAIN_32.DLL"
  #define DSM_ENTRYPOINT "DSM_Entry"
#else
  #define DSM_FILENAME "TWAIN.DLL"
  #define DSM_ENTRYPOINT "DSM_ENTRY"
#endif

#define IsValidHandle(h) ((h) != NULL)

#define TRACE_ERROR(e) { if (bTrace) TRACE("TWAIN: Error(%d)\n", e); TwainError(e); }

static int			iAvailable;       // TWAIN available: 0:unknown, -1:No, 1:Yes
static DSMENTRYPROC	pSM_Entry;        // entry point of Data Source Manager (TWAIN.DLL)
#ifndef FLIPBOOK_MAC
static HINSTANCE	hSMLib;           // handle of SM
#endif

// default application identity structure:
const TW_IDENTITY	DefAppId = {
	0,									// Id, filled in by SM
	{ 1, 0, TWLG_USA, TWCY_USA, "<?>"},	// Version
    TWON_PROTOCOLMAJOR,
    TWON_PROTOCOLMINOR,
    DG_IMAGE | DG_CONTROL,
    "<?>",								// Mfg
    "<?>",								// Family
    "<?>"								// Product
    };
static TW_IDENTITY	SourceId;			// source identity structure


// Misc. helper stuff
double ppm2dpi(long ppm)
// converts pels/meter (ppm) to dots/inch (dpi)
{
   return 0.0254 * ppm;
}

long dpi2ppm(double dpi)
// converts dpi to ppm
{
   return (long)(dpi / 0.0254 + 0.5);
}


#ifdef FLIPBOOK_MAC

@interface TwainMessageHelper : NSObject
{
	CTwain* fTwain;
}

- (id) initWithTwain:(CTwain *)inTwain;

- (void) closeRequest:(id)inSender;
- (void) xferReady:(id)inSender;

@end

TwainMessageHelper* gTwainMessageHelper = nil;

@implementation TwainMessageHelper

- (id) initWithTwain:(CTwain *)inTwain
{
	self = [super init];
	if (self) {
		fTwain = inTwain;
	}
	
	return self;
}

- (void) closeRequest:(id)inSender
{
	fTwain->CloseDsRequest();
#if 0
	fTwain->DropToState (SOURCE_MANAGER_LOADED);
//
	fTwain->CloseSourceManager();
	fTwain->UnloadSourceManager();
	fTwain->DropToState (PRE_SESSION);
#endif
}

- (void) xferReady:(id)inSender
{
	fTwain->XferReady (0);
}

@end

TW_UINT16 MacTwainCallback (pTW_IDENTITY pOrigin,
                         pTW_IDENTITY pDest,
                         TW_UINT32    DG,
                         TW_UINT16    DAT,
                         TW_UINT16    MSG,
                         TW_MEMREF    pData)
{    
    switch (MSG)
    {
        case MSG_CLOSEDSREQ:
            [gTwainMessageHelper performSelector: @selector(closeRequest:)
                              withObject: NULL
                              afterDelay: 0.1];
            break;
            
        case MSG_XFERREADY:
            [gTwainMessageHelper performSelector: @selector(xferReady:)
                              withObject: NULL
                              afterDelay: 0.1];
            break;
            
        default:
            break;
    }
    return TWRC_SUCCESS;
}

#endif


// CTwain class members
CTwain::CTwain(void)
 : pDefWnd(NULL), nState(NO_TWAIN_STATE)
{
	bShowUI = TRUE;
	hDib = NULL;
	rc = TWRC_SUCCESS;
	// turn off if you get tired of seeing the output:
	bTrace = TRUE;

	AppId = DefAppId;			// provide default registration
	SetState(PRE_SESSION);
}


void CTwain::RegisterApp(	// record application information
	int   nMajorNum, int nMinorNum,	// major and incremental revision of application. E.g.
                                    // for version 2.1, nMajorNum == 2 and nMinorNum == 1
	int   nLanguage,                 // language of this version (use TWLG_xxx from TWAIN.H)
	int   nCountry,                  // country of this version (use TWCY_xxx from TWAIN.H)
	LPSTR	lpszVersion,               // version info string e.g. "1.0b3 Beta release"
	LPSTR	lpszMfg,                   // name of manufacturer/developer e.g. "Crazbat Software"
	LPSTR	lpszFamily,                // product family e.g. "BitStomper"
	LPSTR	lpszProduct)               // specific product e.g. "BitStomper Deluxe Pro"
{
    AppId.Id = 0;						// init to 0, but Source Manager will assign real value
    AppId.Version.MajorNum = nMajorNum;
    AppId.Version.MinorNum = nMinorNum;
    AppId.Version.Language = nLanguage;
    AppId.Version.Country  = nCountry;
    lstrcpy (AppId.Version.Info, lpszVersion);

    AppId.ProtocolMajor =    TWON_PROTOCOLMAJOR;
    AppId.ProtocolMinor =    TWON_PROTOCOLMINOR;
    AppId.SupportedGroups =  DG_IMAGE | DG_CONTROL;
    lstrcpy (AppId.Manufacturer,  lpszMfg);
    lstrcpy (AppId.ProductFamily, lpszFamily);
    lstrcpy (AppId.ProductName,   lpszProduct);

}

CTwain::~CTwain()
{
	// shut down the Twain connection
	CloseSourceManager();
	UnloadSourceManager();
}

int CTwain::TwainAvailable(void)
{
	if (pSM_Entry) return TRUE;		// SM currently loaded

	if (iAvailable == 0) {
		if (LoadSourceManager()) {
			iAvailable = 1;
		} else {
			iAvailable = -1;
		}
	}
	return (iAvailable > 0);
}


TW_STATE CTwain::State(void)
{
	return nState;
}


void CTwain::SetState(TW_STATE nS)
{
#ifdef _DEBUG
	if (bTrace) {
		const char *pzState[] = {
			"",
			"1:PRE_SESSION",
			"2:SOURCE_MANAGER_LOADED",
			"3:SOURCE_MANAGER_OPEN",
			"4:SOURCE_OPEN",
			"5:SOURCE_ENABLED",
			"6:TRANSFER_READY",
			"7:TRANSFERRING"
		};
		TRACE("TWAIN: State %s -> %s\n", pzState[nState], pzState[nS]);
	}
#endif

	nState = nS;				// update the 'global'
	StateChange(nS);			// notify derived classes via callback
} // SetState


void CTwain::SetDefWindow(CWnd* pWnd)
// Sets the CWnd* to be used as the default for all other calls that
// take a window argument - SelectSource, OpenSourceManager, etc.
// If no default window is set, AfxGetMainWnd() is used.
{
	pDefWnd = pWnd;
}


CWnd* CTwain::DefWnd(CWnd* pWnd)
// Returns pWnd if it's non-null, otherwise it
// finds a safe non-null default substitute.
{
	return pWnd ? pWnd : pDefWnd ? pDefWnd : AfxGetMainWnd();
} // DefWnd


int CTwain::SelectSource(CWnd* pWnd)
{
	TW_IDENTITY		NewSourceId;
	TW_STATE		nStartState = State();
	int				bSuccess = FALSE;

	pWnd = DefWnd(pWnd);
 	if (!OpenSourceManager(pWnd)) {
		TRACE_ERROR(TWERR_OPEN_DSM);
		//"Unable to load & open TWAIN Source Manager");
    } else {

		// I will settle for the system default.  Shouldn't I get a highlight
		// on system default without this call?
		SM(DG_CONTROL, DAT_IDENTITY, MSG_GETDEFAULT, &NewSourceId);
		// now do the real thing
    	bSuccess = SM(DG_CONTROL, DAT_IDENTITY, MSG_USERSELECT, &NewSourceId);
	}

    DropToState(nStartState, pWnd);
	return bSuccess;
}

void CTwain::ModalAcquire(CWnd* pWnd)
{
	pWnd = DefWnd(pWnd);
	if (BeginAcquire(pWnd)) {
		pWnd->EnableWindow(FALSE);
		// source is enabled, wait for transfer or source closed
		ModalEventLoop();
		pWnd->EnableWindow(TRUE);
		DropToState(nStartState);
	} else {
		// BeginAcquire puts everything back when it fails
	}
}

int CTwain::BeginAcquire(CWnd* pWnd)
{
	nStartState = State();
	pWnd = DefWnd(pWnd);

	if (State() >= SOURCE_MANAGER_OPEN || OpenSourceManager(pWnd)) {
		if (State() >= SOURCE_OPEN || (OpenDefaultSource() && NegotiateCapabilities())) {
			if (State() >= SOURCE_ENABLED || EnableSource(pWnd)) {
				return TRUE;
			} else {
				TRACE_ERROR(TWERR_ENABLE_SOURCE);
			}
		} else {
			TRACE_ERROR(TWERR_OPEN_SOURCE);
		}
	} else {
		TRACE_ERROR(TWERR_OPEN_DSM);
	}
	DropToState(nStartState);
	return FALSE;
} // BeginAcquire


int CTwain::LoadSourceManager(void)
{
#ifndef FLIPBOOK_MAC
	char		szSM[256];
	OFSTRUCT	of;

	if (nState >= SOURCE_MANAGER_LOADED) {
		return TRUE;			// SM already loaded
	}

	GetWindowsDirectory(szSM, sizeof(szSM));
	if (szSM[lstrlen(szSM)-1] != '\\') {
		lstrcat(szSM, "\\");
	}
	lstrcat(szSM, DSM_FILENAME);			// could crash!
	if (OpenFile(szSM, &of, OF_EXIST) != -1) {
		hSMLib = LoadLibrary(szSM);
	} else {
		if (bTrace) TRACE("TWAIN: LoadLibrary(%s) failed\n", szSM);
		hSMLib = NULL;
	}
	if (IsValidHandle(hSMLib)) {
		pSM_Entry = (DSMENTRYPROC) GetProcAddress(hSMLib, DSM_ENTRYPOINT);
		if (pSM_Entry) {
			iAvailable = 1;
			SetState(SOURCE_MANAGER_LOADED);
		} else {
			if (bTrace) TRACE("TWAIN: GetProcAddress() failed!!\n");
			FreeLibrary(hSMLib);
			hSMLib = NULL;
		}
	} else {
		pSM_Entry = NULL;
	}

	if (nState != SOURCE_MANAGER_LOADED && bTrace) TRACE("TWAIN: LoadSourceManager() failed.\n");
	return (nState >= SOURCE_MANAGER_LOADED);
#else
	if (nState >= SOURCE_MANAGER_LOADED) {
		return TRUE;			// SM already loaded
	}

	// it's just available because we link to the framework; no need to load a shared library
	pSM_Entry = DSM_Entry;
	iAvailable = 1;
	SetState (SOURCE_MANAGER_LOADED);
	return (nState >= SOURCE_MANAGER_LOADED);
#endif
} // LoadSourceManager


int CTwain::OpenSourceManager(CWnd* pWnd)
{
	TW_INT32 hwnd32 = (TW_INT32)(int)(DefWnd(pWnd)->m_hWnd);

	if (LoadSourceManager()) {
		SM(DG_CONTROL, DAT_PARENT, MSG_OPENDSM, &hwnd32);
		if (nState != SOURCE_MANAGER_OPEN && bTrace) TRACE("TWAIN: OPENDSM failed.\n");
	}

#ifdef FLIPBOOK_MAC
	TW_CALLBACK	callback;
	callback.CallBackProc = (TW_MEMREF)MacTwainCallback;
	callback.RefCon       = (TW_UINT32)this;
	callback.Message      = 0;
	TW_INT16 result = DSM_Entry (&AppId, NULL, DG_CONTROL, DAT_CALLBACK, MSG_REGISTER_CALLBACK, (TW_MEMREF)&callback);
	NSLog (@"TWAIN callback registry: %d", result);
	if (gTwainMessageHelper) {
		[gTwainMessageHelper release];
	}
	gTwainMessageHelper = [[TwainMessageHelper alloc] initWithTwain:this];
#endif

	return (nState >= SOURCE_MANAGER_OPEN);
}


int CTwain::OpenDefaultSource(void)
{
	if (nState != SOURCE_MANAGER_OPEN) return FALSE;

	// open the system default source
	SourceId.ProductName[0] = '\0';
	SourceId.Id = 0;
	SM(DG_CONTROL, DAT_IDENTITY, MSG_OPENDS, &SourceId);
	if (nState != SOURCE_OPEN && bTrace) TRACE("TWAIN: OPENDS failed.\n");
	
	return (nState == SOURCE_OPEN);
} // OpenDefaultSource


void CTwain::SetShowUI(BOOL bShow)
// Set flag for whether source should be enabled with
// user interface visible (bShow == TRUE) or not.
// At construction, ShowUI is set TRUE.
{
	bShowUI = bShow;
} // SetShowUI


BOOL CTwain::GetShowUI(void)
// Return state of ShowUI flag.
{
	return bShowUI;
} // GetShowUI


int CTwain::EnableSource(CWnd* pWnd)
{
	pWnd = DefWnd(pWnd);
	if (nState != SOURCE_OPEN) {
		if (bTrace) TRACE("TWAIN Warning: wrong state for EnableSource()\n"); 
		return FALSE;
	}

	twUI.ShowUI = bShowUI;
	twUI.hParent = (TW_HANDLE)(pWnd->m_hWnd);
	DS(DG_CONTROL, DAT_USERINTERFACE, MSG_ENABLEDS, &twUI);
	if (nState != SOURCE_ENABLED && bTrace) TRACE("TWAIN: ENABLEDS failed.\n");
	// note, source will set twUI.ModalUI.
	return (nState == SOURCE_ENABLED);
} // TWAIN_EnableSource


int CTwain::DisableSource(void)
{
	if (nState == SOURCE_ENABLED) {
		DS(DG_CONTROL, DAT_USERINTERFACE, MSG_DISABLEDS, &twUI);
        if (nState == SOURCE_ENABLED && bTrace) {
            TRACE("TWAIN: DISABLEDS failed.\n");
        } else {
            // Disable succeeded, do this in case the focus
            // to make sure focus goes back to the app
            ::BringWindowToTop((HWND)twUI.hParent);
        }
    }
	return (nState < SOURCE_ENABLED);
} // DisableSource


int CTwain::CloseSource(void)
{
	rc = TWRC_SUCCESS;

	if (nState == SOURCE_ENABLED) {
		DisableSource();
	}
	if (nState == SOURCE_OPEN) {
		DS(DG_CONTROL, DAT_IDENTITY, MSG_CLOSEDS, &SourceId);
		if (nState == SOURCE_OPEN && bTrace) TRACE("TWAIN: CLOSEDS failed.\n");
	}
	return (nState < SOURCE_OPEN);
} // CloseSource


int CTwain::CloseSourceManager(CWnd* pWnd)
{
	CloseSource();			// close source if open

	if (nState >= SOURCE_MANAGER_OPEN) {
		TW_INT32 hwnd32 = (TW_INT32)(int)(DefWnd(pWnd)->m_hWnd);
		SM(DG_CONTROL, DAT_PARENT, MSG_CLOSEDSM, &hwnd32);
		if (nState >= SOURCE_MANAGER_OPEN && bTrace) TRACE("TWAIN: CLOSEDSM failed.\n");
	}
	return (nState < SOURCE_MANAGER_OPEN);
}


int CTwain::UnloadSourceManager(void)
{
	rc = TWRC_SUCCESS;

	if (nState == SOURCE_MANAGER_LOADED) {
#ifndef FLIPBOOK_MAC
		if (hSMLib) {
			FreeLibrary(hSMLib);
			hSMLib = NULL;
		}
#endif
		pSM_Entry = NULL;
		SetState(PRE_SESSION);
	}
	return (nState == PRE_SESSION);
} // UnloadSourceManager


int CTwain::EndXfer(void)
{
	if (nState == TRANSFERRING) {
		DS(DG_CONTROL, DAT_PENDINGXFERS, MSG_ENDXFER, &pendingXfers);
		if (nState == TRANSFERRING && bTrace) TRACE("TWAIN: ENDXFER failed.\n");
	} else {
		if (bTrace) TRACE("TWAIN Warning: wrong state for EndXfer()\n"); 
	}
	return nState < TRANSFERRING;
} // EndXfer

int CTwain::CancelXfers(void)
{
	EndXfer();			// if transferring, cancel it

	if (nState == TRANSFER_READY) {
		DS(DG_CONTROL, DAT_PENDINGXFERS, MSG_RESET, &pendingXfers);
		if (nState == TRANSFER_READY && bTrace) TRACE("TWAIN: PENDINGXFERS,RESET failed.\n");
	}
	return (nState < TRANSFER_READY);
} // CancelXfers


int CTwain::DropToState(int nS, CWnd* pWnd)
{
	pWnd = DefWnd(pWnd);
	while (nState > nS) {
		switch (nState) {
			case TRANSFERRING:
				if (!EndXfer()) return FALSE;
				break;
			case TRANSFER_READY:
				if (!CancelXfers()) return FALSE;
				break;
			case SOURCE_ENABLED:
				if (!DisableSource()) return FALSE;
				break;
			case SOURCE_OPEN:
				if (!CloseSource()) return FALSE;
				break;
			case SOURCE_MANAGER_OPEN:
				if (!CloseSourceManager(pWnd)) return FALSE;
				break;
			case SOURCE_MANAGER_LOADED:
				if (!UnloadSourceManager()) return FALSE;
				break;
			default:
				ASSERT(FALSE);
		} // switch
	} // while
	return TRUE;
}


void CTwain::ModalEventLoop(void)
{
#ifndef FLIPBOOK_MAC
	MSG msg;
	while ((nState >= SOURCE_ENABLED) && GetMessage((LPMSG)&msg, NULL, 0, 0)) {
		if (!TwainMessageHook ((LPMSG)&msg)) {
			TranslateMessage ((LPMSG)&msg);
			DispatchMessage ((LPMSG)&msg);
		}
	} // while
#else
	EventRecord e;
	while (nState >= SOURCE_ENABLED) {
		if (WaitNextEvent (everyEvent, &e, 60, NULL)) {
			TwainMessageHook ((LPMSG)&e);
		}
	}
#endif
} // ModalEventLoop


int CTwain::TwainMessageHook(LPMSG lpmsg)
// returns TRUE if message processed by TWAIN
// FALSE otherwise
{
	int		bProcessed = FALSE;

	if (nState >= SOURCE_ENABLED) {
		// source enabled
		TW_EVENT	twEvent;
		twEvent.pEvent = (TW_MEMREF)lpmsg;
		twEvent.TWMessage = MSG_NULL;
		// relay message to source in case it wants it
		DS(DG_CONTROL, DAT_EVENT, MSG_PROCESSEVENT, &twEvent);
		bProcessed = (rc == TWRC_DSEVENT);
		switch (twEvent.TWMessage) {
			case MSG_XFERREADY:
				// notify by callback
				// default callback does transfers
				XferReady(lpmsg);
				break;

			case MSG_CLOSEDSREQ:
				// notify by callback
				// default callback closes the source
				CloseDsRequest();
				break;

			case MSG_NULL:
				// no message returned from DS
				break;
		} // switch
	}

	return bProcessed;
} // TwainMessageHook

void CTwain::XferReady(LPMSG lpmsg)
// Default transfer-ready handler
// Assumes xfer mech is Native, gets the next
// DIB and calls back to DibReceived();
{
	TW_UINT32		hNative;

	ASSERT(nState == TRANSFER_READY);

	do {
		// get xfers until there are no more, or 
		DS(DG_IMAGE, DAT_IMAGENATIVEXFER, MSG_GET, &hNative);

		if (TWRC_XFERDONE != rc) {
			// transfer failed for some reason or another
			hNative = NULL;
			if (bTrace) TRACE("TWAIN: NATIVEXFER,GET failed.\n");
		}
		ASSERT(nState >= TRANSFER_READY);
		EndXfer();				// acknowledge & end transfer
		ASSERT(nState == TRANSFER_READY || nState == SOURCE_ENABLED);
		if (hNative) {
			// call back with DIB
			if (bTrace) TRACE0("TWAIN: calling DibReceived...\n");
			#ifdef FLIPBOOK_MAC
			// for the Mac we need to convert from PICT first
			HGLOBAL dib_handle = MakeBitmapHandleFromPICT ((void *)hNative);
			if (dib_handle) {
				DibReceived (dib_handle);
			}
			#else
			DibReceived((HGLOBAL)hNative);
			#endif
		}
	} while (nState == TRANSFER_READY);
}

void CTwain::CloseDsRequest(void)
{
	CloseSource();
}


int CTwain::NegotiateCapabilities(void)
// called after source is successfully opened.
// Use this call-back to negotiate any special settings
// (capabilities) for the session.  Default does nothing.
{
   return TRUE;
}

void CTwain::StateChange(int nState)
{
}

void CTwain::TwainError(TW_ERR e)
{
}


void CTwain::DibReceived(HGLOBAL hDib)
{
	GlobalFree(hDib);
}


///////////////////////////////////////////////////////////////////
// Capability Negotiation

int CTwain::SetCurrentResolution(double dRes)
// Negotiate the current resolution for acquisition.
// Negotiation is only allowed in State 4 (TWAIN_SOURCE_OPEN)
// The source may select this resolution, but don't assume it will.
{
	return SetCapFix32(ICAP_XRESOLUTION, dRes);
} // SetCurrentResolution


int CTwain::SetCurrentPixelType(int nType)
{
   return SetCapOneValue(ICAP_PIXELTYPE, TWTY_UINT16, (TW_UINT16)nType);
} // SetCurrentPixelType


int CTwain::SetXferCount(int nXfers)
{
	return SetCapOneValue(CAP_XFERCOUNT, TWTY_INT16, nXfers);
} // SetXferCount


int CTwain::SetBrightness(double dBri)
{
   return SetCapFix32(ICAP_BRIGHTNESS, dBri);
} // SetBrightness


int CTwain::SetContrast(double dCon)
{
   return SetCapFix32(ICAP_CONTRAST, dCon);
} // SetContrast


int CTwain::SetCurrentUnits(unsigned uUnits)
{
    return SetCapOneValue(ICAP_UNITS, TWTY_UINT16, uUnits);
} // SetCurrentUnits


unsigned CTwain::GetCurrentUnits(void)
{
    unsigned short usUnits = TWUN_INCHES;
    GetCapCurrent(ICAP_UNITS, usUnits);
    return usUnits;
} // GetCurrentUnits


int CTwain::SetImageLayout(double left, double top, double width, double height)
{
    if (nState != SOURCE_OPEN) {
        TRACE_ERROR(TWERR_NOT_4);
        return FALSE;
    }

    TW_IMAGELAYOUT layout;
    layout.Frame.Left = ToFix32(left);
    layout.Frame.Top = ToFix32(top);
    layout.Frame.Right = ToFix32(left + width);
    layout.Frame.Bottom = ToFix32(top + height);
    // I hope these are ignored:
    layout.DocumentNumber = 0;
    layout.PageNumber = 0;
    layout.FrameNumber = 0;
    return DS(DG_IMAGE, DAT_IMAGELAYOUT, MSG_SET, &layout);
} // SetImageLayout


int CTwain::GetImageLayout(double &left, double &top, double &width, double &height)
{
    TW_IMAGELAYOUT layout;
    if (DS(DG_IMAGE, DAT_IMAGELAYOUT, MSG_GET, &layout)) {
        left = Fix32ToFloat(layout.Frame.Left);
        top = Fix32ToFloat(layout.Frame.Top);
        width = Fix32ToFloat(layout.Frame.Right) - left;
        height = Fix32ToFloat(layout.Frame.Bottom) - top;
        return TRUE;
    } else {
        left = top = width = height = 0.0;
        return FALSE;
    }
} // SetImageLayout

//----- Capability Helper Functions

int CTwain::SetCapFix32(unsigned Cap, double dVal)
{
   return SetCapOneValue(Cap, ToFix32(dVal));
} // SetCapFix32


TW_FIX32 CTwain::ToFix32(double r)
{
	TW_FIX32 fix;
   TW_INT32 val = (TW_INT32)(r * 65536.0 + (r < 0 ? -0.5 : 0.5));
	fix.Whole = (TW_INT16)(val >> 16);			// most significant 16 bits
	fix.Frac = (TW_UINT16)(val & 0xffff);		// least
   return fix;
} // ToFix32


double CTwain::Fix32ToFloat(TW_FIX32 fix)
{
	TW_INT32 val = ((TW_INT32)fix.Whole << 16) | ((TW_UINT32)fix.Frac & 0xffff);
	return val / 65536.0;
} // Fix32ToFloat


int CTwain::SetCapOneValue(unsigned Cap, TW_FIX32 ItemVal)
{
   return SetCapOneValue(Cap, TWTY_FIX32, *(TW_UINT32*)&ItemVal);
} // SetCapOneValue


int CTwain::SetCapOneValue(unsigned Cap, unsigned ItemType, TW_UINT32 ItemVal)
{
	TW_CAPABILITY	cap;
	pTW_ONEVALUE	pv;
	BOOL			bSuccess;

	if (nState != SOURCE_OPEN) {
      TRACE_ERROR(TWERR_NOT_4);
		return FALSE;
	}

	cap.Cap = Cap;			            // capability id
	cap.ConType = TWON_ONEVALUE;		// container type
   do {
	   cap.hContainer = (TW_HANDLE) GlobalAlloc(GHND, sizeof (TW_ONEVALUE));
	   if (!cap.hContainer) {
         if (IDCANCEL == MyError(IDS_BAD_TWAIN,
						MB_RETRYCANCEL | MB_ICONEXCLAMATION)) {
		      return FALSE;
         }
      }
   } while (!cap.hContainer);
	pv = (pTW_ONEVALUE)GlobalLock((HANDLE)cap.hContainer);
	pv->ItemType = ItemType;
	pv->Item = ItemVal;
	GlobalUnlock((HANDLE)cap.hContainer);
	bSuccess = DS(DG_CONTROL, DAT_CAPABILITY, MSG_SET, (TW_MEMREF)&cap);
   GlobalFree((HANDLE)cap.hContainer);
	return bSuccess;
} // SetCapOneValue


BOOL CTwain::GetCapCurrent(unsigned Cap, short& sVal)
{
   return GetCapCurrent(Cap, TWTY_INT16, &sVal);
}


BOOL CTwain::GetCapCurrent(unsigned Cap, int& nVal)
{
   return GetCapCurrent(Cap, TWTY_INT32, &nVal);
}


BOOL CTwain::GetCapCurrent(unsigned Cap, unsigned short &wVal)
{
   return GetCapCurrent(Cap, TWTY_UINT16, &wVal);
}


BOOL CTwain::GetCapCurrent(unsigned Cap, unsigned &uVal)
{
   return GetCapCurrent(Cap, TWTY_UINT32, &uVal);
}


BOOL CTwain::GetCapCurrent(unsigned Cap, double& dVal)
{
   dVal = 0.0;
   TW_FIX32 fix;
   if (GetCapCurrent(Cap, TWTY_FIX32, &fix)) {
      dVal = Fix32ToFloat(fix);
      return TRUE;
   } else {
      return FALSE;
   }
}


const size_t nTypeSize[13] =
	{	sizeof (TW_INT8),
		sizeof (TW_INT16),
		sizeof (TW_INT32),
		sizeof (TW_UINT8),
		sizeof (TW_UINT16),
		sizeof (TW_UINT32),
		sizeof (TW_BOOL),
		sizeof (TW_FIX32),
		sizeof (TW_FRAME),
		sizeof (TW_STR32),
		sizeof (TW_STR64),
		sizeof (TW_STR128),
		sizeof (TW_STR255),
	};

// helper function:
static int TypeMatch(unsigned nTypeA, unsigned nTypeB)
{
	// Integral types match if they are the same size.
	// All other types match only if they are equal
	return nTypeA == nTypeB ||
		   (nTypeA <= TWTY_UINT32 &&
		    nTypeB <= TWTY_UINT32 &&
		    nTypeSize[nTypeA] == nTypeSize[nTypeB]);
} // TypeMatch


BOOL CTwain::GetCapCurrent(unsigned Cap, unsigned ItemType, void FAR *pVal)
{
#ifndef FLIPBOOK_MAC
	TW_CAPABILITY 	cap;
	void far *		pv = NULL;
	BOOL			   bSuccess = FALSE;

   if (nState < SOURCE_OPEN) {
      TRACE_ERROR(TWERR_NOT_4);
		return FALSE;
	}

	// Fill in capability structure
	cap.Cap = Cap;					// capability id
	cap.ConType = TWON_ONEVALUE;	// favorite type of container (should be ignored...)
	cap.hContainer = NULL;

	if (DS(DG_CONTROL, DAT_CAPABILITY, MSG_GETCURRENT, (TW_MEMREF)&cap) &&
	    cap.hContainer &&
	    (pv = GlobalLock(cap.hContainer))) {

		if (cap.ConType == TWON_ENUMERATION) {
			TW_ENUMERATION far *pcon = (TW_ENUMERATION far *)pv;
			TW_UINT32 index = pcon->CurrentIndex;
			if (index < pcon->NumItems && TypeMatch(pcon->ItemType, ItemType)) {
				LPSTR pitem = (LPSTR)pcon->ItemList + index*nTypeSize[ItemType];
				memcpy(pVal, pitem, nTypeSize[ItemType]);
				bSuccess = TRUE;
			}
		} else if (cap.ConType == TWON_ONEVALUE) {
			TW_ONEVALUE far *pcon = (TW_ONEVALUE far *)pv;
			if (TypeMatch(pcon->ItemType, ItemType)) {
				memcpy(pVal, &pcon->Item, nTypeSize[ItemType]);
				bSuccess = TRUE;
			}
		}
	}
	
	if (pv) GlobalUnlock(cap.hContainer);
	if (cap.hContainer) GlobalFree(cap.hContainer);

	return bSuccess;
#else
	return false;
#endif
} 


///////////////////////////////////////////////////////////////////
// Primitive functions

static char* pzRcName[] = {
   "TWRC_SUCCESS",
   "TWRC_FAILURE",
   "TWRC_CHECKSTATUS",
   "TWRC_CANCEL",
   "TWRC_DSEVENT",
   "TWRC_NOTDSEVENT",
   "TWRC_XFERDONE",
   "TWRC_ENDOFLIST" };

static char* pzCcName[] = {
   "TWCC_SUCCESS",       
   "TWCC_BUMMER",
   "TWCC_LOWMEMORY",     
   "TWCC_NODS",
   "TWCC_MAXCONNECTIONS",
   "TWCC_OPERATIONERROR",
   "TWCC_BADCAP",
   "<bad cc:7>",
   "<bad cc:8>",
   "TWCC_BADPROTOCOL",
   "TWCC_BADVALUE",
   "TWCC_SEQERROR",
   "TWCC_BADDEST",
   "TWCC_CAPUNSUPPORTED",
   "TWCC_CAPBADOPERATION",
   "TWCC_CAPSEQERROR" };


int CTwain::DS(unsigned long dg, unsigned dat, unsigned msg, void FAR *pd)
// Call the current source with a triplet
{
	int bSuccess = FALSE;
	ASSERT(nState >= SOURCE_OPEN);
	if (pSM_Entry) {
		rc = (*pSM_Entry)(&AppId, &SourceId, dg, dat, msg, (TW_MEMREF)pd);
      if (dat != DAT_STATUS && dat != DAT_EVENT) {
         TRACE("DS(%ul, %u, %u) => %s", dg, dat, msg, pzRcName[rc]);
         if (rc != TWRC_SUCCESS) {
            unsigned cc = ConditionCode();
            TRACE(", %s", pzCcName[cc]);
         }
         TRACE("\n");
      }
		bSuccess = (TWRC_SUCCESS == rc);
		// model state changes!
		if (DG_CONTROL == dg) {
			if (DAT_EVENT == dat) {
				if (MSG_PROCESSEVENT == msg) {
					if (MSG_XFERREADY == ((TW_EVENT FAR *)pd)->TWMessage) {
						if (bTrace) TRACE0("TWAIN: received MSG_XFERREADY\n");
						SetState(TRANSFER_READY);
					} else if (MSG_CLOSEDSREQ == ((TW_EVENT FAR *)pd)->TWMessage) {
						// well, see TWAIN v1.5 p7-27!
						if (bTrace) TRACE0("TWAIN: received MSG_CLOSEDSREQ\n");
						SetState(SOURCE_ENABLED);
					}
				}
			} else if (DAT_PENDINGXFERS == dat) {
				if (MSG_RESET == msg) {
					if (bSuccess) SetState(SOURCE_ENABLED);
				} else if (MSG_ENDXFER == msg) {
					if (bSuccess) {
						SetState(((TW_PENDINGXFERS FAR *)pd)->Count ? TRANSFER_READY : SOURCE_ENABLED);
                        // Note that a Count of -1 is valid,
                        // indicating 'unknown number' pending.
                        // TWAIN 1.6 4-22
                    } else if (nState > SOURCE_ENABLED) {
                        // We can only guess that the DS made a, shall we say,
                        // 'unexpected' transition out of state 7 or 8.
                        // Thanks to Pat O'Neil for tipping us to this.
                        SetState(SOURCE_ENABLED);
						if (bTrace) TRACE0("TWAIN: unexpected ENDXFER failure.\n");
                    }
				}
			} else if (DAT_USERINTERFACE == dat) {
				if (MSG_DISABLEDS == msg) {
					if (bSuccess) SetState(SOURCE_OPEN);
				} else if (MSG_ENABLEDS == msg) {
                    bSuccess |= (TWRC_CHECKSTATUS == rc);
                    if (bSuccess) {
                        SetState(SOURCE_ENABLED);
                    }
				}
			} else if (DAT_IDENTITY == dat) {
				if (MSG_CLOSEDS == msg) {
					if (bSuccess) SetState(SOURCE_MANAGER_OPEN);
				}
			}
		} else if (DG_IMAGE == dg) {
			if (DAT_IMAGENATIVEXFER == dat) {
				if (MSG_GET == msg) {
					if (TWRC_XFERDONE == rc) {
						// xfer successful, still in State 7
						SetState(TRANSFERRING);
					} else if (TWRC_CANCEL == rc) {
						// xfer cancelled by user, still in State 7
						SetState(TRANSFERRING);
					} else {
						// something else happened
						// transition to State 7 failed
						// STAY IN STATE 6
						SetState(TRANSFER_READY);
					}
				}
			}
		}
	}
	return bSuccess;
} // DS



int CTwain::SM(unsigned long dg, unsigned dat, unsigned msg, void FAR *pd)
// Call the Source Manager with a triplet
{
	int bSuccess = FALSE;
	ASSERT(nState >= SOURCE_MANAGER_LOADED);
	ASSERT(pSM_Entry);
	if (pSM_Entry) {
		rc = (*pSM_Entry)(&AppId, NULL, dg, dat, msg, (TW_MEMREF)pd);
		bSuccess = (TWRC_SUCCESS == rc);
		if (DG_CONTROL == dg) {
			if (DAT_PARENT == dat) {
				if (MSG_OPENDSM == msg) {
					if (bSuccess) SetState(SOURCE_MANAGER_OPEN);
				} else if (MSG_CLOSEDSM == msg) {
					if (bSuccess) SetState(SOURCE_MANAGER_LOADED);
				}
			} else if (DAT_IDENTITY == dat) {
				if (MSG_OPENDS == msg) {
					if (bSuccess) SetState(SOURCE_OPEN);
				}
			}
		}
	}
	return bSuccess;
} // SM



unsigned CTwain::ResultCode(void)
{
	return rc;
} // ResultCode



unsigned CTwain::ConditionCode(void)
{
	TW_STATUS	twStatus;
   TW_INT16    rc;         // private, not shared

	if (nState >= SOURCE_OPEN) {
		// get source status if open
      rc = (*pSM_Entry)(&AppId, &SourceId, DG_CONTROL, DAT_STATUS, MSG_GET, (TW_MEMREF)&twStatus);
	} else if (nState == SOURCE_MANAGER_OPEN) {
		// otherwise get source manager status
      rc = (*pSM_Entry)(&AppId, NULL, DG_CONTROL, DAT_STATUS, MSG_GET, (TW_MEMREF)&twStatus);
		//SM(DG_CONTROL, DAT_STATUS, MSG_GET, (TW_MEMREF)&twStatus);
	} else {
		// nothing open, not a good time to get condition code!
		return TWCC_SEQERROR;
	}
	if (rc == TWRC_SUCCESS) {
		return twStatus.ConditionCode;
	} else {
		return TWCC_BUMMER;			// what can I say. 
	}
} // ConditionCode


