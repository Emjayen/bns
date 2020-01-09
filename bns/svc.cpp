/*
 * svc.cpp
 *
 */
#include "bns.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <DbgHelp.h>
#pragma comment(lib, "dbghelp.lib")



// Prototypes
uib ConfigureService(uib bUninstall, uib bStart);
void SetServiceState(DWORD State, DWORD ErrorCode);
LONG WINAPI HandleException(_EXCEPTION_POINTERS* pException);

// Service configuration
#define SVC_CFG_NAME      "bns"
#define SVC_CFG_FRIENDLY  "BNS"
#define SVC_CFG_TYPE      SERVICE_WIN32_OWN_PROCESS
#define SVC_CFG_START     SERVICE_AUTO_START
#define SVC_CFG_ERROR     SERVICE_ERROR_NORMAL
#define SVC_CFG_ARG       " -svc"
#define SVC_CFG_RST_DELAY 1000

// Globals
char ImagePath[MAX_PATH];
static HANDLE hLog;
static HMODULE hNT;
static SERVICE_STATUS_HANDLE hSvcStatus;
static SERVICE_STATUS SvcStatus;

// User entry point
uib AppEntry();
void Exit(DWORD ExitCode, uib bRestart);


/*
 * ServiceControlHandler
 *
 */
static DWORD WINAPI ServiceControlHandler(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext)
{
	LOG("Received service ctrl: %u", dwControl);

	switch(dwControl)
	{
		case SERVICE_CONTROL_STOP:
			SetServiceState(SERVICE_STOPPED, NO_ERROR);
	}

	return NO_ERROR;
}



/*
 * SetServiceState
 *
 */
static void SetServiceState(DWORD State, DWORD ErrorCode)
{
	SvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	SvcStatus.dwCurrentState = State;
	SvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
	SvcStatus.dwWin32ExitCode = ErrorCode;
	SvcStatus.dwServiceSpecificExitCode = 0;
	SvcStatus.dwCheckPoint = 0;
	SvcStatus.dwWaitHint = 0;

	if(!SetServiceStatus(hSvcStatus, &SvcStatus))
	{
		LERR("Failed to set service status");
		Exit(NULL, TRUE);
	}
}



/*
 * Exit
 *
 */
static void Exit(DWORD ExitCode, uib bRestart)
{
	CloseHandle(hLog);


	if(!bRestart)
	{
		SetServiceState(SERVICE_STOPPED, ExitCode ? ExitCode : GetLastError());
	}

	ExitProcess(ExitCode);
}



/*
 * ServiceMain
 *
 */
static VOID WINAPI ServiceMain(DWORD argc, LPSTR* pArgs)
{
	LOG("Service entry");
	

	if(!(hSvcStatus = RegisterServiceCtrlHandlerEx(SVC_CFG_NAME, &ServiceControlHandler, NULL)))
	{
		LERR("Failed to register service control handler");
		return;
	}

	SetServiceState(SERVICE_RUNNING, NO_ERROR);

	LOG("Service running.");

	uib Result = AppEntry();

	SetServiceState(SERVICE_STOPPED, (Result ? NO_ERROR : GetLastError()));
}



/*
 * Entry
 *
 */
static DWORD WINAPI Entry()
{
	char Path[MAX_PATH];
	

	// Install exception handler.
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	SetUnhandledExceptionFilter(&HandleException);

	// Used for logging NT-status codes
	hNT = GetModuleHandle("ntdll.dll");

	// Retrieve the path to the process's image.
	GetModuleFileName(NULL, ImagePath, sizeof(ImagePath));

	// Set working directory to the process's path.
	ZeroMemory(Path, sizeof(Path));
	memcpy(Path, ImagePath, PathGetFile(ImagePath)-ImagePath);
	SetCurrentDirectory(Path);
	GetCurrentDirectory(sizeof(Path), Path);

	// System log startup.
	if(!SyslogStartup(CFG_LOG_FILE))
		return FALSE;


	LOG("");
	LOG("--------------------------------------------------------------------------------------------------------");
	LOG("Process startup");
	LOG("  Command-line: %s", GetCommandLine());
	LOG("  Image: %s", ImagePath);
	LOG("  Working: %s", Path);
	LOG("");

#ifdef DEBUG
	return AppEntry();
#endif

	if(!strstri(GetCommandLine(), SVC_CFG_ARG))
	{
		LOG("Installing and starting service");

		if(!ConfigureService(FALSE, TRUE))
		{
			LERR("Failed to install service");
		}
	}
	
	else
	{
		LOG("Service dispatch started");

		static const SERVICE_TABLE_ENTRY ServiceTable[] =
		{
			{ (LPSTR) SVC_CFG_NAME, &ServiceMain },
			{ NULL, NULL }
		};

		if(!StartServiceCtrlDispatcher(ServiceTable))
		{
			LERR("Failed to begin service dispatch");
			return FALSE;
		}

		else
			LOG("Service dispatch started");
	}

	LOG("Exit process");

	return TRUE;
}


