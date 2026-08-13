/* SPDX-License-Identifier: GPL-2.0-or-later */
/* IA-64 EFI firmware entry point (GCC assembler syntax).
 *
 * The linker script links at VMA=0x100000, so all symbols (`__gp`,
 * firmware_main, etc.) are already at correct runtime addresses.
 * No GP manual offset computation is needed.
 */

    .section .text._start, "ax"
    .global _start
    .type _start, @function
    .proc _start
    .explicit
_start:
    /* Secondary processors resume here when the platform wake IPI arrives. */
    mov r14 = cr.lid
    ;;
    cmp.ne p6, p7 = r14, r0
    ;;
(p6) br.cond.sptk.few start_secondary
    ;;

    br.cond.sptk.few start_after_pal
    ;;
    .org 0x60
    .endp _start
    .size _start, . - _start

    .section .text.pal_proc_entry, "ax"
    .global pal_proc_entry
    .type pal_proc_entry, @function
    .proc pal_proc_entry
    .explicit
pal_proc_entry:
    break.m 0x100000
    ;;
    br.many b0
    ;;
    .endp pal_proc_entry
    .size pal_proc_entry, . - pal_proc_entry

    /*
     * SAL 3.0 requires a virtual-mode caller to put the virtual SAL GP in
     * r1.  One released IA-64 HAL instead relocates the advertised GP with
     * the runtime-code mapping even when the MDT gives runtime data a
     * different mapping.  The public SAL and EFI specifications found so
     * far do not describe that historical mapping rule.
     *
     * The compatibility firmware profile advertises the anchor below,
     * whereas the standards-oriented profile advertises the real data GP.
     * Derive the SAL code relocation from the current IP instead of assuming
     * that the caller's r1 uses either profile.  The stub then runs the
     * ordinary linked C image with instruction, data, and RSE translation
     * disabled and its real physical GP.
     *
     * A physical SAL call must not retain the caller's virtual RSE as the
     * previous bridge did: alloc or br.ret can otherwise raise an RSE TLB
     * miss after PSR.ic/it/dt no longer describe the OS exception context.
     * Follow SDM Vol. 2 section 6.11.3 instead.  Flush the caller's dirty
     * partition while its translations are live, enter enforced-lazy mode,
     * and switch BSPSTORE/RNAT to a private physical backing store.  The
     * reverse sequence restores the caller's backing store before its
     * translation state and RSC are reinstated.
     *
     * Each processor gets a 32 KiB slot in the CPU-assist area: 16 KiB for
     * the RSE backing store followed by a 16 KiB memory stack.  Interrupts
     * and interruption collection stay disabled while the private physical
     * context is active.
     */
    .equ SAL_PSR_I,                         (1 << 14)
    .equ SAL_PSR_IC,                        (1 << 13)
    .equ SAL_PSR_DT,                        (1 << 17)
    .equ SAL_PSR_RT,                        (1 << 27)
    .equ SAL_PSR_IT,                        (1 << 36)
    .equ SAL_PSR_BN,                        (1 << 44)
    .equ SAL_PSR_TRANSLATION_MASK,          (SAL_PSR_DT | SAL_PSR_RT | SAL_PSR_IT)
    .equ SAL_PSR_PHYSICAL_ENTRY_CLEAR,      (SAL_PSR_I | SAL_PSR_IC | SAL_PSR_TRANSLATION_MASK)
    .equ SAL_RUNTIME_AREA_SHIFT,            15
    .equ SAL_RUNTIME_BACKING_SIZE,          0x4000
    .equ SAL_RUNTIME_STACK_FRAME_SIZE,      0x100
    .equ SAL_RUNTIME_RSC_EAGER_PL0,         3

    .section .text.sal_proc_entry, "ax"
    .align 16
    .global sal_proc_gp_anchor
    .type sal_proc_gp_anchor, @object
sal_proc_gp_anchor:
    .quad 0
    .quad 0
    .size sal_proc_gp_anchor, . - sal_proc_gp_anchor

    .align 8
    .global sal_runtime_area_top
    .type sal_runtime_area_top, @object
sal_runtime_area_top:
    .quad 0
    .size sal_runtime_area_top, . - sal_runtime_area_top

    .align 16
    .global sal_proc_entry
    .type sal_proc_entry, @function
    .proc sal_proc_entry
    .explicit
