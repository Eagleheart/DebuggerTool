#include "DebugHandler.h"

#include <format>
#include <QTimer>
#include <regex>

// These are debug commands that the debuggee program can send to this debugger. 
// Ensure these match up with the op codes used for the same commands in DebuggerCmds.asm in DummyProgram.sln
enum DbgCmd {
	debuggerCmdNop				= 0,
	debuggerCmdSetCallbacks		= 1,
	debuggerCmdRegisterAltStack = 2,
};

// These are functions in the debuggee that we can call from this debug handler.
// Ensure they match up with the DebugCmdCallbacks struct there.
struct Callbacks
{
	unsigned __int64 PrintAAA = 0;
	unsigned __int64 ReturnDoubleTheInput = 0;
};
static Callbacks s_Callbacks;

void DebugHandler::StartButtonPressed()
{
	static char dummyStr[] = "DummyProgram.exe";
	m_DummyProc.Start(dummyStr, false, true);

	std::string cdbStr(std::format("C:\\Program Files (x86)\\Windows Kits\\10\\Debuggers\\x64\\cdb.exe -g -o -p {}", m_DummyProc.GetProcessId()));
	m_CdbProc.Start(cdbStr.data(), true, false);

	// Sets the QTimer to call DebugHandler::DebugUpdate every 1 ms.
	QTimer* const timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, QOverload<>::of(&DebugHandler::DebugUpdate));
	timer->start(1);
}

void DebugHandler::StopButtonPressed()
{
	m_CdbProc.Stop();
	m_DummyProc.Stop();

	m_FirstPrompt = true;
	m_AltStackLocation = 0;
}

std::string DebugHandler::GetLogData()
{
	std::scoped_lock lock(m_DataLock);

	std::string buffer = m_DataBuffer;
	m_DataBuffer.clear();

	return buffer;
}

void DebugHandler::DebugUpdate()
{
	enum PatternType
	{
		PATTERN_TYPE_NEWLINE = 0,
		PATTERN_TYPE_PROMPT,
		PATTERN_COUNT
	};

	std::string out;
	int patternIndex;

	// Read and return true if we hit either a newline or cdb prompt.
	// The prompt format is an optional one or more numbers followed by a colon, then a definite one or more numbers followed by >
	if (m_CdbProc.Read(out, { std::regex(".*\\n"), std::regex("^(?:[0-9]+:)?[0-9]+> $") }, PATTERN_COUNT, patternIndex))
	{
		{
			// Echo everything we read to the log buffer.
			std::scoped_lock lock(m_DataLock);
			m_DataBuffer += out;
		}

		if (m_OnLineRead)
		{
			// The callback will return true if m_OnLineRead should be cleared.
			if (m_OnLineRead(out))
			{
				m_OnLineRead = nullptr;
			}
		}
		else if (patternIndex == PATTERN_TYPE_PROMPT)
		{
			if (m_FirstPrompt)
			{
				// Just continue if it's the first prompt that is sent on connection.
				m_FirstPrompt = false;
				WriteToCdbProc("g\n");
			}
			else if (m_OnPrompt)
			{
				// The callback might set m_OnPrompt, so null it out prior to calling the callback.
				std::function<void()> func(m_OnPrompt);
				m_OnPrompt = nullptr;
				func();
			}
			else
			{
				HandlePrompt();
			}
		}
	}
}

void DebugHandler::HandlePrompt()
{
	m_OnLineRead = [this](const std::string line) -> bool
	{
		//Example db output:
		//00007ff6`6ce72589  cc eb 05 44 43 4d 44 01                          ...DCMD. 
		//The printout will start with the memory location we are printing the memory of, so match that.
		//Then match the int 3 (cc) that we will be on, the jmp after that (eb), and the other two bytes (jmp offset operand) between that and the DMCD (44 43 4d 44) we've planted to identify a debug command in the debuggee assembly function corresponding to each dbg command.
		//The final two bytes are the opcode, which will be matched in HandleDbgCmd to identify which dbg command this is.
		if (std::regex_search(line, std::regex("^........`........  cc eb .. 44 43 4d 44 ")))
		{
			HandleDbgCmd(line);
		}
		else
		{
			// Unidentified break, since it was not a DbgCmd. Print the stack and go unhandled.
			m_OnPrompt = [this]
			{
				m_OnLineRead = [this](const std::string line) -> bool
				{
					if (line.find("No runnable debuggees error"))
					{
						LogMessage("The application has exited!");
						StopButtonPressed();
					}

					return true;
				};

				WriteToCdbProc("kn; gn\n");
			};
		}

		return true;
	};

	// We need to check the contents of rip to see if the debuggee is firing a debug command, and if so, which one it is (since the opcode for them is stored inline in the assembly functions).
	WriteToCdbProc("db @rip L8\n");
}

