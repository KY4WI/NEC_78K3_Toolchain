; Minimal cross-object symbol reference for the linker smoke test.
; This is NOT a bootable radio image.
    .global _add
    .section .text
_start:
    call _add
halt:
    br halt
