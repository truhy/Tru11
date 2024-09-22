; MIT License
;
; Copyright (c) 2024 Truong Hy
;
; Permission is hereby granted, free of charge, to any person obtaining a copy
; of this software and associated documentation files (the "Software"), to deal
; in the Software without restriction, including without limitation the rights
; to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
; copies of the Software, and to permit persons to whom the Software is
; furnished to do so, subject to the following conditions:
;
; The above copyright notice and this permission notice shall be included in all
; copies or substantial portions of the Software.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
; IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
; FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
; AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
; LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
; OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
; SOFTWARE.

; 03 Aug 2024 - Truong Hy
; Talker firmware to read memory and program EEPROM
;
; Description
; ===========
;
; Enables host computer to control the MCU through the serial port in
; bootstrap mode.  Provides these controls:
; - read memory
; - write normal memory
; - program EEPROM
; - program EPROM
;
; Only need MODA + MODB tied to ground, serial pins TX+RX wired to a TTL serial
; adapter to host.
;
; Commands and communication flow
; ===============================
;
; Read memory command
; 1. Host sends $01
; 2. MCU replies with $01 (echo)
; 3. Host sends byte count. A value from 0 to 255 (Note 0 = 256 bytes)
; 4. Host sends high byte of start read address
; 5. Host sends low byte of start read address
; 6. MCU sends byte of memory, increments read address and decrements byte count
; 7. Repeat from 6 until byte count is zero
;
; Write normal memory (RAM or memory-mapped register) command
; 1. Host sends $02
; 2. MCU replies with $02 (echo)
; 3. Host sends byte count. A value from 0 to 255 (Note 0 = 256 bytes)
; 4. Host sends high byte of start write address
; 5. Host sends low byte of start write address
; 7. MCU replies with byte written (reread)
; 8. MCU increments read address and decrements byte count, repeat from 6 until byte count is zero
;
; Write EEPROM command
; 1. Host sends $03
; 2. MCU replies with $03 (echo)
; 3. Host sends byte count. A value from 0 to 255 (Note 0 = 256 bytes)
; 4. Host sends high byte of start write address
; 5. Host sends low byte of start write address
; 6. Host sends byte of memory
; 7. MCU replies with byte programmed (reread)
; 8. MCU increments read address and decrements byte count, repeat from 6 until byte count is zero
;
; Write EPROM command (excluding MC68HC711E20)
; 1. Host sends $04
; 2. MCU replies with $04 (echo)
; 3. Host sends byte count. A value from 0 to 255 (Note 0 = 256 bytes)
; 4. Host sends high byte of start write address
; 5. Host sends low byte of start write address
; 6. Host sends byte of memory
; 7. MCU replies with byte programmed (reread)
; 8. MCU increments read address and decrements byte count, repeat from 6 until byte count is zero
;
; Write MC68HC711E20 EPROM command
; 1. Host sends $05
; 2. MCU replies with $05 (echo)
; 3. Host sends byte count. A value from 0 to 255 (Note 0 = 256 bytes)
; 4. Host sends high byte of start write address
; 5. Host sends low byte of start write address
; 6. Host sends byte of memory
; 7. MCU replies with byte programmed (reread)
; 8. MCU increments read address and decrements byte count, repeat from 6 until byte count is zero

; Stack options at top of RAM
Stack        EQU $00FF                 ; for A and 811E2
;Stack       EQU $01FF                 ; for E0, E1, E9
;Stack       EQU $02FF                 ; for E20
;Stack       EQU $03FF                 ; for F1

; Counter value for 10ms delay when using 8MHz xtal
; The delay loop (excluding call, setup and return) takes 6 cycles (DEX = 3 & BNE = 3), so with an 8 MHz crytal and 2 MHz E clock (0.5us),
; the loop time is 6 * 0.5us = 3us, so a counter value for a delay of 10 ms is: 10ms*1000/3us = 10000/3 = 3333 (truncated)
DelayAmt     EQU 10000/3

; Register address constants
RegBase      EQU $1000                 ; Base address of memory mapped registers
BAUD_OFS     EQU $2B
SCCR1_OFS    EQU $2C
SCCR2_OFS    EQU $2D
SCSR_OFS     EQU $2E
SCDR_OFS     EQU $2F
BPROT_OFS    EQU $35
PPROG_OFS    EQU $3B
EPROG_OFS    EQU $36
HPRIO_OFS    EQU $3C
CONFIG       EQU $103F

; Bitmasks
TDRE         EQU $80
RDRF         EQU $20
EEByteErase  EQU $16
EEBulkErase  EQU $06
EEByteProg   EQU $02
EByteProg    EQU $20

; Our own address constants
EEOpt        EQU $0000

; Main
; Initialisations
             ORG  $0
             LDS  #Stack               ; Load stack pointer
             LDX  #RegBase             ; Load X register with the base address of memory mapped registers
             CLR  SCCR1_OFS,X          ; SCCR1 register: ($102C) = $00. Together with next few lines, initialise SCI + BAUD registers for 8 data bits, 9600 baud
             LDD  #$300C               ; D register = $300C. A register = $30, B register = $0C
             STAA BAUD_OFS,X           ; Store A into BAUD register: ($102B) = $30 (Set 9612 baud with an 8MHz crystal, good enough to communicate at 9600 baud)
             STAB SCCR2_OFS,X          ; Store B into SCCR2 register: ($102D) = $0C
             CLR  BPROT_OFS,X          ; Clear the block protect register (BPROT), which allows EEPROM programming
             LDAA #$66                 ; A = $66.  Value for HPRIO
             STAA HPRIO_OFS,X          ; HPRIO ($103C) = A.  Switch to Special Test mode, RBOOT = 0, IRV = 0.  This enables config register programming and also access to external memory areas