void DebugHandler::WriteToCdbProc(const char* const string)
{
	if (m_CdbProc.Write(string))
	{
		// Echo everything we write to the log buffer.
		std::scoped_lock lock(m_DataLock);
		m_DataBuffer += string;
	}
}

void DebugHandler::HandleDbgCmd(const std::string line)
{
	// The opcode will start at index 40 (see comment in HandlePrompt for an explanation of what comes before)
	const unsigned opCode = std::stoul(line.substr(40, 2), nullptr, 16);
	switch (opCode)
	{
		case debuggerCmdNop:
		{
			m_OnPrompt = [this]
			{
				WriteToCdbProc("gh\n");
				LogMessage("Processed a nop!\n");
			};
			break;
		}
		case debuggerCmdSetCallbacks:
		{
			m_OnPrompt = std::bind(&DebugHandler::HandleDbgCmdSetCallbacks, this);
			break;
		}
		case debuggerCmdRegisterAltStack:
		{
			m_OnPrompt = std::bind(&DebugHandler::HandleDbgCmdRegisterAltStack, this);
			break;
		}
	}
}

void DebugHandler::HandleDbgCmdSetCallbacks()
{
	m_OnLineRead = [this](const std::string line) -> bool
	{
		//r command outputs register value in hex in the format:
		//rcx=0000000000000000
		//So, the address will start at index 4 and be 16 chars long.
		//rcx stores the first param passed in, so obtain that to get the address of the callback struct in the debuggee application.
		const std::string address = line.substr(4, 16);
		m_OnLineRead = [this, address](const std::string line) -> bool
		{
			// Rdx stores the second param passed in; this will be the number of callbacks available which should match our s_Callbacks struct.
			// Ignoiring count for the purposes of this example, but it could be used as a version check to only set callbacks certain versions of the program supports.
			const unsigned count = std::atoi(line.substr(4, 16).c_str());
			m_OnPrompt = [this, address, count]
			{
				m_OnLineRead = [this, count](const std::string line) -> bool
				{
					if (count > 0)
					{
						//example dq output:
						//00007ff6`6ce7d170  00007ff6`6ce72200 00007ff6`6ce72260 00007ff6`6ce71055
						//the first address is the memory location we are printing values from; after the two spaces will be the pointers stored there
						//each additional address will have another address and another space to skip past
						const size_t ADDRESS_LENGTH = 17; // length of an address with the ' char in the middle will be 17
						size_t numAddresses = 1; // start at 1 to access after the memory location address
						size_t numSpaces = 2; // start at 2 to access after the 2 spaces

						//use the erase call to remove the ' char in the middle of each address
						const int BASE = 16;
						s_Callbacks.PrintAAA = std::stoull(line.substr((ADDRESS_LENGTH * numAddresses++) + numSpaces++, ADDRESS_LENGTH).erase(8, 1).c_str(), nullptr, BASE);
						s_Callbacks.ReturnDoubleTheInput = std::stoull(line.substr((ADDRESS_LENGTH * numAddresses++) + numSpaces++, ADDRESS_LENGTH).erase(8, 1).c_str(), nullptr, BASE);

						LogMessage("Callbacks have been set!\n");
					}
					else
					{
						LogMessage("Error setting callbacks! Count value is below 0!\n");
					}

					return true;
				};

				m_OnPrompt = [this]
				{
					LogMessage("Firing callback PrintAAA!\n");
					FireCallback(s_Callbacks.PrintAAA, [this](unsigned __int64)
					{
						LogMessage("Firing callback ReturnDoubleTheInput!\n");
						const int valueToDouble = 7;
						FireCallback(s_Callbacks.ReturnDoubleTheInput, [this, valueToDouble](unsigned __int64 retValue)
						{
							LogMessage(std::format("Double the value of {} is {}!\n", valueToDouble, retValue).c_str());
							WriteToCdbProc("gh\n");
						}, valueToDouble);
					});
				};

				// Get the values of the callback addresses printed out.
				WriteToCdbProc((std::string("dq /c3 ") + address + std::string(" L3\n")).c_str());
			};

			return true;
		};

		return false;
	};

	// Print out the values of rcx and rdx. This call expects to receive the location of the struct storing pointers to the callback functions in the debuggee application and its count as params, which are stored in these two registers.
	WriteToCdbProc("r rcx;r rdx\n");
}

