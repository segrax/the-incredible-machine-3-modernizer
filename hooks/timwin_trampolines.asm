.386
.model flat
option casemap:none

EXTERN _timwin_apply_dynamic_layout:PROC
EXTERN _timwin_force_half_screen:PROC
EXTERN _timwin_force_half_if_near_screen:PROC
EXTERN _timwin_center_child_rect:PROC
EXTERN _timwin_partbin_content_stretch:PROC
EXTERN _timwin_scale_mouse_move:PROC
EXTERN _timwin_scale_mouse_down:PROC

PUBLIC _timwin_child_create_layout_wrapper
PUBLIC _timwin_fullscreen_restore_size_hook
PUBLIC _timwin_parent_syscommand_restore_hook
PUBLIC _timwin_parent_resize_layout_hook
PUBLIC _timwin_toolbar_surface_native_layout_hook
PUBLIC _timwin_enter_command_hook
PUBLIC _timwin_enter_key_hook
PUBLIC _timwin_part_buffer_resize_hook
PUBLIC _timwin_part_surface_stretch_hook
PUBLIC _timwin_partbin_reopen_guard
PUBLIC _timwin_playfield_mouse_hook
PUBLIC _timwin_goal_mouse_hook
PUBLIC _timwin_part_mouse_hook
PUBLIC _timwin_playfield_region_client_bounds_hook
PUBLIC _timwin_input_mouse_move_hook
PUBLIC _timwin_input_mouse_down_hook
PUBLIC _timwin_toolbar_scaled_repaint_hook
PUBLIC _timwin_toolbar_partial_repaint_hook
PUBLIC _timwin_partbin_partial_stretch_hook
PUBLIC _timwin_toolbar_wm_paint_scale_hook
PUBLIC _timwin_shape_frame_table_guard
PUBLIC _timwin_single_shape_frame_guard

GETCLIENTRECT_VA EQU 004671E5h
ENDDIALOG_VA EQU 00467209h
MOVEWINDOW_VA EQU 00467311h
TB_WINDOW_PROCESS_CHILD_VA EQU 00412997h
PARTBIN_SURFACE_PTR EQU 00480FD0h
STRETCH_PROC_PTR EQU 0047A5F4h
CURRENT_DEST_DC EQU 004B1754h
ORIG_REPAINT_VA EQU 00458364h
SURFACE_REPAINT_VA EQU 00458384h
SURFACE_LOCK_VA EQU 00458B58h

JMP_ABS MACRO target
    push target
    ret
ENDM

CALL_ABS MACRO target
    mov eax, target
    call eax
ENDM

.code

_timwin_child_create_layout_wrapper PROC
    call _timwin_apply_dynamic_layout
    mov eax, dword ptr [esp+8]
    cmp eax, 00414FC1h
    je goal_rect
    cmp eax, 00415CABh
    je play_rect
    cmp eax, 00417ADAh
    je part_rect
    cmp eax, 004147B6h
    je centered_child_rect
    JMP_ABS 00411FA5h

goal_rect:
    mov eax, dword ptr ds:[00471B24h]
    mov dword ptr [esp+10h], eax
    mov eax, dword ptr ds:[00471B28h]
    mov dword ptr [esp+14h], eax
    mov eax, dword ptr ds:[00471B2Ch]
    mov dword ptr [esp+18h], eax
    mov eax, dword ptr ds:[00471B30h]
    mov dword ptr [esp+1Ch], eax
    JMP_ABS 00411FA5h

play_rect:
    mov eax, dword ptr ds:[00471D0Ch]
    mov dword ptr [esp+10h], eax
    mov eax, dword ptr ds:[00471D10h]
    mov dword ptr [esp+14h], eax
    mov eax, dword ptr ds:[00471D14h]
    mov dword ptr [esp+18h], eax
    mov eax, dword ptr ds:[00471D18h]
    mov dword ptr [esp+1Ch], eax
    JMP_ABS 00411FA5h

part_rect:
    mov eax, dword ptr ds:[00471F7Ch]
    mov dword ptr [esp+10h], eax
    mov eax, dword ptr ds:[00471F80h]
    mov dword ptr [esp+14h], eax
    mov eax, dword ptr ds:[00471F84h]
    mov dword ptr [esp+18h], eax
    mov eax, dword ptr ds:[00471F88h]
    mov dword ptr [esp+1Ch], eax
    JMP_ABS 00411FA5h

