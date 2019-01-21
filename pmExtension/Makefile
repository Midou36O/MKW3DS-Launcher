rwildcard = $(foreach d, $(wildcard $1*), $(filter $(subst *, %, $2), $d) $(call rwildcard, $d/, $2))

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

include $(DEVKITARM)/3ds_rules

name := $(shell basename $(CURDIR))

dir_source := source
dir_build := build
dir_out := $(dir_build)

LIBS := -lctru
LIBDIRS	:= $(CTRULIB)
LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

INCLUDE	:= $(foreach dir,$(LIBDIRS),-I$(dir)/include)

ARCH 	:= -mcpu=mpcore -mfloat-abi=hard
ASFLAGS := $(ARCH)
CFLAGS  := -Wall -Wextra $(ARCH) -fno-builtin -std=c11 -Wno-main -Os -flto -ffast-math $(INCLUDE) -fomit-frame-pointer -ffunction-sections -fdata-sections -DARM11 -D_3DS
LDFLAGS := -fno-builtin -nostartfiles $(ARCH) -T linker.ld -Wl,--section-start,.text=0x0010BA00 -Wl,--gc-sections

objects = $(patsubst $(dir_source)/%.s, $(dir_build)/%.o, \
          $(patsubst $(dir_source)/%.c, $(dir_build)/%.o, \
          $(call rwildcard, $(dir_source), *.s *.c)))

bin2c = C:\\devkitPro\\devkitPSP\\bin\\bin2c

#bundled =  $(dir_build)/pmExtension.bin.o

define bin2o
	bin2s $< | $(AS) -o $(@)
endef

.PHONY: all
all: $(dir_out)/$(name).bin

.PHONY: clean
clean:
	@rm -rf $(dir_build)

$(dir_out)/$(name).bin: $(dir_build)/$(name).elf
	$(OBJCOPY) -S -O binary $< $@
	python3 bin2shark.py

$(dir_build)/$(name).elf: $(objects)
	$(LINK.o) $(OUTPUT_OPTION) $^ $(LIBPATHS) $(LIBS)

$(dir_build)/bundled.h: $(bundled)
	@$(foreach f, $(bundled),\
	echo "extern const u8" `(echo $(basename $(notdir $(f))) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`"[];" >> $@;\
	echo "extern const u32" `(echo $(basename $(notdir $(f)))| sed -e 's/^\([0-9]\)/_\1/' | tr . _)`_size";" >> $@;\
	)

$(dir_build)/%.o: $(dir_source)/%.c $(dir_build)/bundled.h
	@mkdir -p "$(@D)"
	$(COMPILE.c) $(OUTPUT_OPTION) $<

$(dir_build)/%.o: $(dir_source)/%.s
	@mkdir -p "$(@D)"
	$(COMPILE.s) $(OUTPUT_OPTION) $<

