.set MAGIC, 0xE85250D6
.set ARCH, 0  	#i386
.set SIZE, multiboot_end - multiboot_start
.set CHECKSUM, -(MAGIC + SIZE)

.section .multiboot, "ax"
.align 4
multiboot_start:
.long MAGIC
.long ARCH
.long SIZE
.long CHECKSUM

.word 3
.word 0
.long 12
.long _start

.align 8

.word 1 	#type
.word 0 	#flags
.long 28 	#size
.long 6 	#mmap
.long 8 	#framebuffer
.long 9 	#elf map
.long 14    #acpi_old
.long 15 	#acpi_new

.align 8

.word 5
.word 0
.long 20
.long 1024
.long 768
.long 32

.align 8

.word 6
.word 0
.long 8

.align 8

.word 0
.word 0
.long 8

multiboot_end:
                               .section .bootstrap_code, "ax", @progbits
                               .code32

                               temp_stor_info:
                               .long 0
                               .long 0
                               temp_stor_magic:
                               .long 0
                               .long 0
                               gdt64:
                               .nulld:
                               .word 0
                               .word 0
                               .byte 0
                               .byte 0
                               .byte 0
                               .byte 0
                               .coded:
                               .word 0
                               .word 0
                               .byte 0
                               .byte 0b10011010
                               .byte 0b00100000
                               .byte 0
                               .datad:
                               .word 0
                               .word 0
                               .byte 0
                               .byte 0b10010010
                               .byte 0b00000000
                               .byte 0
                               .GDTPTR:
                               .word 23
                               .long gdt64

                               .global _start
                               .type _start, @function
                               _start:
                               mov $0x1000, %edi
                               mov %edi, %cr3

                               mov %eax, (temp_stor_magic)
                               mov %ebx, (temp_stor_info)	#Backup EBX



                               mov $0x80000001, %eax
                               cpuid
                               test $(1 << 29), %edx
                               jz .Crash

                               xor %eax, %eax
                               mov $4096, %ecx
                               rep stosl
                               mov $4096, %ecx
                               rep stosl
                               mov %cr3, %edi
                               movl $0x2003, (%edi)
                               movl $0x5003, 0xFF8(%edi)
                               add $0x1000, %edi
                               movl $0x3003, (%edi)
                               add $0x1000, %edi
                               movl $0x4003, (%edi)
                               add $0x1000, %edi

#This needs to be converted to use 4MB pages instead to support older systems
#Map first 4GB RAM to the top of memory
                               movl $0x0, 0x1FE0(%edi)
                               movl $0x0, 0x1FE8(%edi)
                               movl $0x0, 0x1FF0(%edi)
                               movl $0x0, 0x1FF8(%edi)

                               #Map the bottom of the memory space
                               movl $0x16003, 0x1FF0(%edi)
                               movl $0x17003, 0x1FF8(%edi)

                               mov $0x16000, %edi
                               mov $0x108B, %ebx
                               mov $2048, %ecx

                               .SetGBEntry:
                               movl %ebx, (%edi)
                               movl $0, 4(%edi)
                               add $0x200000, %ebx
                               add $8, %edi
                               loop .SetGBEntry

                               mov $0x4000, %edi
                               mov $0x3, %ebx
                               mov $512, %ecx

                               .SetEntry:
                               movl %ebx, (%edi)
                               add $0x1000, %ebx
                               add $8, %edi
                               loop .SetEntry

#Enable PAE
                               mov %cr4, %eax
                               or $(1 << 5), %eax
                               mov %eax, %cr4

#Set LM bit
                               mov $0xC0000080, %ecx
                               rdmsr
                               or $256, %eax
                               wrmsr

#Enable paging
                               mov %cr0, %eax
                               or $0x80010000, %eax
                               mov %eax, %cr0

#We're in IA-32e mode now, restore the values of eax and ebx so they can be used
#ebx was restored earlier

#Now lets setup a temporary GDT with 64bit descriptors and jump to it
                               lgdt (.GDTPTR)
                               mov %edx, %ebx
                               jmp $0x8, $.Ljmp
                               .Ljmp:
                               .code64
                               mov $0x10, %ax
                               mov %ax, %ds
                               mov %ax, %es
                               mov %ax, %fs
                               mov %ax, %gs
                               mov %ax, %ss
                               mov $0xffffffff80000000, %rsp
                               mov $0xffffffff80000000, %rdi
                               add $stack_top, %rsp
                               mov (temp_stor_magic), %rsi
                               add (temp_stor_info), %rdi
                               mov $0xffffffff80200000, %rax
                               push %rax
                               ret
                               cli
                               hlt
                               .Lhang:
                               jmp .Lhang

                               .Crash:
                               sti
                               int $0x0
                               jmp .Crash

                               .size _start, . - _start

                               .section .bootstrap_stack, "ax"
                               stack_bottom:
                               .skip 16384 # 16 KiB	(16 KiB is enough for initialization until threading is setup)
                               stack_top:

                               .section .bootstrap_pagetable_block, "ax"
                               pagetable_block_area:
                               .skip 16384  # ensure space for bootstrap pagetable isn't taken by something else