sal_proc_entry:
    /*
     * Preserve every value needed across the RSE switch in static GRs.
     * r4-r7 are left untouched so the C ABI preserves their values and NaT
     * bits without an unsafe ordinary store.
     */
    mov r14 = r32
    mov r15 = r33
    mov r16 = r34
    mov r17 = r35
    ;;
    mov r18 = r36
    mov r19 = r37
    mov r20 = r38
    mov r21 = r39
    ;;
    mov r22 = b0
    mov r23 = r1
    mov r24 = r12
    mov r25 = psr
    ;;

    /*
     * mov-from-PSR exposes only PSR{36:35} and PSR{31:0}; PSR.bn reads as
     * zero.  The SAL calling convention requires bank 1 on entry and exit,
     * so restore that architecturally implicit bit before either RFI.
     */
    movl r31 = SAL_PSR_BN
    ;;
    or r25 = r25, r31
    ;;
sal_proc_capture_ip:
    /*
     * Do not use br.call to obtain IP here.  A call overwrites ar.pfs before
     * the physical-context bridge has saved the caller's frame state.
     */
    mov r26 = ip
    mov r27 = ar.rsc
    ;;
    movl r31 = sal_proc_capture_ip
    ;;
    sub r26 = r26, r31

    /*
     * SAL permits fully physical or fully virtual calls.  Do not attempt a
     * privileged mode transition from a mixed translation context.
     */
    movl r31 = SAL_PSR_TRANSLATION_MASK
    ;;
    and r30 = r25, r31
    ;;
    cmp.eq p6, p7 = r30, r0
    cmp.eq p8, p9 = r30, r31
    ;;
(p6) br.cond.sptk.few sal_proc_mode_valid
(p8) br.cond.sptk.few sal_proc_mode_valid
    ;;
    mov r8 = -3
    mov r9 = r0
    mov r10 = r0
    mov r11 = r0
    ;;
    br.ret.sptk.many b0
    ;;

sal_proc_mode_valid:
    /*
     * Explicit flushrs may fault on the caller's backing store.  Perform it
     * before changing translation or interruption-collection state.
     */
    rsm psr.i
    ;;
    mov ar.rsc = r0
    ;;
    flushrs
    ;;
    mov r28 = ar.bsp
    mov r29 = ar.rnat
    mov r31 = cr.lid
    ;;
    extr.u r31 = r31, 24, 8
    ;;
    shl r31 = r31, SAL_RUNTIME_AREA_SHIFT
    movl r2 = sal_runtime_area_top
    ;;
    add r2 = r2, r26
    ;;
    ld8 r2 = [r2]
    ;;
    sub r2 = r2, r31
    movl r3 = (SAL_RUNTIME_BACKING_SIZE * 2)
    ;;
    sub r30 = r2, r3
    ;;

    /*
     * Start an empty physical RSE context.  mov-to-BSPSTORE makes RNAT
     * undefined, so initialize it before enabling eager RSE operation.
     * Interruption collection is disabled before BSPSTORE changes; until
     * that point both the caller's SP and RSE context remain intact.
     */
    rsm psr.ic
    ;;
    srlz.d
    ;;
    srlz.i
    ;;
    invala
    mov ar.bspstore = r30
    ;;
    mov ar.rnat = r0
    movl r31 = SAL_PSR_PHYSICAL_ENTRY_CLEAR
    ;;
    andcm r31 = r25, r31
    movl r3 = sal_proc_physical
    ;;
    mov cr.ipsr = r31
    mov cr.iip = r3
    mov cr.ifs = r0
    ;;
    rfi
    ;;

