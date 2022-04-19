#include "Process.h"

#include <string>

#include "WinAssert.h"

Process::Process(Process&& other) noexcept
	: m_ProcInfo(other.m_ProcInfo),
	m_ChildStdInWr(other.m_ChildStdInWr),
	m_ChildStdOutRd(other.m_ChildStdOutRd),
	m_CurrBufferIndex(other.m_CurrBufferIndex),
	m_Started(other.m_Started)
{
	std::copy(std::begin(other.m_Buffer), std::end(other.m_Buffer), m_Buffer);

	SecureZeroMemory(&other.m_ProcInfo, sizeof(PROCESS_INFORMATION));
	other.m_ChildStdInWr = nullptr;
	other.m_ChildStdOutRd = nullptr;
	other.m_CurrBufferIndex = 0;
	other.m_Buffer[0] = '\0';
	other.m_Started = false;
}

Process& Process::operator=(Process&& other) noexcept
{
	m_ProcInfo = other.m_ProcInfo;
	m_ChildStdInWr = other.m_ChildStdInWr;
	m_ChildStdOutRd = other.m_ChildStdOutRd;
	m_CurrBufferIndex = other.m_CurrBufferIndex;
	std::copy(std::begin(other.m_Buffer), std::end(other.m_Buffer), m_Buffer);
	m_Started = other.m_Started;

	SecureZeroMemory(&other.m_ProcInfo, sizeof(PROCESS_INFORMATION));
	other.m_ChildStdInWr = nullptr;
	other.m_ChildStdOutRd = nullptr;
	other.m_CurrBufferIndex = 0;
	other.m_Buffer[0] = '\0';
	other.m_Started = false;

	return *this;
}

bool Process::Start(char* const launchCommand, const bool redirectInputOutput, const bool showWindow)
{
	if (m_Started)
	{
		return true;
	}

	// Set the bInheritHandle flag so pipe handles are inherited.
	SECURITY_ATTRIBUTES saAttr;
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.bInheritHandle = TRUE;
	saAttr.lpSecurityDescriptor = nullptr;

	HANDLE childStdOutWr = nullptr;
	HANDLE childStdInRd = nullptr;

	if (redirectInputOutput)
	{
		// Create the pipes for communication with the process.
		if (!WinAssert(CreatePipe(&m_ChildStdOutRd, &childStdOutWr, &saAttr, 0), "StdoutRd CreatePipe"))
		{
			return false;
		}
		if (!WinAssert(CreatePipe(&childStdInRd, &m_ChildStdInWr, &saAttr, 0), "Stdin CreatePipe"))
		{
			return false;
		}

		// Ensure these ends of the pipe aren't inherited.
		if (!WinAssert(SetHandleInformation(m_ChildStdOutRd, HANDLE_FLAG_INHERIT, 0), "Stdout SetHandleInformation"))
		{
			return false;
		}
		if (!WinAssert(SetHandleInformation(m_ChildStdInWr, HANDLE_FLAG_INHERIT, 0), "Stdin SetHandleInformation"))
		{
			return false;
		}
	}

	STARTUPINFOA siStartInfo;
	SecureZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
	siStartInfo.cb = sizeof(STARTUPINFO);
	siStartInfo.hStdError = childStdOutWr;
	siStartInfo.hStdOutput = childStdOutWr;
	siStartInfo.hStdInput = childStdInRd;

	if (redirectInputOutput)
	{
		siStartInfo.dwFlags |= STARTF_USESTDHANDLES;
	}

	// Create the process. 
	m_Started = CreateProcessA(nullptr,
		launchCommand,		// command line 
		nullptr,			// process security attributes 
		nullptr,			// primary thread security attributes 
		TRUE,				// handles are inherited 
		showWindow ? 0 : CREATE_NO_WINDOW,	// creation flags 
		nullptr,			// use parent's environment 
		nullptr,			// use parent's current directory 
		&siStartInfo,		// STARTUPINFO pointer 
		&m_ProcInfo);		// receives PROCESS_INFORMATION 

	if (WinAssert(m_Started, "CreateProcessA") && redirectInputOutput)
	{
		// Close handles to the stdin and stdout pipes no longer needed by the child process.
		// If they are not explicitly closed, there is no way to recognize that the child process has ended.
		CloseHandle(childStdOutWr);
		CloseHandle(childStdInRd);
	}

	return m_Started;
}

