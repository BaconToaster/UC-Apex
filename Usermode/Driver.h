#pragma once
#include <Windows.h>
#include <iostream>

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
} KREQ, * PKREQ;

class KernelDriver
{
private:
	uintptr_t currentPID;
	void* hookedFunc;

public:
	template<typename ... arg>
	uint64_t CallHook(const arg ... args)
	{
		auto func = static_cast<uint64_t(_stdcall*)(arg...)>(hookedFunc);
		return func(args ...);
	}

	uintptr_t GetModuleBase(uintptr_t pID, const wchar_t* modName)
	{
		hookedFunc = GetProcAddress(LoadLibrary(L"win32u.dll"), "NtOpenCompositionSurfaceSectionInfo");
		currentPID = GetCurrentProcessId();
		KREQ modRequest;
		modRequest.inst = INST_GETBASE;
		modRequest.targetPID = pID;
		modRequest.modName = modName;
		CallHook(&modRequest);

		uintptr_t base = 0;
		base = reinterpret_cast<uintptr_t>(modRequest.response);
		return base;
	}

	bool ReadRaw(uintptr_t pID, UINT_PTR readAddress, UINT_PTR targetAddress, ULONG size)
	{
		KREQ rpmRequest;
		rpmRequest.inst = INST_READ;
		rpmRequest.srcPID = currentPID;
		rpmRequest.targetPID = pID;
		rpmRequest.srcAddr = readAddress;
		rpmRequest.targetAddr = targetAddress;
		rpmRequest.size = size;
		CallHook(&rpmRequest);
		return true;
	}

	template<class type>
	type rpm(uintptr_t pID, UINT_PTR readAddress)
	{
		type tmp;
		if (ReadRaw(pID, readAddress, (UINT_PTR)&tmp, sizeof(type)))
			return tmp;
		else
			return { 0 };
	}
};