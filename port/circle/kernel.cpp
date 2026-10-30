//
// FRANK 386 – Raspberry Pi 4 bare-metal port (Circle)
//
// Implements port_hal.h on top of Circle: HDMI frame buffer, USB HID
// (keyboard, mouse, gamepad) through the Pi 4 xHCI controller, the SD
// card through EMMC2, HDMI or 3.5 mm audio, and the BCM2711 system timer.
//
// SPDX-License-Identifier: MIT
//
#include "kernel.h"

#include <circle/startup.h>
#include <circle/synchronize.h>
#include <circle/sound/pwmsoundbasedevice.h>
#include <circle/sound/hdmisoundbasedevice.h>
#include <circle/util.h>
#include <circle_glue.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

extern "C" {
#include "port_hal.h"
#include "frank_main.h"
#include "hid_keymap.h"
#include "audio.h"
}

static const char FromKernel[] = "frank386";

CKernel *CKernel::s_pThis = 0;

// ---------------------------------------------------------------------------
// Shared state between cores / interrupt handlers
// ---------------------------------------------------------------------------

// Keyboard events: produced in the USB interrupt handler, consumed on core 0.
#define KEYQ_SIZE 256
static struct { u8 down; u8 code; } s_KeyQ[KEYQ_SIZE];
static volatile unsigned s_nKeyHead, s_nKeyTail;

// Mouse: accumulated in the interrupt handler.
static volatile int s_nMouseDX, s_nMouseDY, s_nMouseDZ;
static volatile unsigned s_nMouseButtons, s_nMouseButtonsSent;
static volatile bool s_bMouseEvent;

// Gamepad: latest state.
static volatile int s_nPadX, s_nPadY;
static volatile u8 s_nPadButtons;
static volatile bool s_bPadPresent;

// Video: lock-free triple buffer from core 0 (emulator) to core 2 (scaler).
#define FRAME_MAX_W 1024
#define FRAME_MAX_H 768
struct TFrame
{
	unsigned nWidth, nHeight;
	u32 *pPixels;			// nWidth x nHeight, tightly packed
};
static TFrame s_Frames[3];
#define FRAME_NEW 4u
static volatile unsigned s_nMiddle = 1;		// index | FRAME_NEW
static unsigned s_nWriteIdx = 0;		// owned by core 0
static unsigned s_nReadIdx = 2;			// owned by core 2

// ---------------------------------------------------------------------------

CKernel::CKernel (void)
:	m_Serial (&m_Interrupt),
	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_USBHCI (&m_Interrupt, &m_Timer, TRUE),
	m_EMMC (&m_Interrupt, &m_Timer, &m_ActLED),
	m_Console (&m_Serial, &m_Serial),
	m_Cores (CMemorySystem::Get (), this),
	m_pFrameBuffer (0),
	m_nScreenWidth (0), m_nScreenHeight (0), m_nPitchPixels (0),
	m_bDoubleBuffered (false),
	m_pSound (0),
	m_bSoundHDMI (true),
	m_pKeyboard (0),
	m_pMouse (0),
	m_pGamePad (0),
	m_nDiskSectors (0),
	m_bUSBOK (true)
{
	s_pThis = this;
	m_ActLED.Blink (3);
}