centered_child_rect:
    lea eax, [esp+10h]
    lea edx, [esp+14h]
    push dword ptr [esp+1Ch]
    push dword ptr [esp+1Ch]
    push edx
    push eax
    call _timwin_center_child_rect
    add esp, 10h
    JMP_ABS 00411FA5h
_timwin_child_create_layout_wrapper ENDP

_timwin_fullscreen_restore_size_hook PROC
    call _timwin_force_half_if_near_screen
    JMP_ABS 004197B4h
_timwin_fullscreen_restore_size_hook ENDP

_timwin_parent_syscommand_restore_hook PROC
    mov eax, edi
    and eax, 0000FFF0h
    cmp eax, 0000F120h
    jne not_restore
    call _timwin_force_half_screen
    xor eax, eax
    JMP_ABS 0041298Eh

not_restore:
    mov eax, edi
    and eax, 0000FFF0h
    cmp eax, 0000F030h
    jne default_path
    JMP_ABS 00412862h

default_path:
    JMP_ABS 00412983h
_timwin_parent_syscommand_restore_hook ENDP

_timwin_parent_resize_layout_hook PROC
    pushad
    cmp ebx, dword ptr ds:[00490EF0h]
    jne original_resize_path
    call _timwin_apply_dynamic_layout
    popad
    xor eax, eax
    JMP_ABS 0041298Eh

original_resize_path:
    popad
    push dword ptr [ebp+14h]
    push edi
    push esi
    JMP_ABS 004126ACh
_timwin_parent_resize_layout_hook ENDP

_timwin_toolbar_surface_native_layout_hook PROC
    push dword ptr ds:[00480FCCh]
    pushad
    cmp dword ptr ds:[00480FB8h], 0
    je toolbar_surface_done
    cmp dword ptr ds:[00490EF0h], 0
    je toolbar_surface_done
    sub esp, 10h
    lea eax, [esp]
    push eax
    push dword ptr ds:[00490EF0h]
    CALL_ABS GETCLIENTRECT_VA
    test eax, eax
    jz toolbar_surface_cleanup
    mov esi, dword ptr [esp+8]
    mov edi, dword ptr [esp+0Ch]
    mov eax, edi
    cdq
    mov ecx, 10h
    idiv ecx
    cmp eax, 22h
    jge toolbar_surface_top_ready
    mov eax, 22h

toolbar_surface_top_ready:
    mov ebx, dword ptr ds:[00480FCCh]
    xor edx, edx
    test ebx, ebx
    jz toolbar_surface_flag_ready
    mov edx, dword ptr [ebx+38h]
    or dword ptr [ebx+38h], 40h

toolbar_surface_flag_ready:
    mov dword ptr [esp], edx
    push 1
    push eax
    push esi
    push 0
    push 0
    push dword ptr ds:[00480FB8h]
    CALL_ABS MOVEWINDOW_VA
    mov ebx, dword ptr ds:[00480FCCh]
    test ebx, ebx
    jz toolbar_surface_cleanup
    mov edx, dword ptr [esp]
    mov dword ptr [ebx+38h], edx

toolbar_surface_cleanup:
    add esp, 10h

toolbar_surface_done:
    popad
    JMP_ABS 00418A8Ah
_timwin_toolbar_surface_native_layout_hook ENDP

_timwin_enter_command_hook PROC
    mov eax, dword ptr [ebp+10h]
    and eax, 0000FFFFh
    cmp eax, 1
    jne command_done
    push 1
    push edi
    CALL_ABS ENDDIALOG_VA
command_done:
    JMP_ABS 00412D6Ah
_timwin_enter_command_hook ENDP

_timwin_enter_key_hook PROC
    mov eax, dword ptr [ebp+0Ch]
    cmp eax, 00000100h
    je check_return
    cmp eax, 00000102h
    jne original_key_path

check_return:
    cmp dword ptr [ebp+10h], 0Dh
    jne original_key_path
    push 1
    push edi
    CALL_ABS ENDDIALOG_VA
    JMP_ABS 00412D6Ah

