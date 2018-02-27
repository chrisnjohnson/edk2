;------------------------------------------------------------------------------ ;
; Copyright (c) 2016 - 2018, Intel Corporation. All rights reserved.<BR>
; This program and the accompanying materials
; are licensed and made available under the terms and conditions of the BSD License
; which accompanies this distribution.  The full text of the license may be found at
; http://opensource.org/licenses/bsd-license.php.
;
; THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
; WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
;
; Module Name:
;
;   MpFuncs.nasm
;
; Abstract:
;
;   This is the assembly code for Multi-processor S3 support
;
;-------------------------------------------------------------------------------

%pragma macho subsections_via_symbols

SECTION .text

extern ASM_PFX(InitializeFloatingPointUnits)

%define VacantFlag 0x0
%define NotVacantFlag 0xff

%define LockLocation L_RendezvousFunnelProcEnd - L_RendezvousFunnelProcStart
%define StackStart LockLocation + 0x4
%define StackSize LockLocation + 0x8
%define RendezvousProc LockLocation + 0xC
%define GdtrProfile LockLocation + 0x10
%define IdtrProfile LockLocation + 0x16
%define BufferStart LockLocation + 0x1C

;-------------------------------------------------------------------------------------
;RendezvousFunnelProc  procedure follows. All APs execute their procedure. This
;procedure serializes all the AP processors through an Init sequence. It must be
;noted that APs arrive here very raw...ie: real mode, no stack.
;ALSO THIS PROCEDURE IS EXECUTED BY APs ONLY ON 16 BIT MODE. HENCE THIS PROC
;IS IN MACHINE CODE.
;-------------------------------------------------------------------------------------
;RendezvousFunnelProc (&WakeUpBuffer,MemAddress);

BITS 16
global ASM_PFX(RendezvousFunnelProc)
ASM_PFX(RendezvousFunnelProc):
L_RendezvousFunnelProcStart:

; At this point CS = 0x(vv00) and ip= 0x0.

        mov        ax,  cs
        mov        ds,  ax
        mov        es,  ax
        mov        ss,  ax
        xor        ax,  ax
        mov        fs,  ax
        mov        gs,  ax

L_flat32Start:

        mov        si, BufferStart
        mov        edx,dword [si]          ; EDX is keeping the start address of wakeup buffer

        mov        si, GdtrProfile
o32     lgdt       [cs:si]

        mov        si, IdtrProfile
o32     lidt       [cs:si]

        xor        ax,  ax
        mov        ds,  ax

        mov        eax, cr0                    ; Get control register 0
        or         eax, 0x000000001            ; Set PE bit (bit #0)
        mov        cr0, eax

L_FLAT32_JUMP:

a32     jmp   dword 0x20:0x0

BITS 32
L_PMODE_ENTRY:                         ; protected mode entry point

        mov         ax,  0x8
o16     mov         ds,  ax
o16     mov         es,  ax
o16     mov         fs,  ax
o16     mov         gs,  ax
o16     mov         ss,  ax           ; Flat mode setup.

        mov         esi, edx

        mov         edi, esi
        add         edi, LockLocation
        mov         al,  NotVacantFlag
L_TestLock:
        xchg        byte [edi], al
        cmp         al, NotVacantFlag
        jz          L_TestLock

L_ProgramStack:

        mov         edi, esi
        add         edi, StackSize
        mov         eax, dword [edi]
        mov         edi, esi
        add         edi, StackStart
        add         eax, dword [edi]
        mov         esp, eax
        mov         dword [edi], eax

L_Releaselock:

        mov         al,  VacantFlag
        mov         edi, esi
        add         edi, LockLocation
        xchg        byte [edi], al

        ;
        ; Call assembly function to initialize FPU.
        ;
        mov         ebx, ASM_PFX(InitializeFloatingPointUnits)
        call        ebx
        ;
        ; Call C Function
        ;
        mov         edi, esi
        add         edi, RendezvousProc
        mov         eax, dword [edi]

        test        eax, eax
        jz          L_GoToSleep
        call        eax                           ; Call C function

L_GoToSleep:
        cli
        hlt
        jmp         $-2

L_RendezvousFunnelProcEnd:
;-------------------------------------------------------------------------------------
;  AsmGetAddressMap (&AddressMap);
;-------------------------------------------------------------------------------------
global ASM_PFX(AsmGetAddressMap)
ASM_PFX(AsmGetAddressMap):

        pushad
        mov         ebp,esp

        mov         ebx, dword [ebp+0x24]
        mov         dword [ebx], L_RendezvousFunnelProcStart
        mov         dword [ebx+0x4], L_PMODE_ENTRY - L_RendezvousFunnelProcStart
        mov         dword [ebx+0x8], L_FLAT32_JUMP - L_RendezvousFunnelProcStart
        mov         dword [ebx+0xc], L_RendezvousFunnelProcEnd - L_RendezvousFunnelProcStart

        popad
        ret

