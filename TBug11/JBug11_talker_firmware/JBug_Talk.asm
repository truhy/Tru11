; JB assembly file for talkers

; This file may be used to assemble talkers for A, E and F1 variants of the MC68HC11

; This assembly language file will produce talkers identical with the Motorola ones,
; but note that a leading $FF must be added to establish the baud rate.

; Use the conditional assemby commands below to select the type of interrupt
; control mechanism which JBug11 will use to get control of the MCU:

; 27 Jul 2024 - Notes by Truong Hy
; It seems JBug11 doesn't work with USB-to-TTL serial adapters. I've made modifications which enables
; it to work with my own commandline talker application:
;   - my app accepts talker firmware in s-record format, so no need for the binary .BOO or .XOO format
;   - my app sends the sync character $FF for upload, so no need to append that to the talker firmware
;   - my app does not use/wait for the serial break so all USB-to-TTL serial adapters should now work
;   - option added to work without needing the XIRQ/IRQ pins to be pulled up
; Modifications:
;   - Added $02 option for no interrupt
;
; Note, the original developer is John Beatty, I have no idea of the license but I would like to
; keep it as it was.

					; Note: if programming EPROM, in addition to the 12V on XIRQ/VPPE pin, a pull-up resistor on IRQ pin is also required (4.7K or 10K)
IntType  	EQU 	$02		; $00 for a .BOO talker using IRQ. Pull-up resistor on IRQ pin is required (4.7K or 10K)
					; $01 for a .XOO talker using XIRQ.  Pull-up resistor on XIRQ pin is required (4.7K or 10K)
					; $02 for a talker using polling.  Generally does not require pull-up resistors on IRQ/XIRQ pins

; Select where the stack will go:

Stack		EQU	$00ED		; for A and 811E2
;Stack		EQU	$01FF		; for E0, E1, E9
;Stack		EQU	$02FF		; for E20
;Stack		EQU	$03FF		; for F1


RegBase		EQU	$1000		; Base address for control registers
oSCSR		EQU	$2E		; Offset to SCI status register
oSCDR		EQU	$2F		; Offset to SCI data register
oBAUD		EQU	$2B		; Offset to the BAUD register
oSCCR1		EQU	$2C		; Offset to SCI control register 1
oSCCR2		EQU	$2D		; Offset to SCI control register 2
SCSR		EQU	RegBase + oSCSR	; SCI status register
SCDR		EQU	RegBase + oSCDR	; SCI data register


talker_start	EQU	$0000

		ORG	talker_start

; Set the stack pointer SP to a suitable value for the chip

		LDS	#Stack

; Set up the SCI for communication with the host

		LDX	#RegBase
		CLR	oSCCR1,X	; Clear SCCR1, i.e. 1 start, 8 data,
					; 1 stop; and idle-line wake-up

; Load the BAUD and SCCR2 registers. BAUD is loaded with $30 for a communication rate
; of 9612 with an 8MHz crystal. This is the closest available rate to 9600, and quite
; close enough to work with the UART in PC's

; SCCR2 is loaded with either $2C for a .BOO type talker, or $0C for an .XOO one.

; $2C means:
; TIE	Transmit interrupt enable		= 0
; TCIE	Transmit complete interrupt enable	= 0
; RIE	Receive interrupt enable		= 1 for a .BOO talker
; ILIE	Idle line interrupt enable		= 0
; TE	Transmit enable				= 1
; RE	Receive enable				= 1
; RWU	Receiver wake-up			= 0
; SBK	Send break				= 0

; $0C means:
; TIE	Transmit interrupt enable		= 0
; TCIE	Transmit complete interrupt enable	= 0
; RIE	Receive interrupt enable		= 0 for an .XOO talker
; ILIE	Idle line interrupt enable		= 0
; TE	Transmit enable				= 1
; RE	Receive enable				= 1
; RWU	Receiver wake-up			= 0
; SBK	Send break				= 0

#IF IntType == $00
		LDD	#$302C
#ENDIF
#IF IntType == $01
		LDD	#$300C
#ENDIF
#IF IntType == $02
		LDD	#$300C
#ENDIF

		STAA	oBAUD,X		; 9600 baud. $2B is the BAUD register offset

;
		STAB	oSCCR2,X	; See note above

#IF IntType == $00
		LDAA	#$40		; CCR = - X - - - - - -
					; i.e. XIRQ\ disabled, IRQ\ enabled
		TAP			; Transfer to CCR