original_key_path:
    push dword ptr [ebp+14h]
    push dword ptr [ebp+10h]
    mov eax, dword ptr [ebp+0Ch]
    push eax
    push edi
    CALL_ABS 00451838h
    add esp, 10h
    xor ebx, ebx
    JMP_ABS 00412D6Ah
_timwin_enter_key_hook ENDP

_timwin_part_buffer_resize_hook PROC
    push 636
    push 432
    CALL_ABS 00416F1Dh
    add esp, 8
    JMP_ABS 00417E23h
_timwin_part_buffer_resize_hook ENDP

_timwin_part_surface_stretch_hook PROC
    mov dword ptr [esi], eax
    test eax, eax
    jz part_surface_zero
    or byte ptr [eax+38h], 40h
    JMP_ABS 00416FE3h

part_surface_zero:
    JMP_ABS 0041712Eh
_timwin_part_surface_stretch_hook ENDP

_timwin_partbin_reopen_guard PROC
    cmp eax, dword ptr ds:[0047C50Ch]
    jne recreate_partbin
    cmp edx, dword ptr ds:[0047C510h]
    jne recreate_partbin
    cmp dword ptr [esi], 0
    je recreate_partbin
    cmp dword ptr ds:[00480FD4h], 0
    je recreate_partbin
    mov ecx, dword ptr ds:[00480FD4h]
    mov ecx, dword ptr [ecx+18h]
    cmp ecx, dword ptr ds:[00471F84h]
    jne recreate_partbin
    JMP_ABS 0041712Eh

recreate_partbin:
    JMP_ABS 00416F47h
_timwin_partbin_reopen_guard ENDP

_timwin_playfield_mouse_hook PROC
    lea eax, [ebx-200h]
    cmp eax, 9
    ja playfield_after_filter
    nop
playfield_after_filter:
    push esi
    push dword ptr [ebp+10h]
    push ebx
    push edi
    CALL_ABS TB_WINDOW_PROCESS_CHILD_VA
    JMP_ABS 00415FC6h
_timwin_playfield_mouse_hook ENDP

_timwin_goal_mouse_hook PROC
    lea eax, [ebx-200h]
    cmp eax, 9
    ja goal_after_filter
    nop
goal_after_filter:
    push esi
    push edi
    push ebx
    push dword ptr [ebp+8]
    CALL_ABS TB_WINDOW_PROCESS_CHILD_VA
    JMP_ABS 0041562Dh
_timwin_goal_mouse_hook ENDP

_timwin_part_mouse_hook PROC
    lea eax, [ebx-200h]
    cmp eax, 9
    ja part_after_filter
    nop
part_after_filter:
    push esi
    push edi
    push ebx
    push dword ptr [ebp+8]
    CALL_ABS TB_WINDOW_PROCESS_CHILD_VA
    JMP_ABS 00418122h
_timwin_part_mouse_hook ENDP

_timwin_playfield_region_client_bounds_hook PROC
    cmp esi, dword ptr ds:[00480FB4h]
    je playfield_region
    cmp esi, dword ptr ds:[00480FB8h]
    je playfield_region

original_region:
    lea ecx, [eax+edi]
    test ecx, ecx
    jl region_fail
    lea ecx, [eax+edi]
    cmp ecx, dword ptr [ebx+18h]
    jge region_fail
    mov ecx, dword ptr [ebp-0Ch]
    add ecx, edx
    test ecx, ecx
    jl region_fail
    mov ecx, dword ptr [ebp-0Ch]
    add ecx, edx
    cmp ecx, dword ptr [ebx+1Ch]
    jge region_fail
    JMP_ABS 004519B0h

region_fail:
    JMP_ABS 004519B7h

playfield_region:
    push eax
    push edx
    sub esp, 10h
    lea ecx, [esp]
    push ecx
    push esi
    CALL_ABS GETCLIENTRECT_VA
    test eax, eax
    jz region_fallback
    mov ecx, dword ptr [esp+14h]
    add ecx, edi
    test ecx, ecx
    jl playfield_region_fail
    cmp ecx, dword ptr [esp+8]
    jge playfield_region_fail
    mov ecx, dword ptr [ebp-0Ch]
    add ecx, dword ptr [esp+10h]
    test ecx, ecx
    jl playfield_region_fail
    cmp ecx, dword ptr [esp+0Ch]
    jge playfield_region_fail

