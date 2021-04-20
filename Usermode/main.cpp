/*
Credits to TheCruZ (https://github.com/TheCruZ/) and iraizo (https://github.com/iraizo/)
*/


#include <Windows.h>
#include <TlHelp32.h>
#include <iostream>
#include <vector>
#include "Driver.h"

#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

#include <d3d9.h>
#pragma comment(lib, "d3d9.lib")

#include <d3dx9.h>
#pragma comment(lib, "d3dx9.lib")

#define OFFSET_ENTITYLIST		    0x18DB438
#define OFFSET_LOCAL_ENT			0x1C8AA98
#define OFFSET_MATRIX				0x1B3BD0
#define OFFSET_RENDER				0x408C968

#define OFFSET_TEAM					0x0450
#define OFFSET_HEALTH				0x0440
#define OFFSET_NAME					0x0589
#define OFFSET_ORIGIN				0x14C
#define OFFSET_BONES				0xF38

struct vec3
{
	float x, y, z;
};

struct bone_t
{
	BYTE pad[0xCC];
	float x;
	BYTE pad2[0xC];
	float y;
	BYTE pad3[0xC];
	float z;
};

struct viewMatrix_t
{
	float matrix[16];
};

HWND overlayWindow;
RECT rc;
IDirect3D9Ex* p_Object;
IDirect3DDevice9Ex* p_Device;
D3DPRESENT_PARAMETERS p_Params;
POINT windowWH;
POINT windowXY;
KernelDriver Driver;

HWND hWnd;
uintptr_t pID;
uintptr_t moduleBase;
uintptr_t localPlayer;
uintptr_t entList;
uintptr_t viewRender;
uintptr_t viewMatrix;
viewMatrix_t vm;

uintptr_t GetPid(const wchar_t* procName)
{
	PROCESSENTRY32 procEntry32;
	uintptr_t pID = 0;

	procEntry32.dwSize = sizeof(PROCESSENTRY32);

	HANDLE hProcSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

	if (hProcSnap == INVALID_HANDLE_VALUE)
		return 0;

	while (Process32Next(hProcSnap, &procEntry32))
	{
		if (!wcscmp(procName, procEntry32.szExeFile))
		{
			pID = procEntry32.th32ProcessID;

			CloseHandle(hProcSnap);
		}
	}

	CloseHandle(hProcSnap);
	return pID;
}

std::vector<uintptr_t> GetPlayers()
{
	std::vector<uintptr_t> vec;
	for (int i = 0; i <= 100; i++)
	{
		uintptr_t ent = Driver.rpm<uintptr_t>(pID, entList + ((uintptr_t)i << 5));
		if (ent == localPlayer || ent == 0) continue;

		// Player check
		if (Driver.rpm<uintptr_t>(pID, ent + OFFSET_NAME) == 125780153691248)
			vec.push_back(ent);
	}
	return vec;
}

void DrawFilledRectangle(int x, int y, int w, int h, D3DCOLOR color)
{
	D3DRECT rect = { x, y, x + w, y + h };
	p_Device->Clear(1, &rect, D3DCLEAR_TARGET, color, 0, 0);
}

void DrawBorderBox(int x, int y, int w, int h, D3DCOLOR color)
{
	DrawFilledRectangle(x - 1, y - 1, w + 2, 1, color);
	DrawFilledRectangle(x - 1, y, 1, h - 1, color);
	DrawFilledRectangle(x + w, y, 1, h - 1, color);
	DrawFilledRectangle(x - 1, y + h - 1, w + 2, 1, color);
}

bool WorldToScreen(vec3 from, float* m_vMatrix, int targetWidth, int targetHeight, vec3& to)
{
	float w = m_vMatrix[12] * from.x + m_vMatrix[13] * from.y + m_vMatrix[14] * from.z + m_vMatrix[15];

	if (w < 0.01f) return false;

	to.x = m_vMatrix[0] * from.x + m_vMatrix[1] * from.y + m_vMatrix[2] * from.z + m_vMatrix[3];
	to.y = m_vMatrix[4] * from.x + m_vMatrix[5] * from.y + m_vMatrix[6] * from.z + m_vMatrix[7];

	float invw = 1.0f / w;
	to.x *= invw;
	to.y *= invw;

	float x = targetWidth / 2;
	float y = targetHeight / 2;

	x += 0.5 * to.x * targetWidth + 0.5;
	y -= 0.5 * to.y * targetHeight + 0.5;

	to.x = x + windowXY.x;
	to.y = y + windowXY.y;
	to.z = 0;

	return true;
}

vec3 GetBonePos(uintptr_t ent, int id)
{
	vec3 pos = Driver.rpm<vec3>(pID, ent + OFFSET_ORIGIN);
	uintptr_t bones = Driver.rpm<uintptr_t>(pID, ent + OFFSET_BONES);
	vec3 bone = {};
	UINT32 boneloc = (id * 0x30);
	bone_t bo = {};
	bo = Driver.rpm<bone_t>(pID, bones + boneloc);

	bone.x = bo.x + pos.x;
	bone.y = bo.y + pos.y;
	bone.z = bo.z + pos.z;
	return bone;
}

