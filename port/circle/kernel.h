//
// FRANK 386 – Raspberry Pi 4 bare-metal port (Circle)
//
// Core assignment:
//   core 0 : emulator (i386 interpreter + PC devices), USB/SD/IRQs
//   core 1 : audio  (mixer, OPL synthesis, feeds HDMI or PWM sound queue)
//   core 2 : video  (scales the guest picture to the HDMI frame buffer)
//   core 3 : idle
//
#ifndef FRANK_KERNEL_H
#define FRANK_KERNEL_H

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/serial.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/memory.h>
#include <circle/multicore.h>
#include <circle/bcmframebuffer.h>
#include <circle/usb/usbhcidevice.h>
#include <circle/usb/usbkeyboard.h>
#include <circle/usb/usbgamepad.h>
#include <circle/input/mouse.h>
#include <circle/input/console.h>
#include <circle/sound/soundbasedevice.h>
#include <SDCard/emmc.h>
#include <circle/types.h>

class CKernel;

class CFrankCores : public CMultiCoreSupport
{
public:
	CFrankCores (CMemorySystem *pMemorySystem, CKernel *pKernel)
	:	CMultiCoreSupport (pMemorySystem), m_pKernel (pKernel) {}

	void Run (unsigned nCore) override;

private:
	CKernel *m_pKernel;
};

class CKernel
{
public:
	CKernel (void);

	boolean Initialize (void);
	void Run (void);			// never returns

	// called on secondary cores
	void AudioLoop (void);
	void VideoLoop (void);

	// HAL helpers (core 0)
	bool DiskRead (u32 nSector, u32 nCount, void *pBuffer);
	bool DiskWrite (u32 nSector, u32 nCount, const void *pBuffer);
	u32  DiskSectors (void);
	void UpdateUSB (void);

	static CKernel *Get (void) { return s_pThis; }

private:
	bool SetupFrameBuffer (void);
	void SetupSound (void);

	static void KeyStatusHandlerRaw (unsigned char ucModifiers, const unsigned char RawKeys[6]);
	static void KeyboardRemovedHandler (CDevice *pDevice, void *pContext);
	static void MouseStatusHandler (unsigned nButtons, int nDX, int nDY, int nWheel);
	static void MouseRemovedHandler (CDevice *pDevice, void *pContext);
	static void GamePadStatusHandler (unsigned nDeviceIndex, const TGamePadState *pState);
	static void GamePadRemovedHandler (CDevice *pDevice, void *pContext);

private:
	// do not change this order
	CActLED			m_ActLED;
	CKernelOptions		m_Options;
	CDeviceNameService	m_DeviceNameService;
	CSerialDevice		m_Serial;
	CExceptionHandler	m_ExceptionHandler;
	CInterruptSystem	m_Interrupt;
	CTimer			m_Timer;
	CLogger			m_Logger;
	CUSBHCIDevice		m_USBHCI;
	CEMMCDevice		m_EMMC;
	CConsole		m_Console;
	CFrankCores		m_Cores;

	CBcmFrameBuffer		*m_pFrameBuffer;
	unsigned		 m_nScreenWidth, m_nScreenHeight, m_nPitchPixels;
	bool			 m_bDoubleBuffered;

	CSoundBaseDevice	*m_pSound;
	bool			 m_bSoundHDMI;

	CUSBKeyboardDevice * volatile m_pKeyboard;
	CMouseDevice * volatile	 m_pMouse;
	CUSBGamePadDevice * volatile m_pGamePad;

	u64			 m_nDiskSectors;
	bool			 m_bUSBOK;

	static CKernel *s_pThis;
};

#endif