playfield_region_pass:
    add esp, 10h
    pop edx
    pop eax
    JMP_ABS 004519B0h

playfield_region_fail:
    add esp, 10h
    pop edx
    pop eax
    JMP_ABS 004519B7h

region_fallback:
    add esp, 10h
    pop edx
    pop eax
    jmp original_region
_timwin_playfield_region_client_bounds_hook ENDP

_timwin_input_mouse_move_hook PROC
    push dword ptr [ebp-0Ch]
    push edi
    push esi
    call _timwin_scale_mouse_move
    add esp, 0Ch
    JMP_ABS 00451A3Fh
_timwin_input_mouse_move_hook ENDP

_timwin_input_mouse_down_hook PROC
    push dword ptr [ebp-0Ch]
    push edi
    push esi
    call _timwin_scale_mouse_down
    add esp, 0Ch
    JMP_ABS 00451A1Bh
_timwin_input_mouse_down_hook ENDP

_timwin_toolbar_scaled_repaint_hook PROC
    push ebp
    mov ebp, esp
    sub esp, 14h
    push ebx
    push esi
    push edi
    mov eax, dword ptr [ebp+8]
    test eax, eax
    jz repaint_return
    cmp eax, dword ptr ds:[00480FCCh]
    jne repaint_normal
    mov edx, dword ptr [eax+38h]
    mov dword ptr [ebp-4], edx
    or dword ptr [eax+38h], 40h
    push 1
    push eax
    CALL_ABS SURFACE_LOCK_VA
    add esp, 8
    mov ebx, dword ptr [ebp+8]
    mov eax, dword ptr [ebx+8]
    test eax, eax
    jz repaint_toolbar_after
    lea ecx, [ebp-14h]
    push ecx
    push eax
    CALL_ABS GETCLIENTRECT_VA
    test eax, eax
    jz repaint_toolbar_after
    mov edi, dword ptr [ebp-8]
    test edi, edi
    jle repaint_toolbar_after
    mov ecx, dword ptr [ebx+1Ch]
    test ecx, ecx
    jle repaint_toolbar_after
    mov eax, dword ptr [ebx+18h]
    imul eax, edi
    cdq
    idiv ecx
    mov esi, eax
    mov eax, dword ptr [ebx+0Ch]
    mov eax, dword ptr [eax]
    mov eax, dword ptr [eax+94h]
    cmp dword ptr ds:[CURRENT_DEST_DC], 0
    je repaint_toolbar_after
    push dword ptr [ebx+1Ch]
    push dword ptr [ebx+18h]
    push 0
    push 0
    push eax
    push edi
    push esi
    push 0
    push 0
    push dword ptr ds:[CURRENT_DEST_DC]
    call dword ptr ds:[STRETCH_PROC_PTR]

repaint_toolbar_after:
    push 0
    push dword ptr [ebp+8]
    CALL_ABS SURFACE_LOCK_VA
    add esp, 8
    mov eax, dword ptr [ebp+8]
    mov edx, dword ptr [ebp-4]
    mov dword ptr [eax+38h], edx

repaint_return:
    pop edi
    pop esi
    pop ebx
    mov esp, ebp
    pop ebp
    ret

repaint_normal:
    mov eax, dword ptr [ebp+8]
    mov edx, dword ptr [eax+1Ch]
    push edx
    mov edx, dword ptr [eax+18h]
    push edx
    push 0
    push 0
    push eax
    CALL_ABS SURFACE_REPAINT_VA
    add esp, 14h
    pop edi
    pop esi
    pop ebx
    mov esp, ebp
    pop ebp
    ret
_timwin_toolbar_scaled_repaint_hook ENDP

_timwin_toolbar_partial_repaint_hook PROC
    push ebp
    mov ebp, esp
    sub esp, 78h
    push ebx
    push esi
    push edi
    mov ebx, dword ptr [ebp+8]
    cmp ebx, dword ptr ds:[00480FCCh]
    je toolbar_partial_toolbar
    cmp ebx, dword ptr ds:[00480FD0h]
    je toolbar_partial_full_rect
    cmp ebx, dword ptr ds:[00480FD4h]
    jne toolbar_partial_original