bool InitWindow()
{
	// get nvidia overlay
	overlayWindow = FindWindow(L"CEF-OSC-WIDGET", L"NVIDIA GeForce Overlay");
	if (!overlayWindow)
		return false;

	// set window style
	int i = (int)GetWindowLong(overlayWindow, -20);
	SetWindowLongPtr(overlayWindow, -20, (LONG_PTR)(i | 0x20));
	long style = GetWindowLong(overlayWindow, GWL_EXSTYLE);

	// set window trancparency
	MARGINS margin;
	UINT opacity, opacityFlag, colorKey;

	margin.cyBottomHeight = -1;
	margin.cxLeftWidth = -1;
	margin.cxRightWidth = -1;
	margin.cyTopHeight = -1;

	DwmExtendFrameIntoClientArea(overlayWindow, &margin);

	opacityFlag = 0x02;
	colorKey = 0x000000;
	opacity = 0xFF;

	SetLayeredWindowAttributes(overlayWindow, colorKey, opacity, opacityFlag);

	// set window topmost
	SetWindowPos(overlayWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

	ShowWindow(overlayWindow, SW_SHOW);

	return true;
}

bool DirectXInit()
{
	if (FAILED(Direct3DCreate9Ex(D3D_SDK_VERSION, &p_Object)))
		return false;

	GetClientRect(overlayWindow, &rc);

	windowWH = { rc.right - rc.left, rc.bottom - rc.top };
	windowXY = { rc.left, rc.top };

	ZeroMemory(&p_Params, sizeof(p_Params));
	p_Params.Windowed = TRUE;
	p_Params.SwapEffect = D3DSWAPEFFECT_DISCARD;
	p_Params.hDeviceWindow = overlayWindow;
	p_Params.MultiSampleQuality = D3DMULTISAMPLE_NONE;
	p_Params.BackBufferFormat = D3DFMT_A8R8G8B8;
	p_Params.BackBufferWidth = windowWH.x;
	p_Params.BackBufferHeight = windowWH.y;
	p_Params.EnableAutoDepthStencil = TRUE;
	p_Params.AutoDepthStencilFormat = D3DFMT_D16;

	if (FAILED(p_Object->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, overlayWindow, D3DCREATE_HARDWARE_VERTEXPROCESSING, &p_Params, 0, &p_Device)))
		return false;

	return true;
}

void Render()
{
	while (!GetAsyncKeyState(VK_END))
	{
		localPlayer = Driver.rpm<uintptr_t>(pID, moduleBase + OFFSET_LOCAL_ENT);
		viewRender = Driver.rpm<uintptr_t>(pID, moduleBase + OFFSET_RENDER);
		viewMatrix = Driver.rpm<uintptr_t>(pID, viewRender + OFFSET_MATRIX);
		vm = Driver.rpm<viewMatrix_t>(pID, viewMatrix);

		p_Device->Clear(0, 0, D3DCLEAR_TARGET, 0, 1.f, 0);
		p_Device->BeginScene();

		if (Driver.rpm<uintptr_t>(pID, localPlayer + OFFSET_NAME) == 125780153691248)
		{
			for (uintptr_t& player : GetPlayers())
			{
				int health = Driver.rpm<int>(pID, player + OFFSET_HEALTH);
				int teamID = Driver.rpm<int>(pID, player + OFFSET_TEAM);

				if (health < 0 || health > 100 || teamID < 0 || teamID > 32) continue;

				vec3 targetHead = GetBonePos(player, 8);
				vec3 targetHeadScreen;
				if (!WorldToScreen(targetHead, vm.matrix, windowWH.x, windowWH.y, targetHeadScreen)) continue;

				if (targetHeadScreen.x > windowXY.x && targetHeadScreen.y > windowXY.y)
				{
					vec3 targetBody = Driver.rpm<vec3>(pID, player + OFFSET_ORIGIN);
					vec3 targetBodyScreen;
					if (WorldToScreen(targetBody, vm.matrix, windowWH.x, windowWH.y, targetBodyScreen))
					{
						float height = abs(abs(targetHeadScreen.y) - abs(targetBodyScreen.y));
						float width = height / 2.6f;
						float middle = targetBodyScreen.x - (width / 2);
						D3DCOLOR color = D3DCOLOR_ARGB(255, 255, 0, 0);
						if (teamID == Driver.rpm<int>(pID, localPlayer + OFFSET_TEAM))
							color = D3DCOLOR_ARGB(255, 0, 100, 255);

						DrawBorderBox(middle, targetHeadScreen.y, width, height, color);
					}
				}
			}
		}

		p_Device->EndScene();
		p_Device->PresentEx(0, 0, 0, 0, 0);
	}
	return;
}

void DirectXShutdown()
{
	p_Device->Clear(0, 0, D3DCLEAR_TARGET, 0, 1.f, 0);
	p_Device->BeginScene();
	p_Device->EndScene();
	p_Device->PresentEx(0, 0, 0, 0, 0);

	p_Object->Release();
	p_Device->Release();
}

int main()
{
	if (!InitWindow())
	{
		std::cout << "failed to hijack the nvidia overlay" << std::endl;
		system("pause");
		return 0;
	}

	if (!DirectXInit())
	{
		std::cout << "failed to initialize directx on the nvidia overlay" << std::endl;
		system("pause");
		return 0;
	}
	
	while (hWnd == 0)
	{
		hWnd = FindWindow(NULL, L"Apex Legends");
		Sleep(500);
	}

	while (pID == 0)
	{
		pID = GetPid(L"r5apex.exe");
		Sleep(500);
	}

	std::cout << "apex found" << std::endl;

	while (moduleBase == 0)
	{
		moduleBase = Driver.GetModuleBase(pID, L"r5apex.exe");
		Sleep(1000);
	}

	std::cout << "connected to driver" << std::endl;

	std::cout << "success" << std::endl;
	
	Sleep(2000);
	
	FreeConsole();

	entList = moduleBase + OFFSET_ENTITYLIST;

	Render();

	return 0;
}
