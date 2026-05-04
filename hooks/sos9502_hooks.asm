.386
.model flat
option casemap:none

PUBLIC _sos9502_reloc_guard
PUBLIC _sos9502_reloc_guard_end
PUBLIC _sos9502_midi_a00a
PUBLIC _sos9502_midi_a00a_end
PUBLIC _sos9502_midi_return
PUBLIC _sos9502_midi_return_end

SOS9502_HOOK_BASE EQU 00410F20h
SOS_RELOC_BLOCK_VA EQU 004093E6h
SOS_SKIP_RELOC_VA EQU 00409443h
SOS_RETURN_VA EQU 00409652h
SOS_A00A_CONT_VA EQU 0040AF56h
SOS_SONG_TABLE_VA EQU 00416C38h

JMP_REL32 MACRO target
    db 0E9h
    dd target - (SOS9502_HOOK_BASE + ($ - _sos9502_blob_start) + 4)
ENDM

JNZ_REL32 MACRO target
    db 0Fh, 085h
    dd target - (SOS9502_HOOK_BASE + ($ - _sos9502_blob_start) + 4)
ENDM

PAD_TO MACRO target_off
    IF (($ - _sos9502_blob_start) GT target_off)
        .ERR
    ENDIF
    db (target_off - ($ - _sos9502_blob_start)) dup (090h)
ENDM

.code

_sos9502_blob_start LABEL BYTE

_sos9502_reloc_guard PROC
    mov esi, dword ptr [ebp-8]
    movsx eax, word ptr [esi+20h]
    test ah, 20h
    JNZ_REL32 SOS_SKIP_RELOC_VA
    mov ecx, dword ptr [esi+0E8h]
    cmp ecx, esi
    jb reloc_do_reloc
    mov edx, esi
    add edx, 00100000h
    cmp ecx, edx
    ja reloc_bad
    or word ptr [esi+20h], 2000h
    JMP_REL32 SOS_SKIP_RELOC_VA

reloc_do_reloc:
    JMP_REL32 SOS_RELOC_BLOCK_VA

reloc_bad:
    mov eax, 00020000h
    JMP_REL32 SOS_RETURN_VA

_sos9502_reloc_guard_end LABEL BYTE
_sos9502_reloc_guard ENDP

PAD_TO 060h

_sos9502_midi_a00a PROC
    mov ecx, dword ptr [ebp-8]
    mov cx, word ptr [ecx]
    and ecx, 0FFFFh
    cmp eax, ecx
    je near ptr a00a_match
    cmp ecx, 0A00Ah
    jne near ptr a00a_done

a00a_match:
    mov word ptr [ebp-0Ch], 1

a00a_done:
    JMP_REL32 SOS_A00A_CONT_VA

_sos9502_midi_a00a_end LABEL BYTE
_sos9502_midi_a00a ENDP

PAD_TO 0A0h

_sos9502_midi_return PROC
    test eax, eax
    je midi_return_epilogue
    push ecx
    push edx
    push esi
    call midi_return_anchor

midi_return_anchor:
    pop esi
    mov edx, dword ptr [ebp-8]
    mov ecx, dword ptr [ebp-14h]
    and ecx, 0FFFFh
    cmp ecx, 20h
    jae midi_return_skip
    cmp edx, dword ptr [esi+ecx*4+(SOS_SONG_TABLE_VA - (SOS9502_HOOK_BASE + (midi_return_anchor - _sos9502_blob_start)))]
    jne midi_return_skip
    mov dword ptr [esi+ecx*4+(SOS_SONG_TABLE_VA - (SOS9502_HOOK_BASE + (midi_return_anchor - _sos9502_blob_start)))], 0

midi_return_skip:
    pop esi
    pop edx
    pop ecx

midi_return_epilogue:
    pop edi
    pop esi
    pop ebx
    leave
    ret 8

_sos9502_midi_return_end LABEL BYTE
_sos9502_midi_return ENDP

END
