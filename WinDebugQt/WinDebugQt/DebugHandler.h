#pragma once

#include <mutex>
#include <string>

#include "IDebugHandler.h"
#include "Process.h"

class DebugHandler : public IDebugHandler
{
public:
	// Runs the dummy application and launches the CDB debugger to attach to it.
	virtual void StartButtonPressed() override;

	// Stops the dummy application and the CDB debugger attached to it.
	virtual void StopButtonPressed() override;

	// Gets additional output since the last time this was called from CDB.
	virtual std::string GetLogData() override;

private:
	// The update loop that runs while a program is being debugged.
	void DebugUpdate();

	// Once the cdb debugger detects a prompt, it is handled here.
	void HandlePrompt();

	// Writes to the stdin pipe of the process being debugged.
	void WriteToCdbProc(const char* const string);

	// Handles a debugger command coming from the debuggee application.
	void HandleDbgCmd(const std::string line);

	// Handles the command to set the callbacks in the debuggee code that can be called.
	void HandleDbgCmdSetCallbacks();

	// Handles the command to set the alt stack location in the debuggee code that can be used as the new stack location when firing debuggee callbacks.
	void HandleDbgCmdRegisterAltStack();

	// Fires one of the callbacks the debuggee application has registered to be callable.
	void FireCallback(const unsigned __int64 callbackAddress, std::function<void(unsigned __int64)> andThenDo, const unsigned __int64 arg0 = 0, const unsigned __int64 arg1 = 0, const unsigned __int64 arg2 = 0);

	// Preps a DebugHandler message to be stored in m_DataBuffer for later log retrieval.
	void LogMessage(const char* const message);

	struct RegisterContext
	{
		std::string Rax;
		std::string Rcx;
		std::string Rdx;
		std::string Rbx;
		std::string Rsp;
		std::string Rbp;
		std::string Rsi;
		std::string Rdi;
		std::string R8;
		std::string R9;
		std::string R10;
		std::string R11;
		std::string R12;
		std::string R13;
		std::string R14;
		std::string R15;

		std::string Rip;
		std::string ContextFlags;

		std::string XmmHigh[16];
		std::string XmmLow[16];
	};

	Process m_DummyProc;
	Process m_CdbProc;

	// Stores the current output data that has not yet been retrieved via GetLogData.
	std::string m_DataBuffer;

	// Ensures m_DataBuffer isn't being written to while we retrieve it.
	std::mutex m_DataLock;

	// A callback to fire when any output line comes through.
	// Returns true if the callback should be removed after being called.
	std::function<bool(const std::string)> m_OnLineRead;

	// A callback to fire when a cdb prompt comes through.
	std::function<void()> m_OnPrompt;

	// Used to bypass the first CDB prompt that comes through on connection to resume the program.
	bool m_FirstPrompt = true;
	
	// Used as the new stack location when firing debuggee callbacks. Prevents callback failures when processing stack overflow exceptions.
	unsigned __int64 m_AltStackLocation = 0;

	// Stores register values so that we can restore them after modifying registers in the debuggee.
	// This can be changed to a stack if calling callbacks within callback handling is desired.
	RegisterContext m_StoredContext;

	// Stores the value being returned by a callback function that was fired in the debuggee.
	unsigned __int64 m_CallbackReturnValue;
};