/*
 * LdrCallback
 *
 */
int WINAPI WinMain(HINSTANCE hInstance,
				   HINSTANCE hPrevInstance,
				   LPSTR     lpCmdLine,
				   int       nShowCmd)
{
	ExitProcess(Entry());
	return 0xDEADBEEF;
}


/*
 * ConfigureService
 *
 */
static uib ConfigureService(uib bUninstall, uib bStart)
{
	SC_HANDLE hSCM = NULL;
	SC_HANDLE hSvc = NULL;
	char SvcPath[MAX_PATH];
	uib Result = FALSE;

	wsprintf(SvcPath, "\"%s\"" SVC_CFG_ARG, ImagePath);


	if(!(hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS)))
	{
		LERR("Failed to open SCM");
		goto DONE;
	}

	if(!(hSvc = OpenService(hSCM, SVC_CFG_NAME, SC_MANAGER_ALL_ACCESS)) && GetLastError() != ERROR_SERVICE_DOES_NOT_EXIST)
	{
		LERR("Failed to open service");
		goto DONE;
	}

	// Uninstall service
	if(bUninstall)
	{
		if(!hSvc)
		{
			LWARN("Unable to uninstall service. Service does not exist");
			goto DONE;
		}

		if(!DeleteService(hSvc))
		{
			LERR("Failed to delete service");
			goto DONE;
		}
	}

	// Install/update service
	else
	{
		if(!hSvc)
		{
			if(!(hSvc = CreateService(hSCM, SVC_CFG_NAME, SVC_CFG_FRIENDLY, SERVICE_ALL_ACCESS, SVC_CFG_TYPE, SVC_CFG_START, SVC_CFG_ERROR, SvcPath, NULL, NULL, NULL, NULL, NULL)))
			{
				LERR("Failed to create service");
				goto DONE;
			}

			SC_ACTION sca;
			sca.Type = SC_ACTION_RESTART;
			sca.Delay = SVC_CFG_RST_DELAY;

			SERVICE_FAILURE_ACTIONS sfa;
			sfa.dwResetPeriod = INFINITE;
			sfa.lpRebootMsg = NULL;
			sfa.lpCommand = NULL;
			sfa.cActions = 1;
			sfa.lpsaActions = &sca;

			if(!ChangeServiceConfig2(hSvc, SERVICE_CONFIG_FAILURE_ACTIONS, &sfa))
			{
				LERR("Failed to configure service failure actions");
				goto DONE;
			}
		}

		else
		{
			if(!ChangeServiceConfig(hSvc, SVC_CFG_TYPE, SVC_CFG_START, SVC_CFG_ERROR, SvcPath, NULL, NULL, NULL, NULL, NULL, SVC_CFG_FRIENDLY))
			{
				LERR("Failed to update service config");
				goto DONE;
			}
		}

		if(bStart)
		{
			if(!StartService(hSvc, NULL, NULL))
			{
				LERR("Failed to start service");
				goto DONE;
			}
		}

		Result = TRUE;
	}

	
DONE:
	if(hSvc) CloseServiceHandle(hSvc);
	if(hSCM) CloseServiceHandle(hSCM);

	return Result;
}




/*
 * HandleException
 *
 */
static LONG WINAPI HandleException(_EXCEPTION_POINTERS* pException)
{
	char Path[MAX_PATH];
	HANDLE hFile;


	// Create dump file.
	wsprintf(Path, SVC_CFG_NAME "-%u.dmp", GetTickCount());

	if((hFile = CreateFile(Path, GENERIC_ALL, NULL, NULL, CREATE_ALWAYS, NULL, NULL)) == INVALID_HANDLE_VALUE)
	{
		LERR("Failed to create crash dump file");
		goto DONE;
	}

	MINIDUMP_EXCEPTION_INFORMATION mdei;
	mdei.ThreadId = GetCurrentThreadId();
	mdei.ExceptionPointers = pException;
	mdei.ClientPointers = FALSE;

	if(!MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpWithHandleData, &mdei, NULL, NULL))
	{
		LERR("Failed to generate crash dump");
	}


DONE:
	TerminateProcess(GetCurrentProcess(), GetLastError());
	return NULL;
}