void Process::Stop()
{
	if (m_Started)
	{
		TerminateProcess(m_ProcInfo.hProcess, 0);
		m_Started = false;
	}

	if (m_ChildStdInWr)
	{
		CloseHandle(m_ChildStdInWr);
		m_ChildStdInWr = nullptr;
	}
	if (m_ChildStdOutRd)
	{
		CloseHandle(m_ChildStdOutRd);
		m_ChildStdOutRd = nullptr;
	}
	if (m_ProcInfo.hThread)
	{
		CloseHandle(m_ProcInfo.hThread);
	}
	if (m_ProcInfo.hProcess)
	{
		CloseHandle(m_ProcInfo.hProcess);
	}

	SecureZeroMemory(&m_ProcInfo, sizeof(PROCESS_INFORMATION));
	m_CurrBufferIndex = 0;
	m_Buffer[0] = '\0';
}

bool Process::Write(const char* const str)
{
	if (m_ChildStdInWr)
	{
		DWORD dwWritten;
		return WinAssert(WriteFile(m_ChildStdInWr, str, (DWORD)strlen(str), &dwWritten, nullptr), "WriteFile");
	}

	return false;
}

bool Process::Read(std::string& outStr, const std::regex (&stopPatterns)[], const int stopPatternCount, int& patternIndexHit)
{
	if (!m_ChildStdOutRd)
	{
		return false;
	}
	
	// First check if there is anything to read.
	DWORD bytesAvailable;
	if (!WinAssert(PeekNamedPipe(m_ChildStdOutRd, nullptr, 0, nullptr, &bytesAvailable, nullptr), "PeekNamedPipe"))
	{
		return false;
	}

	// If there is, concat it to the end of m_Buffer.
	if (bytesAvailable)
	{
		char tempBuf[BUFFER_SIZE];
		DWORD readCount;
		if (!WinAssert(ReadFile(m_ChildStdOutRd, tempBuf, BUFFER_SIZE - 1 - (DWORD)strlen(m_Buffer), &readCount, nullptr), "ReadFile"))
		{
			return false;
		}
		tempBuf[readCount] = '\0';
		strcat_s(m_Buffer, tempBuf);
	}

	// Note m_Buffer may have already had data stored in it from the last read, so continue even if there was no new data.

	// Iterate one char at a time through the new data, to return as soon as we hit a requested regex pattern.
	const size_t newBufferLen = strlen(m_Buffer);
	for (size_t i = m_CurrBufferIndex; i < newBufferLen; ++i)
	{
		// Iterate all the regex patterns to see if any of them are now matched.
		for (int regexIndex = 0; regexIndex < stopPatternCount; ++regexIndex)
		{
			if (std::regex_match(std::begin(m_Buffer), std::begin(m_Buffer) + i + 1, stopPatterns[regexIndex]))
			{
				// If we matched, put everything in the buffer into outStr.
				outStr = m_Buffer;

				// Only leave everything after the spot we matched in m_Buffer.
				strcpy_s(m_Buffer, outStr.substr(i + 1).c_str());

				// Now remove everything after the spot we matched from outStr.
				outStr = outStr.substr(0, i + 1);

				m_CurrBufferIndex = 0;
				patternIndexHit = regexIndex;
				return true;
			}
		}
	}

	// Leave anything still unmatched in m_Buffer, mark the current location, and return no match.
	m_CurrBufferIndex = newBufferLen;
	return false;
}