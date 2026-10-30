//
// FRANK 386 – Raspberry Pi 4 bare-metal port (Circle)
//
#include "kernel.h"
#include <circle/startup.h>

int main (void)
{
	// CKernel is never destroyed: the emulator runs until reboot.
	static CKernel Kernel;
	if (!Kernel.Initialize ())
	{
		halt ();
		return EXIT_HALT;
	}

	Kernel.Run ();		// never returns

	halt ();
	return EXIT_HALT;
}
