; G8RTOS_SchedulerASM.s
; Holds all ASM functions needed for the scheduler
; Note: If you have an h file, do not have a C file and an S file of the same name

	; Functions Defined
	.def G8RTOS_Start, PendSV_Handler

	; Dependencies
	.ref CurrentlyRunningThread, G8RTOS_Scheduler

	.thumb		; Set to thumb mode
	.align 2	; Align by 2 bytes (thumb mode uses allignment by 2 or 4)
	.text		; Text section

; Need to have the address defined in file 
; (label needs to be close enough to asm code to be reached with PC relative addressing)
RunningPtr: .field CurrentlyRunningThread, 32

; G8RTOS_Start
;	Sets the first thread to be the currently running thread
;	Starts the currently running thread by setting Link Register to tcb's Program Counter
G8RTOS_Start:

	.asmfunc
	ldr r3, RunningPtr	;get the stack pointer and derefence to get the actual stack pointer
	ldr r2, [r3]
	ldr r0, [r2]
	
	add r0, r0, #32		;increment sp to point to where psp should start
	msr psp, r0			;load the psp
	
	str r0, [r2]		;update sp in tcb
	add r0, r0, #24
	ldr lr, [r0]		;increment to pc and load into lr
	
	bx lr				;jump to thread 0 pc
	; Implement this
	.endasmfunc

; PendSV_Handler
; - Performs a context switch in G8RTOS
; 	- Saves remaining registers into thread stack
;	- Saves current stack pointer to tcb
;	- Calls G8RTOS_Scheduler to get new tcb
;	- Set stack pointer to new stack pointer from new tcb
;	- Pops registers from thread stack
PendSV_Handler:
	
	.asmfunc
	mrs r0, psp				;get the proper sp

	ldr r3, RunningPtr		;get the sp in tcb
	ldr r2, [r3]
	 
	stmdb r0!, {r4-r11}		;store r4-r11
	
	str r0, [r2]			;update tcb sp

	bl G8RTOS_Scheduler		;update to next thread
	
	ldr r1, [r3]			;get the sp in tcb
	ldr r0, [r1]
	ldmia r0!, {r4-r11}		;load r4-r11
	str r0, [r1]			;update tcb sp

	msr psp, r0				;update psp

	eor lr, lr, lr			;get ready to jump back in thread
	add lr, lr, #0xFD		;mode using the psp
	add lr, lr, #(0xFF<<8)
	add lr, lr, #(0xFF<<16)
	add lr, lr, #(0xFF<<24)
	bx lr					;jump to next thread
	;Implement this
	.endasmfunc
	
	; end of the asm file
	.align
	.end