#ENDIF
#IF IntType == $01
		LDAA	#$10		; CCR = - - - I - - - -
					; i.e. XIRQ\ enabled, IRQ\ disabled
		TAP			; Transfer to CCR
#ENDIF

;
;talker_idle	JMP	talker_idle	; Hang-around loop

sci_srv		LDAA	SCSR		; Load A with the SCI status register
		ANDA	#$20		; AND it with the RDRF mask
					; (receive data register full)
		BEQ	sci_srv		; loop back if RDRF is zero

; Talker code to process received byte

		LDAA	SCDR		; Load A with SCDR, the SCI Data Register

; Echo the received character back to the host in inverted form
; inverted as a safety precaution?

		COMA			; Do a one's complement
		BSR	OutSci		; and echo to host

; The most significant bit of command bytes is used as a flag that what follows is a
; command to read or write the CPU inherent registers. This bit is tested next, by the
; Branch if Plus (BPL) operation, remembering that the command byte has been inverted

		BPL	Inh1		; branch if inherent register command

; Else read byte count from host into ACCB

		BSR	InSci		; Read byte count from host

		XGDX			; Save command & byte count in IX

; Read the high address byte from host into ACCA, then read low address byte into ACCB

		BSR	InSci		; Read
		TBA			; Result returns in B, so move to A
		BSR	InSci		; Read

; Restore (inverted) command byte to A, byte count to B, and save address in IX

		XGDX

; Is the command a 'memory read'?  Check by comparing the (inverted) command with $FE
; This implies original memory read command is $01

		CMPA	#$FE
		BNE	RxSrv1		; Maybe it's a 'memory write' command ?

; Following section reads memory and sends it to the host

TReadMem	LDAA	$00,X		; Fetch byte from memory
		BSR	OutSci		; Send byte to host
		TBA			; Save byte count
		BSR	InSci		; Wait for host acknowledgement (may be any char)
		TAB			; Restore byte count
		INX			; Increment address
		DECB			; Decrement byte count
		BNE	TreadMem	; branch until done
#IF IntType == $00
		RTI			; Return to idle loop or user code
#ENDIF
#IF IntType == $01
		RTI			; Return to idle loop or user code
#ENDIF
#IF IntType == $02
		JMP	sci_srv		; Jump to receive wait loop
#ENDIF

; Is the command a 'memory write'?  Check by comparing the (inverted) command with $BE
; This implies original memory write command is $41

RxSrv1		CMPA	#$BE		; If unrecognised command received simply return
		BNE	NullSrv		; i.e. branch to an RTI

; Following section writes bytes from the host to memory

		TBA			; Save byte count in A

; Read the next byte from the host.  Byte goes into B

TWritMem	BSR	InSci		; Read byte
		STAB	$00,X		; Store it at the next address

; Run a 'wait' loop to allow for external EEPROM.  The value of the LDY operand has to
; be adjusted to allow for the time it takes to program the EEPROM. In the standard
; talker this value is 1.

; The loop takes 7 cycles, so with an 8 MHz crytal and 2 MHz E clock, the loop time
; is 7 * 0.5 µs = 3.5 µs. So for a delay of 5 ms, we need to load IY with
; 5000/3.5 = 1429  This facility is used for the MicroStamp11 'D' series talker

		LDY	#$0001		; Set up wait loop and run
WaitPoll	DEY			; [4]
		BNE	WaitPoll	; [3]

		LDAB	$00,X		; Read stored byte, and
		STAB	SCDR		; echo back to host

		INX			; Increment memory location
		DECA			; Decrement byte count
		BNE	TWritMem	; until all done

#IF IntType == $00
NullSrv		RTI
#ENDIF
#IF IntType == $01
NullSrv		RTI
#ENDIF
#IF IntType == $02
NullSrv		JMP	sci_srv		; Jump to receive wait loop
#ENDIF

; SUBROUTINES TO SEND AND RECEIVE A SINGLE BYTE ***************************************

; InSCI gets the received byte from the host PC via the SCI. Byte is returned in B

InSCI		LDAB	SCSR		; Load B from the SCI status register

; Test B against $0A, %00001010, for a 'break' character being received.  If a 'break'
; character is received, then the OR and/or FE flags will be set

; TDRE	Transmit data register empty	= ?	(? = irrelevent)
; TC	Transmit complete		= ?
; RDRF	Receive data register full	= ?
; IDLE	Idle-line detect		= ?
; OR	Overrun error			= 0
; NF	Noise flag			= ?
; FE	Framing error			= 0
; 0					= ?

		BITB	#$0A		; If break detected, then
		BNE	talker_start	; branch to $0000 - restart talker

