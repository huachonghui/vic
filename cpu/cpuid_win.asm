;/****************************************************************************
; *
; *  XVID MPEG-4 VIDEO CODEC
; *  - CPUID check processors capabilities -
; *
; *  Copyright (C) 2001 Michael Militzer <isibaar@xvid.org>
; *
; *  This program is free software ; you can redistribute it and/or modify
; *  it under the terms of the GNU General Public License as published by
; *  the Free Software Foundation ; either version 2 of the License, or
; *  (at your option) any later version.
; *
; *  This program is distributed in the hope that it will be useful,
; *  but WITHOUT ANY WARRANTY ; without even the implied warranty of
; *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; *  GNU General Public License for more details.
; *
; *  You should have received a copy of the GNU General Public License
; *  along with this program ; if not, write to the Free Software
; *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
; *
; ***************************************************************************/

BITS 32

%macro cglobal 1 
    %ifdef PREFIX
        global _%1 
        %define %1 _%1
    %else
        global %1
    %endif
%endmacro

;=============================================================================
; Constants
;=============================================================================

%define CPUID_TSC               0x00000010
%define CPUID_MMX               0x00800000
%define CPUID_SSE               0x02000000
%define CPUID_SSE2              0x04000000

%define EXT_CPUID_3DNOW         0x80000000
%define EXT_CPUID_AMD_3DNOWEXT  0x40000000
%define EXT_CPUID_AMD_MMXEXT    0x00400000

%define FF_CPU_MMX            0x00000001
%define FF_CPU_MMXEXT         0x00000002
%define FF_CPU_SSE            0x00000004
%define FF_CPU_SSE2           0x00000008
%define FF_CPU_3DNOW          0x00000010
%define FF_CPU_3DNOWEXT       0x00000020
%define FF_CPU_TSC            0x00000040

;=============================================================================
; Read only data
;=============================================================================

ALIGN 32
%ifdef FORMAT_COFF
SECTION .rodata data
%else
SECTION .rodata data align=16
%endif

vendorAMD:
   db "AuthenticAMD"

;=============================================================================
; Macros
;=============================================================================

%macro  CHECK_FEATURE         3
    mov     ecx, %1
    and     ecx, edx
    neg     ecx
    sbb     ecx, ecx
    and     ecx, %2
    or      %3, ecx
%endmacro

;=============================================================================
; Code
;=============================================================================

SECTION .text

; int check_cpu_feature(void)

cglobal check_cpu_features
check_cpu_features:
    
	push ebx
	push esi
	push edi
	push ebp

  sub esp, 12             ; Stack space for vendor name
  
	xor ebp,ebp

    ; CPUID command ?
	pushfd
    pop     eax
    mov     ecx, eax
    xor     eax, 0x200000
    push    eax
    popfd
    pushfd
    pop     eax
    cmp     eax, ecx

    jz      near .cpu_quit      ; no CPUID command -> exit


    ; get vendor string, used later
    xor     eax, eax
    cpuid           
  mov [esp], ebx       ; vendor string
  mov [esp+4], edx
  mov [esp+8], ecx
    test    eax, eax

    jz      near .cpu_quit

    mov     eax, 1 
    cpuid

    ; RDTSC command ?
	CHECK_FEATURE CPUID_TSC, FF_CPU_TSC, ebp

    ; MMX support ?
	CHECK_FEATURE CPUID_MMX, FF_CPU_MMX, ebp

    ; SSE support ?
	CHECK_FEATURE CPUID_SSE, (FF_CPU_MMXEXT|FF_CPU_SSE), ebp

    ; SSE2 support?
	CHECK_FEATURE CPUID_SSE2, FF_CPU_SSE2, ebp

    ; extended functions?
    mov     eax, 0x80000000
    cpuid
    cmp     eax, 0x80000000
    jbe     near .cpu_quit

    mov     eax, 0x80000001
    cpuid
         
    ; AMD cpu ?
    lea     esi, [vendorAMD]
  lea edi, [esp]
    mov     ecx, 12
    cld
    repe    cmpsb
    jnz     .cpu_quit

    ; 3DNow! support ?
	CHECK_FEATURE EXT_CPUID_3DNOW, FF_CPU_3DNOW, ebp

    ; 3DNOW extended ?
	CHECK_FEATURE EXT_CPUID_AMD_3DNOWEXT, FF_CPU_3DNOWEXT, ebp

    ; extended MMX ?
	CHECK_FEATURE EXT_CPUID_AMD_MMXEXT, FF_CPU_MMXEXT, ebp
        
.cpu_quit:  

	mov eax, ebp
    
  add esp, 12

	pop ebp
	pop edi
	pop esi
	pop ebx
    
    ret
.endfunc

; sse/sse2 operating support detection routines
; these will trigger an invalid instruction signal if not supported.
ALIGN 16
cglobal sse_os_trigger
sse_os_trigger:
	xorps xmm0, xmm0
	ret
.endfunc


ALIGN 16
cglobal sse2_os_trigger
sse2_os_trigger:
	xorpd xmm0, xmm0
	ret
.endfunc


