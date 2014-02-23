#include <windows.h>
#include <tchar.h>
#include <mmsystem.h>
#include <cstdint>
#include <random>
#include <algorithm>
#include "resource.h"

class noise_generator
{
public:
	virtual int16_t get() = 0;

protected:
	std::default_random_engine m_engine;
};

class white_noise_generator
	: public noise_generator
{
public:
	white_noise_generator()
		: m_distribution(0.0, 400.0)
	{
	}

	int16_t get() override
	{
		return (int16_t)m_distribution(m_engine);
	}

private:
	std::normal_distribution<double> m_distribution;
};

class brown_noise_generator
	: public noise_generator
{
public:
	brown_noise_generator()
		: m_current(0), m_distribution(0.0, 100.0)
	{
	}

	int16_t get() override
	{
		m_current += m_distribution(m_engine);
		m_current *= 0.98;
		return (int16_t)m_current;
	}

private:
	double m_current;
	std::normal_distribution<double> m_distribution;
};

struct config
{
	enum generator_kind_t { gk_none, gk_white, gk_brown, gk_last };

	generator_kind_t m_kind;

	config()
		: m_kind(gk_brown)
	{
	}

	void load()
	{
		HKEY hKey;
		if (RegOpenKeyEx(HKEY_CURRENT_USER, _T("Software\\Noisegen"), 0, KEY_READ, &hKey) != ERROR_SUCCESS)
			return;

		DWORD dwType;
		DWORD dwKind;
		DWORD dwSize;
		if (RegQueryValueEx(hKey, _T("GeneratorKind"), 0, &dwType, (LPBYTE)&dwKind, &dwSize) == ERROR_SUCCESS
			&& dwType == REG_DWORD
			&& dwSize == sizeof dwKind)
		{
			if (dwKind >= gk_last)
				dwKind = gk_brown;
			m_kind = (generator_kind_t)dwKind;
		}

		RegCloseKey(hKey);
	}

	void save()
	{
		HKEY hKey;
		if (RegCreateKeyEx(HKEY_CURRENT_USER, _T("Software\\Noisegen"), 0, 0, 0, KEY_ALL_ACCESS, 0, &hKey, 0) != ERROR_SUCCESS)
			return;

		DWORD dwKind = m_kind;
		RegSetValueEx(hKey, _T("GeneratorKind"), 0, REG_DWORD, (BYTE const *)&dwKind, sizeof dwKind);

		RegCloseKey(hKey);
	}
};

HWAVEOUT hWaveOut;
int16_t bufs[2][4*1024];
WAVEHDR bufs_hdrs[2] = {};

brown_noise_generator bng;
white_noise_generator wng;

int current = 0;

config cfg;

void fill_buffers()
{
	noise_generator * ng;
	switch (cfg.m_kind)
	{
	case config::gk_white:
		ng = &wng;
		break;
	case config::gk_brown:
		ng = &bng;
		break;
	default:
		return;
	}

	for (;;)
	{
		if ((bufs_hdrs[current].dwFlags & WHDR_DONE) == 0)
			break;
		for (int16_t & sample : bufs[current])
			sample = ng->get();
		waveOutWrite(hWaveOut, &bufs_hdrs[current], sizeof bufs_hdrs[current]);
		current = 1 - current;
	}
}

HMENU hMenu;
#define IDM_BROWN_NOISE 1000
#define IDM_WHITE_NOISE 1001
#define IDM_NO_NOISE 1002

