#ifndef ESP_PLATFORM

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>

#include "compat_uname.h"

int uname(struct utsname *name)
{
	OSVERSIONINFO versionInfo;
	SYSTEM_INFO sysInfo;

	// Get Windows version info
	ZeroMemory(&versionInfo, sizeof(OSVERSIONINFO));
	versionInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&versionInfo);

	// Get hardware info
	ZeroMemory(&sysInfo, sizeof(SYSTEM_INFO));
	GetSystemInfo(&sysInfo);

	// Set implementation name
	strcpy(name->sysname, "Windows");
	itoa(versionInfo.dwBuildNumber, name->release, 10);
	sprintf(name->version, "%lu.%lu", versionInfo.dwMajorVersion, versionInfo.dwMinorVersion);

	// Set hostname
	if (gethostname(name->nodename, UTSNAME_MAXLENGTH) != 0) 
		return WSAGetLastError();

	// Set processor architecture
	switch (sysInfo.wProcessorArchitecture) 
	{
	case PROCESSOR_ARCHITECTURE_AMD64:
		strcpy(name->machine, "x86_64");
		break;
	case PROCESSOR_ARCHITECTURE_IA64:
		strcpy(name->machine, "ia64");
		break;
	case PROCESSOR_ARCHITECTURE_INTEL:
		strcpy(name->machine, "x86");
		break;
	case PROCESSOR_ARCHITECTURE_ARM:
		strcpy(name->machine, "arm");
		break;
#ifdef PROCESSOR_ARCHITECTURE_ARM64
	case PROCESSOR_ARCHITECTURE_ARM64:
		strcpy(name->machine, "arm64");
		break;
#endif
	case PROCESSOR_ARCHITECTURE_UNKNOWN:
	default:
		strcpy(name->machine, "unknown");
	}

	return 0;
}

#endif // !ESP_PLATFORM