sal_proc_physical:
    adds r12 = -SAL_RUNTIME_STACK_FRAME_SIZE, r2
    mov ar.rsc = SAL_RUNTIME_RSC_EAGER_PL0
    movl r1 = __gp
    ;;

    /*
     * The bridge owns the fixed frame above the C stack.  All stored values
     * originate in non-NaT-capable special/static state; SAL arguments stay
     * in registers until they are copied into the new output frame.
     */
    mov r2 = r12
    ;;
    st8 [r2] = r22, 8
    ;;
    st8 [r2] = r23, 8
    ;;
    st8 [r2] = r24, 8
    ;;
    st8 [r2] = r25, 8
    ;;
    st8 [r2] = r26, 8
    ;;
    st8 [r2] = r27, 8
    ;;
    st8 [r2] = r28, 8
    ;;
    st8 [r2] = r29, 8
    ;;

    alloc r31 = ar.pfs, 0, 0, 8, 0
    ;;
    st8 [r2] = r31
    mov r32 = r14
    mov r33 = r15
    ;;
    mov r34 = r16
    mov r35 = r17
    mov r36 = r18
    mov r37 = r19
    ;;
    mov r38 = r20
    mov r39 = r21
    ;;
    br.call.sptk.many b0 = sal_proc_dispatch
    ;;

    mov r23 = r8
    mov r24 = r9
    mov r25 = r10
    mov r26 = r11
    ;;
    mov r2 = r12
    ;;
    ld8 r14 = [r2], 8
    ;;
    ld8 r15 = [r2], 8
    ;;
    ld8 r16 = [r2], 8
    ;;
    ld8 r17 = [r2], 8
    ;;
    ld8 r18 = [r2], 8
    ;;
    ld8 r19 = [r2], 8
    ;;
    ld8 r20 = [r2], 8
    ;;
    ld8 r21 = [r2], 8
    ;;
    ld8 r22 = [r2]
    ;;

    /*
     * Flush the private context before rebasing BSPSTORE.  No stacked
     * register is used after the switch until the caller's RT and RSC have
     * been restored.
     */
    mov ar.rsc = r0
    ;;
    flushrs
    ;;
    invala
    movl r31 = sal_proc_return
    ;;
    add r31 = r31, r18
    mov ar.bspstore = r20
    ;;
    mov ar.rnat = r21
    mov ar.pfs = r22
    mov cr.ipsr = r17
    ;;
    mov cr.iip = r31
    mov cr.ifs = r0
    mov ar.rsc = r19
    ;;
    rfi
    ;;

sal_proc_return:
    mov b0 = r14
    mov r1 = r15
    mov r12 = r16
    ;;
    mov r8 = r23
    mov r9 = r24
    mov r10 = r25
    mov r11 = r26
    ;;
    br.ret.sptk.many b0
    ;;
    .endp sal_proc_entry
    .size sal_proc_entry, . - sal_proc_entry

    /*
     * The machine implements the arithmetic behind this architected
     * firmware ABI.  Keeping the bridge as a leaf preserves all eight input
     * registers and returns the four-word result directly in r8-r11.
     */
    .section .text.fpswa_emulation_entry, "ax"
    .global fpswa_emulation_entry
    .type fpswa_emulation_entry, @function
    .proc fpswa_emulation_entry
    .explicit
fpswa_emulation_entry:
    break.m 0x100001
    ;;
    br.ret.sptk.many b0
    ;;
    .endp fpswa_emulation_entry
    .size fpswa_emulation_entry, . - fpswa_emulation_entry

    /* Copied into an IVT slot while an EFI debug callback is registered. */
    .section .text.fw_debug_ivt_stub, "ax"
    .align 16
    .global fw_debug_ivt_stub_start
    .global fw_debug_ivt_stub_end
    .type fw_debug_ivt_stub_start, @function
    .proc fw_debug_ivt_stub_start
    .explicit
fw_debug_ivt_stub_start:
    movl r16 = fw_debug_exception_entry
    ;;
    break.m 0x100002
    ;;
fw_debug_ivt_stub_end:
    .endp fw_debug_ivt_stub_start
    .size fw_debug_ivt_stub_start, . - fw_debug_ivt_stub_start

    /*
     * The target bridge supplies the exception type, context record, a
     * private stack, and the processor index in r16-r18.  cover preserves
     * the interrupted frame in cr.ifs before calling ordinary C code.
     */
    .section .text.fw_debug_exception_entry, "ax"
    .align 16
    .global fw_debug_exception_entry
    .type fw_debug_exception_entry, @function
    .proc fw_debug_exception_entry
    .explicit
