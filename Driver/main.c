#include "main.h"

#pragma warning (disable : 4152)

NTSTATUS DriverEntry(PDRIVER_OBJECT pDriverObject, PUNICODE_STRING pRegistryPath)
{
	UNREFERENCED_PARAMETER(pDriverObject);
	UNREFERENCED_PARAMETER(pRegistryPath);

	HookFunction(&Hook);

	return STATUS_SUCCESS;
}