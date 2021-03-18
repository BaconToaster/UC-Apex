#pragma once
#include <ntifs.h>
#include <ntdef.h>
#include <ntddk.h>
#include <windef.h>
#include "undocumented.h"

VOID HookFunction(PVOID addr);
NTSTATUS Hook(PVOID param);

enum
{
	INST_GETBASE = 0,
	INST_READ
};

typedef struct
{
	ULONG inst;
	ULONG srcPID;
	ULONG targetPID;
	UINT_PTR srcAddr;
	UINT_PTR targetAddr;
	ULONG size;
	const wchar_t* modName;
	PVOID response;
} KREQ, *PKREQ;