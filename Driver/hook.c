#include "hook.h"

#pragma warning (disable : 4022)

NTSTATUS NTAPI MmCopyVirtualMemory
(
	PEPROCESS SourceProcess,
	PVOID SourceAddress,
	PEPROCESS TargetProcess,
	PVOID TargetAddress,
	SIZE_T BufferSize,
	KPROCESSOR_MODE PreviousMode,
	PSIZE_T ReturnSize
);

NTKERNELAPI PPEB NTAPI PsGetProcessPeb
(
	IN PEPROCESS Process
);

NTSTATUS ZwQuerySystemInformation(
	IN SYSTEM_INFORMATION_CLASS SystemInformationClass,
	OUT PVOID SystemInformation,
	IN ULONG SystemInformationLength,
	OUT PULONG ReturnLength OPTIONAL
);

PVOID NTAPI RtlFindExportedRoutineByName(
	PVOID ImageBase,
	PCCH RoutineName
);

BOOLEAN IsValidAddr(ULONG64 ptr)
{
	ULONG64 min = 0x0001000;
	ULONG64 max = 0x7FFFFFFEFFFF;
	BOOLEAN result = (ptr > min && ptr < max);
	return result;
}

PVOID get_system_module_base(LPCSTR module_name)
{
	ULONG bytes = 0;
	NTSTATUS status = ZwQuerySystemInformation(SystemModuleInformation, NULL, bytes, &bytes);

	if (!bytes)
		return NULL;

	PRTL_PROCESS_MODULES modules = (PRTL_PROCESS_MODULES)ExAllocatePoolWithTag(NonPagedPool, bytes, 'kek');

	status = ZwQuerySystemInformation(SystemModuleInformation, modules, bytes, &bytes);

	if (!NT_SUCCESS(status))
		return NULL;



	PRTL_PROCESS_MODULE_INFORMATION module = modules->Modules;
	PVOID module_base = 0, module_size = 0;

	for (ULONG i = 0; i < modules->NumberOfModules; i++)
	{
		if (!strcmp((char*)module[i].FullPathName, module_name))
		{
			module_base = module[i].ImageBase;
			module_size = (PVOID)module[i].ImageSize;
			break;
		}
	}

	if (modules)
		ExFreePoolWithTag(modules, 0);

	if (module_base <= NULL)
		return NULL;

	return module_base;
}

PVOID get_system_module_export(LPCSTR module_name, LPCSTR routine_name)
{
	PVOID lpModule = get_system_module_base(module_name);

	if (!lpModule)
		return NULL;

	return RtlFindExportedRoutineByName(lpModule, routine_name);
}

BOOL write_to_read_only_memory(PVOID address, PVOID buffer, ULONG size)
{
	PMDL Mdl = IoAllocateMdl(address, size, FALSE, FALSE, NULL);

	if (!Mdl)
		return FALSE;

	MmProbeAndLockPages(Mdl, KernelMode, IoReadAccess);
	PVOID Mapping = MmMapLockedPagesSpecifyCache(Mdl, KernelMode, MmNonCached, NULL, FALSE, NormalPagePriority);
	MmProtectMdlSystemAddress(Mdl, PAGE_READWRITE);
	
	RtlCopyMemory(Mapping, buffer, size);

	MmUnmapLockedPages(Mapping, Mdl);
	MmUnlockPages(Mdl);
	IoFreeMdl(Mdl);

	return TRUE;
}

VOID HookFunction(PVOID addr)
{
	if (!addr)
		return;

	PVOID* function = (PVOID*)(get_system_module_export("\\SystemRoot\\System32\\drivers\\dxgkrnl.sys",
		"NtOpenCompositionSurfaceSectionInfo"));

	if (!function)
		return;

	BYTE orig[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	BYTE shell_code[] = { 0x48, 0xB8 }; // mov rax, xxx
	BYTE shell_code_end[] = { 0xFF, 0xE0 }; //jmp rax

	RtlSecureZeroMemory(&orig, sizeof(orig));
	memcpy((PVOID)((ULONG_PTR)orig), &shell_code, sizeof(shell_code));

	uintptr_t hook_address = (uintptr_t)(addr);

	memcpy((PVOID)((ULONG_PTR)orig + sizeof(shell_code)), &hook_address, sizeof(void*));
	memcpy((PVOID)((ULONG_PTR)orig + sizeof(shell_code) + sizeof(void*)), &shell_code_end, sizeof(shell_code_end));

	write_to_read_only_memory(function, &orig, sizeof(orig));

	return;
}

NTSTATUS Hook(PVOID param)
{
	if (param == NULL)
		return STATUS_UNSUCCESSFUL;

	PKREQ req = (PKREQ)param;

	switch (req->inst)
	{
	case INST_GETBASE:
	{
		PEPROCESS process = 0;
		PVOID result = 0;

		if (NT_SUCCESS(PsLookupProcessByProcessId(req->targetPID, &process)))
		{
			PPEB64 peb = (PPEB64)PsGetProcessPeb(process);

			if (peb > 0)
			{
				PVOID buf = ExAllocatePool(NonPagedPool, wcslen(req->modName) * sizeof(wchar_t) + 1);
				if (buf != NULL)
				{
					memcpy(buf, req->modName, wcslen(req->modName) * sizeof(wchar_t) + 1);

					KAPC_STATE state;
					KeStackAttachProcess(process, &state);

					// InLoadOrderLinks will have main executable first, ntdll.dll second, kernel32.dll
					for (PLIST_ENTRY pListEntry = peb->Ldr->InLoadOrderLinks.Flink; pListEntry != &peb->Ldr->InLoadOrderLinks; pListEntry = pListEntry->Flink)
					{
						if (!pListEntry)
							continue;

						PLDR_DATA_TABLE_ENTRY module_entry = CONTAINING_RECORD(pListEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

						UNICODE_STRING unicode_name;
						RtlInitUnicodeString(&unicode_name, buf);

						if (RtlCompareUnicodeString(&module_entry->BaseDllName, &unicode_name, TRUE) == 0)
							result = module_entry->DllBase;
					}

					KeUnstackDetachProcess(&state);

					req->response = result;
				}
			}
		}
		break;
	}

	case INST_READ:
	{
		SIZE_T tmp = 0;
		PEPROCESS proc;
		PEPROCESS srcProc;
		if (NT_SUCCESS(PsLookupProcessByProcessId(req->targetPID, &proc)) && NT_SUCCESS(PsLookupProcessByProcessId(req->srcPID, &srcProc)) && IsValidAddr(req->srcAddr) && IsValidAddr(req->targetAddr))
			MmCopyVirtualMemory(proc, (PVOID)req->srcAddr, srcProc, (PVOID)req->targetAddr, req->size, KernelMode, &tmp);

		break;
	}

	default:
		break;
	}

	return STATUS_SUCCESS;
}