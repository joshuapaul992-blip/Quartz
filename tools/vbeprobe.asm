; vbeprobe.asm - real-mode VBE capability probe (DOS .COM)
; Lists VESA controller info and all modes that expose a linear framebuffer
; (LFB) with bpp >= 15. Output via BIOS teletype. Assemble:  nasm -f bin
; Purpose: confirm the target VGA offers LFB modes before building the
; protected-mode Quartz graphics core.

org 0x100
bits 16

start:
    mov ax, 0x0003          ; 80x25 text, clear screen
    int 0x10

    mov si, msg_hdr
    call puts

    ; --- VBE controller info (4F00) ---
    mov di, vinfo
    mov cx, 256
    xor ax, ax
    push di
    rep stosw               ; zero the 512-byte block
    pop di
    mov dword [vinfo], 'VBE2' ; request VBE 2.0+ info
    mov ax, 0x4F00
    mov di, vinfo
    int 0x10
    cmp ax, 0x004F
    jne .no_vbe
    cmp dword [vinfo], 'VESA'
    jne .no_vbe

    mov si, msg_ver
    call puts
    mov ax, [vinfo+4]       ; VbeVersion
    call puthex16
    call crlf

    mov si, msg_mem
    call puts
    mov ax, [vinfo+18]      ; TotalMemory in 64KB blocks
    call puthex16
    mov si, msg_blk
    call puts
    call crlf

    mov si, msg_modes
    call puts

    ; --- walk the mode list ---
    mov ax, [vinfo+16]      ; VideoModePtr offset
    mov [mptr], ax
    mov ax, [vinfo+14+2-2]  ; (placeholder, replaced below)
    ; load segment:offset of VideoModePtr properly
    mov ax, [vinfo+14]      ; offset
    mov [mptr], ax
    mov ax, [vinfo+16]      ; segment
    mov [mseg], ax

.next_mode:
    push ds
    mov ds, [mseg]
    mov bx, [mptr]
    mov cx, [bx]            ; mode number
    pop ds
    cmp cx, 0xFFFF
    je .done
    add word [mptr], 2
    mov [curmode], cx

    ; get mode info
    mov di, minfo
    mov ax, 0x4F01
    mov cx, [curmode]
    int 0x10
    cmp ax, 0x004F
    jne .next_mode

    mov ax, [minfo]         ; ModeAttributes
    test ax, 0x80           ; bit7 = linear framebuffer available
    jz .next_mode
    test ax, 0x01           ; bit0 = mode supported in hardware
    jz .next_mode
    mov al, [minfo+25]      ; bpp
    cmp al, 15
    jb .next_mode

    ; print: " <mode> <W>x<H> <bpp>bpp phys=<addr>"
    mov si, pad
    call puts
    mov ax, [curmode]
    call puthex16
    mov al, ' '
    call putc
    mov ax, [minfo+18]      ; XResolution
    call puthex16
    mov al, 'x'
    call putc
    mov ax, [minfo+20]      ; YResolution
    call puthex16
    mov al, ' '
    call putc
    xor ah, ah
    mov al, [minfo+25]      ; bpp
    call puthex16
    mov si, msg_phys
    call puts
    mov ax, [minfo+42]      ; PhysBasePtr high word
    call puthex16
    mov ax, [minfo+40]      ; PhysBasePtr low word
    call puthex16
    call crlf
    jmp .next_mode

.no_vbe:
    mov si, msg_novbe
    call puts
.done:
    mov si, msg_end
    call puts
    ; wait for a key then exit (so screenshot can be taken any time)
    xor ax, ax
    int 0x16
    mov ax, 0x4C00
    int 0x21

; ---- helpers ----
puts:                       ; DS:SI -> $-terminated string
    lodsb
    cmp al, '$'
    je .r
    call putc
    jmp puts
.r: ret

putc:                       ; AL = char
    push ax
    push bx
    mov ah, 0x0E
    xor bx, bx
    int 0x10
    pop bx
    pop ax
    ret

crlf:
    mov al, 13
    call putc
    mov al, 10
    call putc
    ret

puthex16:                   ; AX = word -> 4 hex digits
    push ax
    mov al, ah
    call puthex8
    pop ax
    call puthex8
    ret
puthex8:                    ; AL = byte -> 2 hex digits
    push ax
    shr al, 4
    call puthex4
    pop ax
    and al, 0x0F
    call puthex4
    ret
puthex4:
    and al, 0x0F
    cmp al, 10
    jb .d
    add al, 'A'-10
    jmp putc
.d: add al, '0'
    jmp putc

msg_hdr   db 'Quartz VBE probe',13,10,'================',13,10,'$'
msg_ver   db 'VBE version : 0x$'
msg_mem   db 'Video memory: 0x$'
msg_blk   db ' x 64KB$'
msg_modes db 'LFB modes (mode WxH bpp phys), hex:',13,10,'$'
msg_phys  db ' phys=0x$'
msg_novbe db '*** NO VESA VBE DETECTED ***',13,10,'$'
msg_end   db '---- end (press a key) ----',13,10,'$'
pad       db ' $'

mptr      dw 0
mseg      dw 0
curmode   dw 0

align 2
vinfo     times 512 db 0
minfo     times 256 db 0
