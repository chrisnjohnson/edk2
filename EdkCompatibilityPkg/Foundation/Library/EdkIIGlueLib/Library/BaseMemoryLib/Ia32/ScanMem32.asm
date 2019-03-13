;------------------------------------------------------------------------------
;
; Copyright (c) 2006, Intel Corporation. All rights reserved.<BR>
; SPDX-License-Identifier: BSD-2-Clause-Patent
;
; Module Name:
;
;   ScanMem32.Asm
;
; Abstract:
;
;   ScanMem32 function
;
; Notes:
;
;   The following BaseMemoryLib instances share the same version of this file:
;
;       BaseMemoryLibRepStr
;       BaseMemoryLibMmx
;       BaseMemoryLibSse2
;
;------------------------------------------------------------------------------

    .686
    .model  flat,C
    .code

;------------------------------------------------------------------------------
; CONST VOID *
; EFIAPI
; InternalMemScanMem32 (
;   IN      CONST VOID                *Buffer,
;   IN      UINTN                     Length,
;   IN      UINT32                    Value
;   );
;------------------------------------------------------------------------------
InternalMemScanMem32    PROC    USES    edi
    mov     ecx, [esp + 12]
    mov     edi, [esp + 8]
    mov     eax, [esp + 16]
    repne   scasd
    lea     eax, [edi - 4]
    cmovnz  eax, ecx
    ret
InternalMemScanMem32    ENDP

    END
