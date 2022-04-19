#pragma once

#include <Windows.h>

#define WinAssert(cond, text) ((cond)||(WinAssertUtility::ShowWinPopupError(text),false))	// Will automatically include Windows error text obtained from GetLastError() if the assert fails.

namespace WinAssertUtility
{
	void ShowWinPopupError(LPCSTR lpszFunction);
};