; Test B against the RDRF mask, $20, %0010:0000

; TDRE	Transmit data register empty	= ?
; TC	Transmit complete		= ?
; RDRF	Receive data register full	= 1
; IDLE	Idle-line detect		= ?
; OR	Overrun error			= ?
; NF	Noise flag			= ?
; FE	Framing error			= ?
; 0	(always reads zero)		= ?

		ANDB	#$20		; If RDRF not set then
		BEQ	InSci		; listen for char from host

; Read data received from host and return it in B

		LDAB	SCDR
		RTS

;
; OutSCI is the subroutine which transmits a byte from the SCI to the host PC
; Byte to send in A on entry

OutSci		XGDY			; save A and B in IY
OutSci1		LDAA	SCSR		; Load A from the SCI status register

; If TDRE, the Transmit Data Register Empty flag is not set then loop round.
; Not by chance, the TDRE flag is the msb of the SCI status register

		BPL	OutSci1

		XGDY			; Restore A and B
		STAA	SCDR		; Send byte
		RTS

; READING AND WRITING THE CPU INHERENT REGISTERS **************************************

; Now decide which CPU inherent register command was sent.  If command is to read the
; MCU registers then the one's complement of the command will be $7E (command = $81)

Inh1		CMPA	#$7E
		BNE	Inh2		; Maybe a write of the registers?

; READ REGISTERS

Inh1a		TSX			; Store stack pointer in IX
		XGDX			; then to D

; Send stack pointer to host, high byte first. Note that the value sent is SP+1 because
; the TSX command increments SP on transfer to IX

		BSR	OutSci		; Send byte
		TBA
		BSR	OutSci		; Send byte

		TSX			; Again store stack pointer to IX

; Use TReadMem to send 9 bytes on the stack

		LDAB	#$09
		BRA	TReadMem

; If the command was to write MCU registers, then the one's complement of the command
; would be $3E (command = $C1)

Inh2		CMPA	#$3E		; If not $3E then
		BNE	SwiSrv1		; Maybe to service an SWI?

; WRITE REGISTERS

; Get stack pointer from host, high byte first. Note that the host needs to send SP+1
; because the TXS operation will decrement the IX value by 1 on transfer to SP.

		BSR	InSci
		TBA
		BSR	InSci

		XGDX			; Move to IX
		TXS			; and copy to Stack Pointer

; Use TWritMem to get the next nine bytes from the host onto the stack

		LDAA	#$09
		BRA	TWritMem

;
; Breakpoints generated by SWI instructions cause this routine to run
; The code $4A is sent to the host as a signal that a breakpoint has been reached

swi_srv		LDAA	#$4A
		BSR	OutSci

; Now enter idle loop until the acknowledge signal is received from the host (also $4A)

SWIidle		EQU	*

#IF IntType == $00
		CLI			; Enable interrupts
#ENDIF
#IF IntType == $01
		SEI			; Disable interrupts (except XIRQ\)
#ENDIF

		BRA	SWIidle

; If command from host is an acknowledgement of breakpoint ($B5 complemented, = $4A),
; then the stack pointer is unwound 9 places, ie to where it was before the host
; acknowledged the SWI

SwiSrv1		CMPA	#$4A
		BNE	NullSrv		; branch to $0058 (NullSrv). If not
					; $4A then simply return

; HOST SERVICE SWI

		TSX			; Copy stack pointer to IX
		LDAB	#$09		; Load B with 9
		ABX			; Add 9 to IX
		TXS			; Copy IX to the stack pointer

; Send the breakpoint return address to the host, high byte first. Note that the address
; sent is actually the one immediately following the address at which the break occurred.

		LDD	$07,X
		BSR	OutSci
		TBA
		BSR	OutSci

; Alter the value of PC on the return stack to be the address of the SWIidle routine, so
; that after sending the CPU registers to the host the CPU will enter the idle routine

		LDD	#SwiIdle	; Force idle loop on return from breakpoint
					; processing
		STD	$07,X
		BRA	Inh1A		; Return all CPU registers to host

; END OF TALKER CODE ******************************************************************

; Following space is blank

TalkEnd		FCB	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0