boolean CKernel::Initialize (void)
{
	boolean bOK = TRUE;

	if (bOK) bOK = m_Serial.Initialize (115200);
	if (bOK) bOK = m_Logger.Initialize (&m_Serial);
	if (bOK) bOK = m_Interrupt.Initialize ();
	if (bOK) bOK = m_Timer.Initialize ();
	if (bOK) bOK = m_Console.Initialize ();
	if (bOK)
	{
		// printf()/stdout from the emulator core goes to the serial port
		// (GPIO14 TX, 115200 8N1).
		CGlueStdioInit (m_Console);
	}

	if (bOK && !SetupFrameBuffer ())
	{
		m_Logger.Write (FromKernel, LogError, "Cannot initialise HDMI frame buffer");
		bOK = FALSE;
	}

	if (bOK)
	{
		bOK = m_EMMC.Initialize ();
		if (bOK)
		{
			m_nDiskSectors = m_EMMC.GetSize () / 512;
		}
		else
		{
			m_Logger.Write (FromKernel, LogError, "SD card initialisation failed");
			// keep going: the front-end shows a proper error screen
			bOK = TRUE;
		}
	}

	if (bOK && !m_USBHCI.Initialize ())
	{
		// No keyboard means no interaction, but the guest can still boot
		// (and AUTOEXEC.BAT can run); do not halt the whole system.
		m_Logger.Write (FromKernel, LogError, "USB host controller initialisation failed");
		m_bUSBOK = false;
	}

#ifndef FRANK_NO_SOUND		// defined for QEMU smoke tests (no PWM/HDMI audio there)
	if (bOK) SetupSound ();
#endif

	if (bOK) bOK = m_Cores.Initialize ();

	return bOK;
}

bool CKernel::SetupFrameBuffer (void)
{
	unsigned nWidth = m_Options.GetWidth ();	// cmdline.txt width=/height=
	unsigned nHeight = m_Options.GetHeight ();

	// Try double buffering first (tear-free page flipping).
	m_pFrameBuffer = new CBcmFrameBuffer (nWidth, nHeight, 32, 0, 0, 0, TRUE);
	if (m_pFrameBuffer->Initialize ())
	{
		m_bDoubleBuffered = true;
	}
	else
	{
		delete m_pFrameBuffer;
		m_pFrameBuffer = new CBcmFrameBuffer (nWidth, nHeight, 32);
		if (!m_pFrameBuffer->Initialize ())
		{
			return false;
		}
		m_bDoubleBuffered = false;
	}

	m_nScreenWidth  = m_pFrameBuffer->GetWidth ();
	m_nScreenHeight = m_pFrameBuffer->GetHeight ();
	m_nPitchPixels  = m_pFrameBuffer->GetPitch () / 4;

	u32 *pFB = (u32 *) (uintptr) m_pFrameBuffer->GetBuffer ();
	memset (pFB, 0, (size_t) m_pFrameBuffer->GetPitch () * m_nScreenHeight * (m_bDoubleBuffered ? 2 : 1));

	for (unsigned i = 0; i < 3; i++)
	{
		s_Frames[i].pPixels = (u32 *) memalign (64, FRAME_MAX_W * FRAME_MAX_H * 4);
		if (s_Frames[i].pPixels == 0)
		{
			return false;
		}
		s_Frames[i].nWidth = s_Frames[i].nHeight = 0;
	}

	m_Logger.Write (FromKernel, LogNotice, "HDMI %ux%u, %s buffered",
			m_nScreenWidth, m_nScreenHeight, m_bDoubleBuffered ? "double" : "single");
	return true;
}

void CKernel::SetupSound (void)
{
	// cmdline.txt: sounddev=sndpwm  -> 3.5 mm jack only
	//              sounddev=sndhdmi -> HDMI only
	//              (not set)        -> HDMI, falling back to the jack when
	//                                  the display has no audio support
	const char *pDev = m_Options.GetSoundDevice ();
	bool bWantPWM  = pDev && strcmp (pDev, "sndpwm") == 0;
	bool bWantHDMI = pDev && strcmp (pDev, "sndhdmi") == 0;

	for (unsigned nTry = 0; nTry < 2; nTry++)
	{
		bool bHDMI = nTry == 0 ? !bWantPWM : false;
		if (nTry == 1 && (bWantPWM || bWantHDMI))
		{
			break;		// explicit choice: no fallback
		}

		CSoundBaseDevice *pSound;
		if (bHDMI)
		{
			pSound = new CHDMISoundBaseDevice (&m_Interrupt, SOUND_FREQUENCY, 384 * 10);
		}
		else
		{
			pSound = new CPWMSoundBaseDevice (&m_Interrupt, SOUND_FREQUENCY, 2048);
		}

		bool bOK = pSound->AllocateQueue (150);
		if (bOK)
		{
			pSound->SetWriteFormat (SoundFormatSigned16, 2);
			bOK = pSound->Start ();
		}
		if (bOK)
		{
			m_pSound = pSound;
			m_bSoundHDMI = bHDMI;
			m_Logger.Write (FromKernel, LogNotice, "Sound: %s, %u Hz",
					bHDMI ? "HDMI" : "3.5 mm jack (PWM)", SOUND_FREQUENCY);
			return;
		}
		delete pSound;
		m_Logger.Write (FromKernel, LogWarning, "%s sound not available",
				bHDMI ? "HDMI" : "PWM");
	}
}