; Command input loop: Wait for command from host loop
ReadCmd      CLR EEOpt
             BSR ReadEchoSerA
             CMPA #$01
             BEQ ReadMemCmd
             CMPA #$02
             BEQ WriteMemCmd
             CMPA #$03
             BEQ WriteEECmd
             CMPA #$04
             BEQ WriteECmd
             CMPA #$05
             BEQ WriteE20Cmd
             BRA ReadCmd               ; Loop when no command

; Read command: Read memory and send to host
ReadMemCmd   BSR MemParams
ReadMem      LDAA $00,Y                ; Read memory value into A reg
             BSR WriteSerA             ; Send byte to host
             INY                       ; Increment address
             DECB                      ; Decrement byte count
             BNE ReadMem               ; Loop until all bytes done
             JMP ReadCmd

; EEOpt: 3 = E20 EPROM, 2 = EPROM, 1 = EEPROM, 0 = Normal memory

; Write EPROM E20 command
WriteE20Cmd  INC EEOpt

; Write EPROM command
WriteECmd    INC EEOpt

; Write EEPROM command
WriteEECmd   INC EEOpt

; Write command: Receive byte from host then write normal memory or program EEPROM/EPROM
WriteMemCmd  BSR MemParams
WriteMem     BRCLR SCSR_OFS,X,#RDRF,*  ; Wait for receive buffer full
             LDAA SCDR_OFS,X           ; Read byte from host into A register, then below echo back to host
             TST EEOpt
             BEQ NoProg                ; If EEOpt = 0 or negative then NoProg
             JMP Prog                  ; Program byte to EEPROM
NoProg       STAA $00,Y                ; Write to memory
ProgReturn   LDAA $00,Y                ; Reread memory
             BSR WriteSerA             ; Send byte to host
             INY                       ; Increment address
             DECB                      ; Decrement byte count
             BNE WriteMem              ; Loop until all bytes done
             JMP ReadCmd

; Read memory parameters from host
MemParams    BSR ReadSerB              ; Read byte count from host
             XGDY                      ; Save command & byte count to IY reg
             BSR ReadSerB              ; Read high byte of address from host
             TBA                       ; Transfer high byte to A reg
             BSR ReadSerB              ; Read low byte of address from host
             XGDY                      ; Restore command byte to A reg, byte count to B reg, and save address to IY reg
             RTS

; Read serial no echo
ReadSerB     BRCLR SCSR_OFS,X,#RDRF,*  ; Wait for receive buffer full
             LDAB SCDR_OFS,X           ; Read byte from host into B register
             RTS

; Read serial with echo
ReadEchoSerA BRCLR SCSR_OFS,X,#RDRF,*  ; Wait for receive buffer full
             LDAA SCDR_OFS,X           ; Read byte from host into A register, then below echo back to host

; Write serial
WriteSerA    BRCLR SCSR_OFS,X,#TDRE,*  ; Wait for transmit buffer empty
             STAA SCDR_OFS,X           ; Write byte from A register to host
             RTS

; Program EEPROM or EPROM. Y = address, A = byte to program
Prog         PSHB                       ; Save B reg
             LDAB EEOpt
             CMPB #$03
             BEQ DoE20Prog
             CMPB #$02
             BEQ DoEProg
EEErase      LDAB #EEByteErase          ; Set default byte erase mode
             CPY #CONFIG                ; If address is CONFIG then bulk erase
             BNE ProgDefault
             LDAB #EEBulkErase          ; Set bulk erase mode for compatibility with A1, A8 and A2 series
ProgDefault  BSR DoProg                 ; Byte erase or bulk erase + CONFIG
             LDAB #EEByteProg           ; Set program mode
             BSR DoProg                 ; Program byte
ProgExit     PULB                       ; Restore B reg
             JMP ProgReturn
DoEProg      LDAB #EByteProg            ; Set program mode
             BSR DoProg                 ; Program byte
             BRA ProgExit
DoProg       STAB PPROG_OFS,X           ; Enable internal addr/data latches
             STAA $00,Y                 ; Write byte to address
             INC PPROG_OFS,X            ; Enable internal programming voltage
             BSR Delay
             CLR PPROG_OFS,X            ; Disable internal programming voltage and release internal addr/data latches
             RTS
DoE20Prog    LDAB #EByteProg            ; Set program mode
             STAB EPROG_OFS,X           ; Enable internal addr/data latches
             STAA $00,Y                 ; Write byte to address
             INC EPROG_OFS,X            ; Enable internal programming voltage
             BSR Delay
             CLR EPROG_OFS,X            ; Disable internal programming voltage and release internal addr/data latches
             BRA ProgExit
Delay        PSHX
             LDX #DelayAmt              ; Delay amount
Wait         DEX
             BNE Wait
             PULX
             RTS

    END
