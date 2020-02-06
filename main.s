.global main

.text
main:
    mov $msg, %rdi
    call puts
    ret
.data
msg:
	.asciz "Hello world"