void CKernel::Run (void)
{
	m_Logger.Write (FromKernel, LogNotice, "FRANK 386 for Raspberry Pi 4 (bare metal)");
	m_Logger.Write (FromKernel, LogNotice, "Compile time: " __DATE__ " " __TIME__);

	// Give USB a moment to enumerate a keyboard before the welcome screen.
	for (unsigned i = 0; i < 20; i++)
	{
		UpdateUSB ();
		m_Timer.MsDelay (50);
	}

	if (!frank_init ())
	{
		m_Logger.Write (FromKernel, LogPanic, "Emulator initialisation failed");
	}
	frank_run ();		// never returns
}

// ---------------------------------------------------------------------------
// Secondary cores
// ---------------------------------------------------------------------------

void CFrankCores::Run (unsigned nCore)
{
	switch (nCore)
	{
	case 1:
		m_pKernel->AudioLoop ();
		break;

	case 2:
		m_pKernel->VideoLoop ();
		break;

	default:
		break;		// core 3 stays idle
	}
}

void CKernel::AudioLoop (void)
{
	if (m_pSound == 0)
	{
		return;
	}

	static s16 Buffer[256 * 2];
	const unsigned nTargetFrames = SOUND_FREQUENCY * 40 / 1000;	// keep ~40 ms queued

	while (1)
	{
		unsigned nQueued = m_pSound->GetQueueFramesAvail ();
		if (nQueued < nTargetFrames)
		{
			audio_render (Buffer, 256);
			m_pSound->Write (Buffer, sizeof Buffer);
		}
		else
		{
			CTimer::SimpleusDelay (1000);
		}
	}
}

void CKernel::VideoLoop (void)
{
	static u32 Row[4096];
	static u16 XMap[4096];
	unsigned nMapSrcW = 0, nMapDstW = 0;
	unsigned nPage = 0;
	unsigned nLastW[2] = {0, 0}, nLastH[2] = {0, 0};
	const unsigned W = m_nScreenWidth, H = m_nScreenHeight;
	u32 *pBase = (u32 *) (uintptr) m_pFrameBuffer->GetBuffer ();

	while (1)
	{
		if (!(s_nMiddle & FRAME_NEW))
		{
			CTimer::SimpleusDelay (500);
			continue;
		}
		s_nReadIdx = __atomic_exchange_n (&s_nMiddle, s_nReadIdx, __ATOMIC_ACQ_REL) & 3;
		const TFrame &F = s_Frames[s_nReadIdx];
		if (F.nWidth == 0 || F.nHeight == 0)
		{
			continue;
		}

		// Largest 4:3 box (CRT proportions) that fits the screen.
		unsigned dh = H, dw = H * 4 / 3;
		if (dw > W)
		{
			dw = W;
			dh = W * 3 / 4;
		}
		if (dw > 4096) dw = 4096;
		unsigned ox = (W - dw) / 2, oy = (H - dh) / 2;

		if (nMapSrcW != F.nWidth || nMapDstW != dw)
		{
			for (unsigned x = 0; x < dw; x++)
			{
				XMap[x] = (u16) (x * F.nWidth / dw);
			}
			nMapSrcW = F.nWidth;
			nMapDstW = dw;
		}

		unsigned nTarget = m_bDoubleBuffered ? (nPage ^ 1) : 0;
		u32 *pPage = pBase + (size_t) nTarget * H * m_nPitchPixels;

		// Clear the borders when this page last held a different geometry.
		if (nLastW[nTarget] != F.nWidth || nLastH[nTarget] != F.nHeight)
		{
			for (unsigned y = 0; y < H; y++)
			{
				memset (pPage + (size_t) y * m_nPitchPixels, 0, W * 4);
			}
			nLastW[nTarget] = F.nWidth;
			nLastH[nTarget] = F.nHeight;
		}

		int nPrevSY = -1;
		for (unsigned y = 0; y < dh; y++)
		{
			int sy = (int) (y * F.nHeight / dh);
			if (sy != nPrevSY)
			{
				const u32 *pSrc = F.pPixels + (size_t) sy * F.nWidth;
				for (unsigned x = 0; x < dw; x++)
				{
					Row[x] = pSrc[XMap[x]];
				}
				nPrevSY = sy;
			}
			memcpy (pPage + (size_t) (oy + y) * m_nPitchPixels + ox, Row, dw * 4);
		}

		if (m_bDoubleBuffered)
		{
			m_pFrameBuffer->SetVirtualOffset (0, nTarget * H);
			m_pFrameBuffer->WaitForVerticalSync ();
			nPage = nTarget;
		}
	}
}

