; getfont.asm - dump the VGA ROM 8x16 font (4096 bytes) to COM1.
; Used on the host to extract a real font for the off-DOS render harness.
org 0x100
bits 16
start:
    mov ax, 0x1130
    mov bh, 0x06            ; 8x16 ROM font -> ES:BP, CX=bytes/char
    int 0x10
    push es
    push bp

    ; init COM1 (0x3F8) 115200 8N1
    mov dx, 0x3FB
    mov al, 0x80
    out dx, al              ; DLAB=1
    mov dx, 0x3F8
    mov al, 0x01
    out dx, al              ; divisor low
    mov dx, 0x3F9
    xor al, al
    out dx, al              ; divisor high
    mov dx, 0x3FB
    mov al, 0x03
    out dx, al              ; 8N1, DLAB=0

    pop bp
    pop es
    mov si, bp
    mov cx, 4096
.next:
    mov dx, 0x3FD
.wait:
    in al, dx
    test al, 0x20           ; THR empty?
    jz .wait
    mov dx, 0x3F8
    mov al, [es:si]
    out dx, al
    inc si
    loop .next

.hlt:
    hlt
    jmp .hlt