toolbar_partial_full_rect:
    test ebx, ebx
    jz toolbar_partial_original
    mov dword ptr [ebp+0Ch], 0
    mov dword ptr [ebp+10h], 0
    mov eax, dword ptr [ebx+18h]
    mov dword ptr [ebp+14h], eax
    mov eax, dword ptr [ebx+1Ch]
    mov dword ptr [ebp+18h], eax
    JMP_ABS 0045838Dh

toolbar_partial_toolbar:
    test ebx, ebx
    jz toolbar_partial_original
    cmp dword ptr ds:[STRETCH_PROC_PTR], 0
    je toolbar_partial_original
    push ebx
    CALL_ABS ORIG_REPAINT_VA
    pop ecx
    pop edi
    pop esi
    pop ebx
    mov esp, ebp
    pop ebp
    ret

toolbar_partial_original:
    JMP_ABS 0045838Dh
_timwin_toolbar_partial_repaint_hook ENDP

_timwin_toolbar_wm_paint_scale_hook PROC
    cmp ebx, dword ptr ds:[00480FCCh]
    je toolbar_wm_toolbar
    cmp ebx, dword ptr ds:[00480FD0h]
    je toolbar_wm_part
    test byte ptr [ebx+38h], 40h
    jz toolbar_wm_normal
    JMP_ABS 004582B6h

toolbar_wm_normal:
    JMP_ABS 004582FBh

toolbar_wm_toolbar:
    lea eax, [ebp-24h]
    push eax
    push dword ptr [ebp+8]
    CALL_ABS GETCLIENTRECT_VA
    test eax, eax
    jz toolbar_wm_done
    mov edi, dword ptr [ebp-18h]
    test edi, edi
    jle toolbar_wm_done
    mov ecx, dword ptr [ebx+1Ch]
    test ecx, ecx
    jle toolbar_wm_done
    mov eax, dword ptr [ebx+18h]
    imul eax, edi
    cdq
    idiv ecx
    mov esi, eax
    mov eax, dword ptr [ebx+0Ch]
    mov eax, dword ptr [eax]
    mov eax, dword ptr [eax+94h]
    cmp dword ptr ds:[CURRENT_DEST_DC], 0
    je toolbar_wm_done
    push dword ptr [ebx+1Ch]
    push dword ptr [ebx+18h]
    push 0
    push 0
    push eax
    push edi
    push esi
    push 0
    push 0
    push dword ptr ds:[CURRENT_DEST_DC]
    call dword ptr ds:[STRETCH_PROC_PTR]

toolbar_wm_done:
    JMP_ABS 0045833Ah

toolbar_wm_part:
    call _timwin_partbin_content_stretch
    JMP_ABS 0045833Ah
_timwin_toolbar_wm_paint_scale_hook ENDP

_timwin_partbin_partial_stretch_hook PROC
    cmp ebx, dword ptr ds:[PARTBIN_SURFACE_PTR]
    je partbin_partial
    test byte ptr [ebx+38h], 40h
    jz partbin_partial_normal
    JMP_ABS 00458435h

partbin_partial_normal:
    JMP_ABS 00458492h

partbin_partial:
    call _timwin_partbin_content_stretch
    JMP_ABS 004584E9h
_timwin_partbin_partial_stretch_hook ENDP

_timwin_shape_frame_table_guard PROC
    mov edx, dword ptr [edx+6Ch]
    test edx, edx
    jz shape_frame_table_zero
    mov ecx, dword ptr [eax+18h]
    mov edx, dword ptr [edx+ecx*4]
    JMP_ABS 00443DD9h

shape_frame_table_zero:
    xor edx, edx
    JMP_ABS 00443DD9h
_timwin_shape_frame_table_guard ENDP

_timwin_single_shape_frame_guard PROC
    mov edx, dword ptr [edx+6Ch]
    test edx, edx
    jz single_shape_frame_zero
    mov edx, dword ptr [edx]
    JMP_ABS 00444F68h

single_shape_frame_zero:
    xor edx, edx
    JMP_ABS 00444F68h
_timwin_single_shape_frame_guard ENDP

END