// ---------------------------------------------------------------------------
// USB input
// ---------------------------------------------------------------------------

void CKernel::UpdateUSB (void)
{
	if (!m_bUSBOK || !m_USBHCI.UpdatePlugAndPlay ())
	{
		return;
	}

	if (m_pKeyboard == 0)
	{
		CUSBKeyboardDevice *pKbd = (CUSBKeyboardDevice *) m_DeviceNameService.GetDevice ("ukbd1", FALSE);
		if (pKbd != 0)
		{
			pKbd->RegisterRemovedHandler (KeyboardRemovedHandler);
			pKbd->RegisterKeyStatusHandlerRaw (KeyStatusHandlerRaw);
			m_pKeyboard = pKbd;
			m_Logger.Write (FromKernel, LogNotice, "USB keyboard attached");
		}
	}

	if (m_pMouse == 0)
	{
		CMouseDevice *pMouse = (CMouseDevice *) m_DeviceNameService.GetDevice ("mouse1", FALSE);
		if (pMouse != 0)
		{
			pMouse->RegisterRemovedHandler (MouseRemovedHandler);
			pMouse->RegisterStatusHandler (MouseStatusHandler);
			m_pMouse = pMouse;
			m_Logger.Write (FromKernel, LogNotice, "USB mouse attached");
		}
	}

	if (m_pGamePad == 0)
	{
		CUSBGamePadDevice *pPad = (CUSBGamePadDevice *) m_DeviceNameService.GetDevice ("upad", 1, FALSE);
		if (pPad != 0)
		{
			pPad->RegisterRemovedHandler (GamePadRemovedHandler);
			pPad->RegisterStatusHandler (GamePadStatusHandler);
			m_pGamePad = pPad;
			s_bPadPresent = true;
			m_Logger.Write (FromKernel, LogNotice, "USB gamepad attached");
		}
	}
}

static void KeyPush (bool bDown, u8 ucUsage)
{
	int nCode = hid_to_linux_keycode (ucUsage);
	if (nCode == 0)
	{
		return;
	}
	unsigned nNext = (s_nKeyHead + 1) % KEYQ_SIZE;
	if (nNext == s_nKeyTail)
	{
		return;			// queue full: drop
	}
	s_KeyQ[s_nKeyHead].down = bDown;
	s_KeyQ[s_nKeyHead].code = (u8) nCode;
	DataMemBarrier ();
	s_nKeyHead = nNext;
}