LRESULT CALLBACK MyWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == MM_WOM_DONE)
	{
		fill_buffers();
		return 0;
	}

	if (uMsg == WM_USER + 1)
	{
		// Notify icon
		if (lParam == WM_RBUTTONDOWN)
		{
			POINT pt;
			GetCursorPos(&pt);

			SetForegroundWindow(hwnd);
			TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, hwnd, 0);
			PostMessage(hwnd, WM_NULL, 0, 0);
		}

		return 0;
	}

	if (uMsg == WM_COMMAND)
	{
		switch (LOWORD(wParam))
		{
		case IDCLOSE:
			PostQuitMessage(0);
			break;
		case IDM_BROWN_NOISE:
		case IDM_WHITE_NOISE:
		case IDM_NO_NOISE:
			{
				MENUITEMINFO mii = {};
				mii.cbSize = sizeof mii;
				mii.fMask = MIIM_STATE;
				mii.fState = MFS_UNCHECKED;
				SetMenuItemInfo(hMenu, IDM_BROWN_NOISE, FALSE, &mii);
				SetMenuItemInfo(hMenu, IDM_WHITE_NOISE, FALSE, &mii);
				SetMenuItemInfo(hMenu, IDM_NO_NOISE, FALSE, &mii);

				mii.fState = MFS_CHECKED;
				SetMenuItemInfo(hMenu, LOWORD(wParam), FALSE, &mii);

				switch (LOWORD(wParam))
				{
				case IDM_BROWN_NOISE:
					cfg.m_kind = config::gk_brown;
					break;
				case IDM_WHITE_NOISE:
					cfg.m_kind = config::gk_white;
					break;
				default:
					cfg.m_kind = config::gk_none;
				}

				cfg.save();

				fill_buffers();
			}
			break;
		}
	}

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int CALLBACK WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
	cfg.load();

	HICON hIcon = (HICON)LoadImage(hInst, MAKEINTRESOURCE(IDI_MAIN), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);

	WNDCLASSEX wce = {};
	wce.cbSize = sizeof wce;
	wce.lpfnWndProc = MyWindowProc;
	wce.hInstance = hInst;
	wce.lpszClassName = _T("myclass");
	ATOM hClass = RegisterClassEx(&wce);

	HWND hWindow = CreateWindow(MAKEINTATOM(hClass), _T("mywindow"), 0, 0, 0, 0, 0, HWND_MESSAGE, 0, hInst, 0);

	hMenu = CreatePopupMenu();
	MENUITEMINFO mii = {};
	mii.cbSize = sizeof mii;

	mii.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_STATE | MIIM_ID;
	mii.fType = MFT_RADIOCHECK | MFT_STRING;
	mii.fState = MFS_CHECKED;
	mii.wID = IDM_BROWN_NOISE;
	mii.dwTypeData = _T("Brown Noise");
	InsertMenuItem(hMenu, -1, TRUE, &mii);

	mii.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_STATE | MIIM_ID;
	mii.fType = MFT_RADIOCHECK | MFT_STRING;
	mii.fState = MFS_UNCHECKED;
	mii.wID = IDM_WHITE_NOISE;
	mii.dwTypeData = _T("White Noise");
	InsertMenuItem(hMenu, -1, TRUE, &mii);

	mii.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_STATE | MIIM_ID;
	mii.fType = MFT_RADIOCHECK | MFT_STRING;
	mii.fState = MFS_UNCHECKED;
	mii.wID = IDM_NO_NOISE;
	mii.dwTypeData = _T("Disabled");
	InsertMenuItem(hMenu, -1, TRUE, &mii);

	InsertMenu(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, 0);
	InsertMenu(hMenu, -1, MF_BYPOSITION | MF_STRING, IDCLOSE, _T("Quit"));

	NOTIFYICONDATA nid = {};
	nid.cbSize = sizeof nid;
	nid.hWnd = hWindow;
	nid.uID = 1;
	nid.uFlags = NIF_MESSAGE | NIF_TIP | NIF_ICON;
	nid.uCallbackMessage = WM_USER + 1;
	nid.uVersion = NOTIFYICON_VERSION;
	nid.hIcon = hIcon;
	_tcscpy_s(nid.szTip, _T("Noise generator"));
	Shell_NotifyIcon(NIM_ADD, &nid);

	WAVEFORMATEX wfe;
	wfe.wFormatTag = WAVE_FORMAT_PCM;
	wfe.nChannels = 1;
	wfe.nSamplesPerSec = 44100;
	wfe.wBitsPerSample = 16;
	wfe.nBlockAlign = wfe.nChannels * wfe.wBitsPerSample / 8;
	wfe.nAvgBytesPerSec = wfe.nSamplesPerSec * wfe.nBlockAlign;
	wfe.cbSize = 0;

	MMRESULT res = waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfe, (DWORD_PTR)hWindow, 0, CALLBACK_WINDOW);

	for (size_t i = 0; i < 2; ++i)
	{
		bufs_hdrs[i].lpData = (LPSTR)bufs[i];
		bufs_hdrs[i].dwBufferLength = sizeof bufs[i];
		res = waveOutPrepareHeader(hWaveOut, &bufs_hdrs[i], sizeof bufs_hdrs[i]);
		bufs_hdrs[i].dwFlags |= WHDR_DONE;
	}

	fill_buffers();

	MSG msg;
	for (;;)
	{
		BOOL r = GetMessage(&msg, 0, 0, 0);
		if (r == -1 || r == 0)
			break;

		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	Shell_NotifyIcon(NIM_DELETE, &nid);
	return msg.wParam;
}
	