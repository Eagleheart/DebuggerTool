_text segment

DCMD_OPS macro opCode:req
 	int 3 ; debug break
	jmp short @F ; jump to the next @@ label
 	db 'D','C','M','D' ; debug command identifier
    db &opCode&
    @@:
    ret
endm

; void __cdecl debuggerCmdNop(void)
?debuggerCmdNop@@YAXXZ proc
DCMD_OPS 0
?debuggerCmdNop@@YAXXZ endp

; void __cdecl debuggerCmdSetCallbacks(void* address, unsigned count)
?debuggerCmdSetCallbacks@@YAXPEAXI@Z proc
DCMD_OPS 1
?debuggerCmdSetCallbacks@@YAXPEAXI@Z endp

; void __cdecl debuggerCmdRegisterAltStack(void* address)
?debuggerCmdRegisterAltStack@@YAXPEAX@Z proc
DCMD_OPS 2
?debuggerCmdRegisterAltStack@@YAXPEAX@Z endp

_text ends

end