// Turns 8-byte boot-protocol reports into individual make/break events.
void CKernel::KeyStatusHandlerRaw (unsigned char ucModifiers, const unsigned char RawKeys[6])
{
	static unsigned char s_ucLastMod;
	static unsigned char s_LastKeys[6];

	// modifiers -> usages 0xE0..0xE7
	unsigned char ucChanged = ucModifiers ^ s_ucLastMod;
	for (unsigned i = 0; i < 8; i++)
	{
		if (ucChanged & (1 << i))
		{
			KeyPush ((ucModifiers >> i) & 1, 0xE0 + i);
		}
	}
	s_ucLastMod = ucModifiers;

	// released keys
	for (unsigned i = 0; i < 6; i++)
	{
		u8 k = s_LastKeys[i];
		if (k < 4) continue;		// 0 = none, 1..3 = error codes
		if (!memchr (RawKeys, k, 6)) KeyPush (false, k);
	}
	// pressed keys
	for (unsigned i = 0; i < 6; i++)
	{
		u8 k = RawKeys[i];
		if (k < 4) continue;
		if (!memchr (s_LastKeys, k, 6)) KeyPush (true, k);
	}
	memcpy (s_LastKeys, RawKeys, 6);
}

void CKernel::KeyboardRemovedHandler (CDevice *pDevice, void *pContext)
{
	s_pThis->m_pKeyboard = 0;
}

void CKernel::MouseStatusHandler (unsigned nButtons, int nDX, int nDY, int nWheel)
{
	s_nMouseDX += nDX;
	s_nMouseDY += nDY;
	s_nMouseDZ += nWheel;
	s_nMouseButtons = nButtons;
	s_bMouseEvent = true;
}

void CKernel::MouseRemovedHandler (CDevice *pDevice, void *pContext)
{
	s_pThis->m_pMouse = 0;
}

void CKernel::GamePadStatusHandler (unsigned nDeviceIndex, const TGamePadState *pState)
{
	int x = 0, y = 0;
	if (pState->naxes >= 2)
	{
		for (int a = 0; a < 2; a++)
		{
			int lo = pState->axes[a].minimum, hi = pState->axes[a].maximum;
			int v = pState->axes[a].value, span = hi - lo;
			int d = 0;
			if (span > 0)
			{
				if (v < lo + span / 4) d = -1;
				else if (v > hi - span / 4) d = 1;
			}
			if (a == 0) x = d; else y = d;
		}
	}
	unsigned b = pState->buttons;
	if (b & GamePadButtonLeft)  x = -1;
	if (b & GamePadButtonRight) x = 1;
	if (b & GamePadButtonUp)    y = -1;
	if (b & GamePadButtonDown)  y = 1;

	u8 btn = 0;
	if (b & (GamePadButtonA | GamePadButtonY)) btn |= 1;
	if (b & (GamePadButtonB | GamePadButtonX)) btn |= 2;

	s_nPadX = x;
	s_nPadY = y;
	s_nPadButtons = btn;
}

void CKernel::GamePadRemovedHandler (CDevice *pDevice, void *pContext)
{
	s_pThis->m_pGamePad = 0;
	s_bPadPresent = false;
}

// ---------------------------------------------------------------------------
// SD card
// ---------------------------------------------------------------------------

bool CKernel::DiskRead (u32 nSector, u32 nCount, void *pBuffer)
{
	size_t nBytes = (size_t) nCount * 512;
	if (m_EMMC.Seek ((u64) nSector * 512) != (u64) nSector * 512)
	{
		return false;
	}
	return m_EMMC.Read (pBuffer, nBytes) == (int) nBytes;
}

bool CKernel::DiskWrite (u32 nSector, u32 nCount, const void *pBuffer)
{
	size_t nBytes = (size_t) nCount * 512;
	if (m_EMMC.Seek ((u64) nSector * 512) != (u64) nSector * 512)
	{
		return false;
	}
	return m_EMMC.Write (pBuffer, nBytes) == (int) nBytes;
}

u32 CKernel::DiskSectors (void)
{
	return (u32) m_nDiskSectors;
}

// ---------------------------------------------------------------------------
// port_hal.h implementation
// ---------------------------------------------------------------------------

