#include <iostream>
#include <windows.h>

#define STACK_FILL_PATTERN_0x00008   'D', 'E', 'B', 'U', 'G', 'S', 'T', 'K',
#define STACK_FILL_PATTERN_0x00020   STACK_FILL_PATTERN_0x00008 STACK_FILL_PATTERN_0x00008 STACK_FILL_PATTERN_0x00008 STACK_FILL_PATTERN_0x00008
#define STACK_FILL_PATTERN_0x00080   STACK_FILL_PATTERN_0x00020 STACK_FILL_PATTERN_0x00020 STACK_FILL_PATTERN_0x00020 STACK_FILL_PATTERN_0x00020
#define STACK_FILL_PATTERN_0x00200   STACK_FILL_PATTERN_0x00080 STACK_FILL_PATTERN_0x00080 STACK_FILL_PATTERN_0x00080 STACK_FILL_PATTERN_0x00080
#define STACK_FILL_PATTERN_0x00800   STACK_FILL_PATTERN_0x00200 STACK_FILL_PATTERN_0x00200 STACK_FILL_PATTERN_0x00200 STACK_FILL_PATTERN_0x00200
#define STACK_FILL_PATTERN_0x02000	 STACK_FILL_PATTERN_0x00800 STACK_FILL_PATTERN_0x00800 STACK_FILL_PATTERN_0x00800 STACK_FILL_PATTERN_0x00800
#define STACK_FILL_PATTERN_0x08000	 STACK_FILL_PATTERN_0x02000 STACK_FILL_PATTERN_0x02000 STACK_FILL_PATTERN_0x02000 STACK_FILL_PATTERN_0x02000

// Keep these declarations in sync with the DbgCmd enum in DebugHnadler.cpp in the WinDebug solution.
extern void __cdecl debuggerCmdNop(void);
extern void __cdecl debuggerCmdSetCallbacks(void* address, unsigned count);
extern void __cdecl debuggerCmdRegisterAltStack(void* address);

static void PrintAAA()
{
	std::cout << "\nAAA\n";
}

static int ReturnDoubleTheInput(int a)
{
	return a * 2;
}

// Ensure this struct matces up with the Callbacks struct in DebugHnadler.cpp in the WinDebug solution.
struct DebugCmdCallbacks
{
	void(*PRINT_AAA_CALLBACK)();
	int(*RETURN_DOUBLE_THE_INPUT_CALLBACK)(int);
};
static DebugCmdCallbacks s_callbacks;

int main()
{
	Sleep(1000);
	std::cout << "\nINITIALIZING DUMMY PROGRAM!\n";

	static __declspec(align(16)) char altStack[] = { STACK_FILL_PATTERN_0x08000 };
	debuggerCmdRegisterAltStack((void*)((unsigned __int64)altStack + sizeof(altStack) - 16));

	s_callbacks.PRINT_AAA_CALLBACK = PrintAAA;
	s_callbacks.RETURN_DOUBLE_THE_INPUT_CALLBACK = ReturnDoubleTheInput;
	debuggerCmdSetCallbacks(&s_callbacks, sizeof(s_callbacks) / sizeof(void*));
	std::cout << "\nCALLBACKS SET!\n";

	debuggerCmdNop();

	while (true)
	{
	}
}