void DebugHandler::HandleDbgCmdRegisterAltStack()
{
	m_OnLineRead = [this](const std::string line) -> bool
	{
		//r command outputs register value in hex in the format:
		//rcx=0000000000000000
		//So, the address will start at index 4 and be 16 chars long.
		//rcx stores the first param passed in, so obtain that to get the address of the static char array in the debuggee application.
		const std::string address = line.substr(4, 16);

		std::istringstream addressStream(address);
		addressStream >> std::hex >> m_AltStackLocation;
		LogMessage("The alternate stack location has been set!\n");

		m_OnPrompt = [this]
		{
			WriteToCdbProc("gh\n");
		};

		return true;
	};

	// Print out the values of rcx. This call expects to receive the location of the static char array used for the new stack location in the debuggee application as a param, which is stored in this register.
	WriteToCdbProc("r rcx\n");
}

bool MatchRegister(const std::string line, std::string& contextStorage, const std::string registerString)
{
	std::regex registerRegex(".*" + registerString + "=([0-9a-f]+).*\\n");
	if (std::regex_match(line, registerRegex))
	{
		contextStorage = std::string(std::regex_replace(line, registerRegex, "$1"));
		return true;
	}

	return false;
}

bool MatchXmmRegister(const std::string line, std::string& contextStorageHigh, std::string& contextStorageLow, const std::string registerString)
{
	std::regex registerRegex(registerString + "=([0-9a-f]+) ([0-9a-f]+)\\n");
	if (std::regex_match(line, registerRegex))
	{
		contextStorageHigh = std::string(std::regex_replace(line, registerRegex, "$1"));
		contextStorageLow = std::string(std::regex_replace(line, registerRegex, "$2"));
		return true;
	}

	return false;
}