fw_debug_exception_entry:
    cover
    ;;
    break.m 0x100003
    ;;
    .align 16
    mov r19 = r18
    movl r1 = __gp
    ;;
    alloc r18 = ar.pfs, 0, 3, 3, 0
    ;;
    mov r35 = r16
    mov r36 = r17
    mov r37 = r19
    ;;
    br.call.sptk.many b0 = fw_debug_support_dispatch
    ;;
    break.m 0x100004
    ;;
    break.m 0
    ;;
    .endp fw_debug_exception_entry
    .size fw_debug_exception_entry, . - fw_debug_exception_entry

    .section .text.start_after_pal, "ax"
    .type start_after_pal, @function
    .proc start_after_pal
    .explicit
start_after_pal:

    /*
     * QEMU reloads the firmware image on reset, but the raw binary does not
     * contain the trailing NOBITS .bss.  Clear it here so guest-requested and
     * monitor-requested resets start with the same firmware state as a fresh
     * process.
     */
    movl r14 = _bss_start
    ;;
    movl r15 = _bss_end
    ;;
1:
    cmp.ltu p6, p7 = r14, r15
    ;;
(p7) br.cond.dpnt.few 2f
    ;;
    st8 [r14] = r0, 8
    ;;
    br.cond.sptk.few 1b
    ;;
2:
    ;;

    /* GP = linker-defined __gp (already at runtime VMA=0x100000 address) */
    movl r1 = __gp
    ;;

    /*
     * Preserve the reset return pointer across the ABI call, then move the
     * firmware stack to the top of installed low RAM.
     */
    mov r4 = b0
    ;;
    br.call.sptk.many b0 = fw_boot_stack_top
    ;;
    /*
     * The region top may be the exact end of installed RAM, and the psABI
     * lets callees use the 16-byte scratch area [sp, sp+16).  Start sp 16
     * bytes below the region top so that scratch area stays inside RAM.
     */
    adds r12 = -16, r8
    mov r3 = r4
    ;;

    /* Alloc: 0 ins, 13 locals, 3 outs → output regs r45-r47
     * After br.call these become callee's input r32-r34. */
    alloc r2 = ar.pfs, 0, 13, 3, 0
    ;;

    /* Pass args: gp, stack region top, boot_b0 */
    mov r45 = r1               /* arg0 → callee r32 = gp      */
    mov r46 = r8               /* arg1 → callee r33 = stack region top */
    mov r47 = r3               /* arg2 → callee r34 = boot_b0 */
    ;;

    br.call.sptk.many b0 = firmware_main
    ;;

    /* firmware_main should not return */
    break 0
    ;;
    .endp start_after_pal
    .size start_after_pal, . - start_after_pal

    .section .text.start_secondary, "ax"
    .type start_secondary, @function
    .proc start_secondary
    .explicit
start_secondary:
    movl r1 = __gp
    ;;
    mov r14 = cr.lid
    ;;
    extr.u r14 = r14, 24, 8
    ;;
    alloc r2 = ar.pfs, 0, 3, 1, 0
    ;;
    mov r32 = r14
    mov r35 = r14
    ;;
    br.call.sptk.many b0 = fw_ap_stack_top
    ;;
    adds r12 = -16, r8
    mov r35 = r32
    ;;
    br.call.sptk.many b0 = firmware_ap_main
    ;;
    break 0
    ;;
    .endp start_secondary
    .size start_secondary, . - start_secondary

    .section .text.fw_pal_halt_light, "ax"
    .global fw_pal_halt_light
    .type fw_pal_halt_light, @function
    .proc fw_pal_halt_light
    .explicit
fw_pal_halt_light:
    alloc r33 = ar.pfs, 0, 2, 0, 0
    ;;
    mov r32 = b0
    movl r28 = 0x1d
    ;;
    movl r14 = pal_proc_entry
    ;;
    mov b6 = r14
    movl r14 = 1f
    ;;
    mov b0 = r14
    ;;
    br.sptk.many b6
    ;;
1:
    mov b0 = r32
    mov ar.pfs = r33
    ;;
    br.ret.sptk.many b0
    ;;
    .endp fw_pal_halt_light
    .size fw_pal_halt_light, . - fw_pal_halt_light

    .section .text.fw_pal_platform_addr, "ax"
    .global fw_pal_platform_addr
    .type fw_pal_platform_addr, @function
    .proc fw_pal_platform_addr
    .explicit