extern "C" {

uint64_t port_time_us (void)
{
	return CTimer::GetClockTicks64 ();	// BCM2711 system timer, 1 MHz
}

void port_sleep_us (uint64_t us)
{
	if (us >= 1000)
	{
		CTimer::Get ()->MsDelay ((unsigned) (us / 1000));
		us %= 1000;
	}
	if (us)
	{
		CTimer::SimpleusDelay ((unsigned) us);
	}
}

// newlib has no usleep(); the CPU core calls it while the guest is halted.
int usleep (unsigned long us)
{
	if (us)
	{
		CTimer::SimpleusDelay ((unsigned) us);
	}
	return 0;
}

void port_reboot (void)
{
	CTimer::Get ()->MsDelay (100);
	reboot ();
}

const char *port_platform_name (void)
{
	return "Raspberry Pi 4 (bare metal)";
}

bool port_disk_init (void)
{
	return CKernel::Get ()->DiskSectors () != 0;
}

bool port_disk_read (uint32_t sector, uint32_t count, void *buf)
{
	return CKernel::Get ()->DiskRead (sector, count, buf);
}

bool port_disk_write (uint32_t sector, uint32_t count, const void *buf)
{
	return CKernel::Get ()->DiskWrite (sector, count, buf);
}

uint32_t port_disk_sector_count (void)
{
	return CKernel::Get ()->DiskSectors ();
}

void port_disk_sync (void)
{
	// EMMC writes are synchronous.
}

void port_video_present (const uint32_t *fb, int stride, int sx, int sy, int sw, int sh)
{
	if (sw > FRAME_MAX_W) sw = FRAME_MAX_W;
	if (sh > FRAME_MAX_H) sh = FRAME_MAX_H;

	TFrame &F = s_Frames[s_nWriteIdx];
	const uint32_t *pSrc = fb + (size_t) sy * stride + sx;
	for (int y = 0; y < sh; y++)
	{
		memcpy (F.pPixels + (size_t) y * sw, pSrc + (size_t) y * stride, (size_t) sw * 4);
	}
	F.nWidth = sw;
	F.nHeight = sh;

	s_nWriteIdx = __atomic_exchange_n (&s_nMiddle, s_nWriteIdx | FRAME_NEW, __ATOMIC_ACQ_REL) & 3;
}

bool port_poll_key (int *is_down, int *keycode)
{
	static u64 s_nLastUSB;
	u64 nNow = CTimer::GetClockTicks64 ();
	if (nNow - s_nLastUSB > 20000)		// USB plug-and-play every 20 ms
	{
		s_nLastUSB = nNow;
		CKernel::Get ()->UpdateUSB ();
	}

	if (s_nKeyTail == s_nKeyHead)
	{
		return false;
	}
	DataMemBarrier ();
	*is_down = s_KeyQ[s_nKeyTail].down;
	*keycode = s_KeyQ[s_nKeyTail].code;
	s_nKeyTail = (s_nKeyTail + 1) % KEYQ_SIZE;
	return true;
}

bool port_poll_mouse (int16_t *dx, int16_t *dy, int8_t *dz, uint8_t *buttons)
{
	if (!s_bMouseEvent)
	{
		return false;
	}
	EnterCritical ();
	int x = s_nMouseDX, y = s_nMouseDY, z = s_nMouseDZ;
	unsigned b = s_nMouseButtons;
	s_nMouseDX = s_nMouseDY = s_nMouseDZ = 0;
	s_bMouseEvent = false;
	LeaveCritical ();

	if (x > 32767) x = 32767; else if (x < -32768) x = -32768;
	if (y > 32767) y = 32767; else if (y < -32768) y = -32768;
	if (z > 127) z = 127; else if (z < -127) z = -127;

	*dx = (int16_t) x;
	*dy = (int16_t) y;
	*dz = (int8_t) z;
	// Circle: bit0 left, bit1 right, bit2 middle — same as PS/2
	*buttons = (uint8_t) (b & 7);
	return true;
}

bool port_poll_joystick (int *x, int *y, uint8_t *buttons)
{
	if (!s_bPadPresent)
	{
		return false;
	}
	*x = s_nPadX;
	*y = s_nPadY;
	*buttons = s_nPadButtons;
	return true;
}

}	// extern "C"
