;------------------------------------------------------------------------------
;
; Copyright (c) 2006, Intel Corporation. All rights reserved.<BR>
; SPDX-License-Identifier: BSD-2-Clause-Patent
;
; Module Name:
;
;   Monitor.Asm
;
; Abstract:
;
;   AsmMonitor function
;
; Notes:
;
;------------------------------------------------------------------------------

    .686
    .model  flat,C
    .code

;------------------------------------------------------------------------------
; UINTN
; EFIAPI
; AsmMonitor (
;   IN      UINTN                     Eax,
;   IN      UINTN                     Ecx,
;   IN      UINTN                     Edx
;   );
;------------------------------------------------------------------------------
AsmMonitor  PROC
    mov     eax, [esp + 4]
    mov     ecx, [esp + 8]
    mov     edx, [esp + 12]
    DB      0fh, 1, 0c8h                ; monitor
    ret
AsmMonitor  ENDP

    END