void DebugHandler::FireCallback(const unsigned __int64 callbackAddress, std::function<void(unsigned __int64)> andThenDo, const unsigned __int64 arg0, const unsigned __int64 arg1, const unsigned __int64 arg2)
{
	m_OnLineRead = [this](const std::string line)
	{
		MatchRegister(line, m_StoredContext.Rax, "rax");
		MatchRegister(line, m_StoredContext.Rcx, "rcx");
		MatchRegister(line, m_StoredContext.Rdx, "rdx");
		MatchRegister(line, m_StoredContext.Rbx, "rbx");
		MatchRegister(line, m_StoredContext.Rsp, "rsp");
		MatchRegister(line, m_StoredContext.Rbp, "rbp");
		MatchRegister(line, m_StoredContext.Rsi, "rsi");
		MatchRegister(line, m_StoredContext.Rdi, "rdi");
		MatchRegister(line, m_StoredContext.R8, "r8");
		MatchRegister(line, m_StoredContext.R9, "r9");
		MatchRegister(line, m_StoredContext.R10, "r10");
		MatchRegister(line, m_StoredContext.R11, "r11");
		MatchRegister(line, m_StoredContext.R12, "r12");
		MatchRegister(line, m_StoredContext.R13, "r13");
		MatchRegister(line, m_StoredContext.R14, "r14");
		MatchRegister(line, m_StoredContext.R15, "r15");
		MatchRegister(line, m_StoredContext.Rip, "rip");
		MatchRegister(line, m_StoredContext.ContextFlags, "efl");

		for (int i = 0; i <= 15; ++i)
		{
			if (MatchXmmRegister(line, m_StoredContext.XmmHigh[i], m_StoredContext.XmmLow[i], "xmm" + std::to_string(i)) && i == 15)
			{
				return true;
			}
		}

		return false;
	};

	m_OnPrompt = [this, callbackAddress, andThenDo, arg0, arg1, arg2]
	{
		m_OnPrompt = [this, andThenDo]
		{
			m_OnLineRead = [this](const std::string line) -> bool
			{
				std::string raxValue = std::regex_replace(line, std::regex(".*rax=([0-9a-f]+).*\\n"), "$1");
				std::istringstream raxStream(raxValue);
				raxStream >> std::hex >> m_CallbackReturnValue;
				return true;
			};

			m_OnPrompt = [this, andThenDo]
			{
				// XMM values must be specified when assigning with the r command in __int64 form.
				unsigned __int64 xmmLows[6];
				unsigned __int64 xmmHighs[6];
				std::istringstream xmmLowStreams[6];
				std::istringstream xmmHighStreams[6];
				for (int i = 0; i <= 5; ++i)
				{
					xmmLowStreams[i] = std::istringstream(m_StoredContext.XmmLow[i]);
					xmmHighStreams[i] = std::istringstream(m_StoredContext.XmmHigh[i]);

					xmmLowStreams[i] >> std::hex >> xmmLows[i];
					xmmHighStreams[i] >> std::hex >> xmmHighs[i];
				}

				// We need to restore the volatile registers. This may seem counterintuitive, but our callback function will naturally restore the nonvolatile registers
				// and since we only simulated a function call, we have to restore the volatile ones manually to keep expected behavior where we were previously, in mid-function.
				WriteToCdbProc((std::string("r rsp=") + m_StoredContext.Rsp +
					";r rip=" + m_StoredContext.Rip +
					";r efl=" + m_StoredContext.ContextFlags +
					";r rcx=" + m_StoredContext.Rcx +
					";r rdx=" + m_StoredContext.Rdx +
					";r r8=" + m_StoredContext.R8 +
					";r r9=" + m_StoredContext.R9 +
					";r r10=" + m_StoredContext.R10 +
					";r r11=" + m_StoredContext.R11 +

					// The xmm values are written low to high for r command assignment, despite being retrieved high to low.
					// Only xmm0-xmm5 are volatile.
					";r xmm0=" + std::to_string(xmmLows[0]) + " " + std::to_string(xmmHighs[0]) +
					";r xmm1=" + std::to_string(xmmLows[1]) + " " + std::to_string(xmmHighs[1]) +
					";r xmm2=" + std::to_string(xmmLows[2]) + " " + std::to_string(xmmHighs[2]) +
					";r xmm3=" + std::to_string(xmmLows[3]) + " " + std::to_string(xmmHighs[3]) +
					";r xmm4=" + std::to_string(xmmLows[4]) + " " + std::to_string(xmmHighs[4]) +
					";r xmm5=" + std::to_string(xmmLows[5]) + " " + std::to_string(xmmHighs[5]) +
					";r rax=" + m_StoredContext.Rax + "\n").c_str());

				m_OnPrompt = [this, andThenDo]
				{
					andThenDo(m_CallbackReturnValue);
				};
			};

			WriteToCdbProc("r rax\n");
		};

		// If an alternate stack location has been set, use that instead of the current stack location.
		unsigned __int64 rspAddress;
		if (m_AltStackLocation)
		{
			rspAddress = m_AltStackLocation;
		}
		else
		{
			std::istringstream originalRSPStream(m_StoredContext.Rsp);
			originalRSPStream >> std::hex >> rspAddress;
		}

		// Win64 ABI requires rsp%16=0, except within a function prologue. Since it is possible we are in the prologue, first align the stack pointer then decrement by 8 to simulate a near call.
		// Subtract another 32-bytes for the parameter home space.
		unsigned __int64 newRsp = (unsigned __int64)((__int64)rspAddress & ~15) - 0x28;

		unsigned __int32 eFlags;
		std::istringstream originalEFLStream(m_StoredContext.ContextFlags);
		originalEFLStream >> std::hex >> eFlags;
		unsigned __int32 newEfl = eFlags & (unsigned __int32)0xfffffbff; // ~0x400, clear RFLAGS.DF (direction flag)

		// Use stringstream to convert from unsigned __int64 to hex format that CDB requires for r command.
		std::stringstream ripStream;
		ripStream << std::hex << callbackAddress;
		std::stringstream rspStream;
		rspStream << std::hex << newRsp;
		std::stringstream eflStream;
		eflStream << std::hex << newEfl;
		std::stringstream rcxStream;
		rcxStream << std::hex << arg0;
		std::stringstream rdxStream;
		rdxStream << std::hex << arg1;
		std::stringstream r8Stream;
		r8Stream << std::hex << arg2;

		// Set rip to the callback address, new rsp and efl values, parameter arguments, and go handled to fire the callback in the debuggee code.
		// Also write 0 to the stack pointer to continue with logic after the callback completes.
		WriteToCdbProc((std::string("r rip=0x") + ripStream.str() +
			";r rsp=0x" + rspStream.str() +
			";r efl=0x" + eflStream.str() +
			";r rcx=0x" + rcxStream.str() +
			";r rdx=0x" + rdxStream.str() +
			";r r8=0x" + r8Stream.str() +
			";eq " + rspStream.str() + " 0" +
			";gh\n").c_str());
	};

	// Get the register values so we can restore them later.
	// The xmm values must be printed out one at a time to get them in a usable format.
	WriteToCdbProc("r;"
		"r xmm0:uq;"
		"r xmm1:uq;"
		"r xmm2:uq;"
		"r xmm3:uq;"
		"r xmm4:uq;"
		"r xmm5:uq;"
		"r xmm6:uq;"
		"r xmm7:uq;"
		"r xmm8:uq;"
		"r xmm9:uq;"
		"r xmm10:uq;"
		"r xmm11:uq;"
		"r xmm12:uq;"
		"r xmm13:uq;"
		"r xmm14:uq;"
		"r xmm15:uq;"
		"\n");
}

void DebugHandler::LogMessage(const char* const message)
{
	std::scoped_lock lock(m_DataLock);
	m_DataBuffer += "DebugHandler: ";
	m_DataBuffer += message;
}