fw_pal_platform_addr:
    alloc r34 = ar.pfs, 1, 2, 0, 0
    ;;
    mov r33 = b0
    mov r30 = r32
    movl r28 = 0x10
    ;;
    adds r29 = 1, r0
    mov r31 = r0
    movl r14 = pal_proc_entry
    ;;
    mov b6 = r14
    movl r14 = 1f
    ;;
    mov b0 = r14
    ;;
    br.sptk.many b6
    ;;
1:
    cmp.eq p6, p7 = r8, r0
    ;;
(p6) mov ar.k0 = r32
    ;;
    mov b0 = r33
    mov ar.pfs = r34
    ;;
    br.ret.sptk.many b0
    ;;
    .endp fw_pal_platform_addr
    .size fw_pal_platform_addr, . - fw_pal_platform_addr

    .section .text.fw_pal_itc_ratio, "ax"
    .global fw_pal_itc_ratio
    .type fw_pal_itc_ratio, @function
    .proc fw_pal_itc_ratio
    .explicit
fw_pal_itc_ratio:
    alloc r33 = ar.pfs, 0, 2, 0, 0
    ;;
    mov r32 = b0
    movl r28 = 0xe
    ;;
    mov r29 = r0
    mov r30 = r0
    mov r31 = r0
    movl r14 = pal_proc_entry
    ;;
    mov b6 = r14
    movl r14 = 1f
    ;;
    mov b0 = r14
    ;;
    br.sptk.many b6
    ;;
1:
    cmp.eq p6, p7 = r8, r0
    ;;
(p6) mov r8 = r11
(p7) mov r8 = r0
    ;;
    mov b0 = r32
    mov ar.pfs = r33
    ;;
    br.ret.sptk.many b0
    ;;
    .endp fw_pal_itc_ratio
    .size fw_pal_itc_ratio, . - fw_pal_itc_ratio

    .section .text.fw_ebc_bridge, "ax"

    /* A generated thunk passes its firmware-owned context in r8. */
    .global fw_ebc_capture_context
    .type fw_ebc_capture_context, @function
    .proc fw_ebc_capture_context
    .explicit
fw_ebc_capture_context:
    br.ret.sptk.many b0
    ;;
    .endp fw_ebc_capture_context
    .size fw_ebc_capture_context, . - fw_ebc_capture_context

    /*
     * Invoke an IA-64 function descriptor from an EBC argument frame.
     * The first eight natural-width arguments are loaded into the register
     * stack.  The complete frame is copied to a temporary native stack so
     * that additional arguments remain at their ABI-defined locations.
     */
    .global fw_ebc_call_native
    .type fw_ebc_call_native, @function
    .proc fw_ebc_call_native
    .explicit
fw_ebc_call_native:
    alloc loc1 = ar.pfs, 3, 9, 8, 0
    ;;
    mov loc0 = b0
    mov loc2 = r12
    mov loc3 = r1
    sub loc4 = in2, in1
    ;;
    sub loc5 = r12, loc4
    mov loc6 = in1
    mov loc8 = loc4
    ;;
    and loc5 = -16, loc5
    ;;
    mov loc7 = loc5
    ;;
1:
    cmp.eq p6, p7 = loc8, r0
    ;;
(p6) br.cond.dpnt.few 2f
    ;;
    ld1 r14 = [loc6], 1
    ;;
    st1 [loc7] = r14, 1
    add loc8 = -1, loc8
    ;;
    br.cond.sptk.few 1b
    ;;
2:
    mov r14 = loc5
    ;;
    ld8 out0 = [r14], 8
    ;;
    ld8 out1 = [r14], 8
    ;;
    ld8 out2 = [r14], 8
    ;;
    ld8 out3 = [r14], 8
    ;;
    ld8 out4 = [r14], 8
    ;;
    ld8 out5 = [r14], 8
    ;;
    ld8 out6 = [r14], 8
    ;;
    ld8 out7 = [r14], 8
    ;;
    add r12 = 48, loc5
    mov r14 = in0
    ;;
    ld8 r15 = [r14], 8
    ;;
    ld8 r16 = [r14]
    ;;
    mov b6 = r15
    mov r1 = r16
    ;;
    br.call.sptk.many b0 = b6
    ;;
    mov r12 = loc2
    mov r1 = loc3
    mov b0 = loc0
    mov ar.pfs = loc1
    ;;
    br.ret.sptk.many b0
    ;;
    .endp fw_ebc_call_native
    .size fw_ebc_call_native, . - fw_ebc_call_native