; Interrupt pseudo-vectors.  Unlabelled interrupts all point to NullSrv which is an
; RTI instruction

		JMP	sci_srv		; SCI	-> sci_srv
		JMP	NullSrv		; SPI
		JMP	NullSrv		; PAIE
		JMP	NullSrv		; PAO
		JMP	NullSrv		; TO
		JMP	NullSrv		; TOC5
		JMP	NullSrv		; TOC4
		JMP	NullSrv		; TOC3
		JMP	NullSrv		; TOC2
		JMP	NullSrv		; TOC1
		JMP	NullSrv		; TIC3
		JMP	NullSrv		; TIC2
		JMP	NullSrv		; TIC1
		JMP	NullSrv		; RTI
		JMP	NullSrv		; IRQ\

#IF IntType == $00
xirq_jmp	JMP	NullSrv		; XIRQ\	-> Nullsrv
#ENDIF
#IF IntType == $01
xirq_jmp	JMP	sci_srv		; XIRQ\	-> sci_srv
#ENDIF
#IF IntType == $02
xirq_jmp	JMP	sci_srv		; XIRQ\	-> sci_srv
#ENDIF

		JMP	swi_srv		; SWI

swi_jmp		EQU	* - 2		; label refers to address

		JMP	talker_start	; Illegal opcode -> restart

illop_jmp	EQU	* - 2		; label refers to address

		JMP	NullSrv		; COP fail
		JMP	NullSrv		; Clock Monitor fail

; COMMUNICATION FLOW - PC <--> TALKER **********************************************

;	Read Memory Bytes

;	1.	Host sends $01
;	2.	MCU replies with $FE (one's complement of $01)
;	3.	Host sends byte count ($00 to $FF)
;	4.	Host sends high byte of address
;	5.	Host sends low byte of address

;	6.	MCU  sends first byte of memory
;	7.	Host acknowledges with any old byte

;	8.	Repeat 6 & 7 until all bytes read

;	Write Memory bytes

;	1.	Host sends $41
;	2.	MCU replies with $BE (one's complement of $41)
;	3.	Host sends byte count ($00 to $FF)
;	4.	Host sends high byte of address
;	5.	Host sends low byte of address

;	6.	Host sends first byte of memory
;	7.	MCU acknowledges by echoing same byte

;	8.	Repeat 6 & 7 until all bytes sent

;	Read MCU Registers

;	1.	Host sends $81
;	2.	MCU replies with $7E
;	3.	MCU  sends high byte of Stack Pointer	} Note 1
;	4.	MCU  sends low byte of Stack Pointer	}

;	5.	MCU  sends lowest byte on stack
;	6.	Host acknowledges with any old byte

;	7.	Repeat steps 5 & 6 for a total of 9 times
;		Bytes are sent in this order:
;		CCR
;		B
;		A
;		IXH
;		IXL
;		IYH
;		IYL
;		PCH
;		PCL

;	Write MCU Registers

;	1.	Host sends $C1
;	2.	MCU replies with $3E
;	3.	Host sends high byte of Stack Pointer	} Note 2
;	4.	Host sends low byte of Stack Pointer	}

;	5.	Host sends lowest byte on stack
;	6.	MCU acknowledges by echoing same byte

;	7.	Repeat steps 5 & 6 for a total of 9 times
;		Bytes are sent in this order:
;		CCR
;		B
;		A
;		IXH
;		IXL
;		IYH
;		IYL
;		PCH
;		PCL

;
;	Software Interrupt

;	When an SWI is encountered, the MCU transmits the character $4A (ASCII
;	letter 'J'). This triggers JBug11 to make use of the following routine:

;	SWI Service Routine

;	1.	Host sends $B5
;	2.	MCU replies with $4A

;	3.	MCU sends high byte of breakpoint address	} Note 3
;	4.	MCU sends low byte of breakpoint address	}

;	5.	MCU sends high byte of Stack Pointer		} Note 1
;	6.	MCU sends low byte of Stack Pointer		}
;	7.	MCU sends lowest byte on stack
;	8.	Host acknowledges by echoing any old byte

;	9.	Repeat steps 7 & 8 for a total of 9 times
;		Bytes are sent in this order:
;		CCR
;		B
;		A
;		IXH
;		IXL
;		IYH
;		IYL
;		PCH	} Note 4
;		PCL	}

; NOTES

; 1	The MCU sends the actual value of the stack pointer plus 1
; 2	The host must send the desired value of the stack pointer plus 1
; 3	The MCU sends the actual value of the breakpoint plus 1
; 4	The value of PC returned by the SWI service routine is always the address
;	of SwiIdle.