#pragma once

#include <regex>
#include <string>
#include <windows.h>

// A simple OOP RAII implementation of a Windows process started with CreateProcessA.
// Based on non-OOP Microsoft implementation: https://docs.microsoft.com/en-us/windows/win32/procthread/creating-a-child-process-with-redirected-input-and-output

class Process
	final // Marked final as a warning due to non-virtual destructor. If inheritance is wanted, ensure non-virutal destructor is desired or make it virtual.
{
public:
	Process() { SecureZeroMemory(&m_ProcInfo, sizeof(PROCESS_INFORMATION)); }
	Process(const Process&) = delete;
	Process(Process&& other) noexcept;
	Process& operator=(const Process&) = delete;
	Process& operator=(Process&& other) noexcept;
	~Process() { Stop(); }

	/* 
	* Launches a process with the specified command.
	* If redirectInputOutput is true, the input and output pipes will be redirected for communication with this calling process.
	* If redirectInputOutput is false, the Read() and Write() functions will do nothing.
	*/
	bool Start(char* const launchCommand, const bool redirectInputOutput, const bool showWindow);

	// Stops the process and cleans up resources while instance is still in scope. Resets the state of this instance.
	void Stop();

	/*
	* Write the contents of str to the child process' stdin pipe.
	* Note: This will do nothing if process was launched with redirectInputOutput set to false.
	*/
	bool Write(const char* const str);

	/* 
	* Read output from the child process' stdout pipe until it is empty (returns false) or a regex delim pattern is hit (returns true).  
	* If the delim pattern is not hit, anything read in so far will be stored in m_Buffer and outStr will be unmodified.
	* Note: This will do nothing if process was launched with redirectInputOutput set to false.
	*/
	bool Read(std::string& outStr, const std::regex (&stopPatterns)[], const int stopPatternCount, int& patternIndexHit);

	DWORD GetProcessId() const { return m_ProcInfo.dwProcessId; }

private:
	static const int BUFFER_SIZE = 1024;

	PROCESS_INFORMATION m_ProcInfo;
	HANDLE m_ChildStdInWr = nullptr;
	HANDLE m_ChildStdOutRd = nullptr;
	size_t m_CurrBufferIndex = 0; // Index after the last output char written to m_Buffer.
	char m_Buffer[BUFFER_SIZE] = "\0";
	bool m_Started = false;
};