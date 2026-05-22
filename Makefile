# DevOS Build System — Phase 3
# Requires: nasm, x86_64-elf-gcc, x86_64-elf-ld, qemu-system-x86_64

TARGET      := x86_64-linux-gnu
AS          := nasm
CC          := $(TARGET)-gcc
LD          := $(TARGET)-ld
OBJCOPY     := $(TARGET)-objcopy
OBJDUMP     := $(TARGET)-objdump

ASFLAGS     := -f elf64 -g -F dwarf
CFLAGS      := -std=c11                 \
               -ffreestanding           \
               -fno-builtin             \
               -fno-stack-protector     \
               -fno-stack-check         \
               -fno-pie                 \
               -fno-pic                 \
               -mno-red-zone            \
               -mno-mmx                 \
               -mno-sse                 \
               -mno-sse2                \
               -mno-avx                 \
               -mcmodel=kernel          \
               -Wall -Wextra            \
               -Wno-unused-parameter    \
               -O2 -g                   \
               -I kernel/include        \
               -I kernel/arch/x86_64    \
               -I kernel/mm             \
               -I kernel/drivers        \
               -I kernel/lib            \
               -I kernel/sched          \
               -I kernel/syscall        \
               -I kernel/elf            \
               -I kernel/fs \
               -I kernel/shell

LDFLAGS     := -T kernel/linker.ld \
               -nostdlib            \
               -static              \
               -z max-page-size=0x1000

BUILD       := build
IMG         := devos.img
STAGE1_BIN  := $(BUILD)/stage1.bin
STAGE2_BIN  := $(BUILD)/stage2.bin
KERNEL_ELF  := $(BUILD)/kernel.elf
KERNEL_BIN  := $(BUILD)/kernel.bin
KERNEL_MAP  := $(BUILD)/kernel.map

# Userland
USER_ASM    := userland/test.asm
USER_OBJ    := $(BUILD)/userland/test.o
USER_ELF    := $(BUILD)/userland/test.elf
USER_EMB    := $(BUILD)/userland/test_elf_embedded.o

QEMU        := qemu-system-x86_64
QEMU_BASE   := -drive format=raw,file=$(IMG),index=0,media=disk \
               -m 512M -cpu qemu64 -serial stdio                \
               -display none -no-reboot -no-shutdown

ASM_SRCS    := $(shell find kernel -name '*.asm' 2>/dev/null)
C_SRCS      := $(shell find kernel -name '*.c'   2>/dev/null)
ASM_OBJS    := $(patsubst %.asm,$(BUILD)/%.o,$(ASM_SRCS))
C_OBJS      := $(patsubst %.c,$(BUILD)/%.o,$(C_SRCS))
ALL_OBJS    := $(ASM_OBJS) $(C_OBJS) $(USER_EMB)

.PHONY: all run debug size clean userland

all: $(IMG)
	@echo ""
	@$(MAKE) --no-print-directory size

# ---- Userland ----
$(BUILD)/userland:
	mkdir -p $@

$(USER_OBJ): $(USER_ASM) | $(BUILD)/userland
	$(AS) -f elf64 -o $@ $<

$(USER_ELF): $(USER_OBJ) | $(BUILD)/userland
	$(LD) -static -Ttext=0x400000 -nostdlib -o $@ $<

# objcopy creates symbols:
#   _binary_build_userland_test_elf_start
#   _binary_build_userland_test_elf_end
$(USER_EMB): $(USER_ELF) | $(BUILD)/userland
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	    $< $@

userland: $(USER_EMB)

# ---- Boot stages ----
$(STAGE1_BIN): boot/bios/stage1.asm | $(BUILD)
	nasm -f bin -o $@ $<
	@s=$$(stat -c%s $@); [ $$s -eq 512 ] || (echo "stage1 wrong size: $$s"; exit 1)

$(STAGE2_BIN): boot/bios/stage2.asm | $(BUILD)
	nasm -f bin -o $@ $<
	@s=$$(stat -c%s $@); [ $$s -le 2560 ] || (echo "stage2 too big: $$s bytes"; exit 1)

# ---- Kernel objects ----
$(BUILD)/%.o: %.asm | $(BUILD)
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD)/%.o: %.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# ---- Kernel ELF ----
$(KERNEL_ELF): $(ALL_OBJS) kernel/linker.ld
	$(LD) $(LDFLAGS) -Map $(KERNEL_MAP) -o $@ $(ALL_OBJS)

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

# ---- Disk image ----
$(IMG): $(STAGE1_BIN) $(STAGE2_BIN) $(KERNEL_BIN)
	dd if=/dev/zero of=$(IMG) bs=1M count=32 2>/dev/null
	dd if=$(STAGE1_BIN) of=$(IMG) bs=512 count=1    conv=notrunc 2>/dev/null
	dd if=$(STAGE2_BIN) of=$(IMG) bs=512 seek=1     conv=notrunc 2>/dev/null
	dd if=$(KERNEL_BIN) of=$(IMG) bs=512 seek=6     conv=notrunc 2>/dev/null
	@echo "[IMG] MBR@LBA0  Stage2@LBA1  Kernel@LBA6"

$(BUILD):
	mkdir -p $(BUILD)

run: $(IMG)
	$(QEMU) $(QEMU_BASE)

debug: $(IMG) $(KERNEL_ELF)
	$(QEMU) $(QEMU_BASE) -s -S -d int,cpu_reset,guest_errors -D $(BUILD)/qemu.log &
	sleep 0.5
	gdb $(KERNEL_ELF) -ex "target remote :1234"

size: $(KERNEL_ELF)
	@echo "Kernel sections:"
	@$(OBJDUMP) -h $(KERNEL_ELF) | grep -E '\.text|\.rodata|\.data|\.bss'
	@$(TARGET)-size $(KERNEL_ELF)

clean:
	rm -rf $(BUILD) $(IMG)
