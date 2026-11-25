TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

TARGET      := switch-mahjong
BUILD       := build
SOURCES     := source
DATA        := data
INCLUDES    := include
ROMFS       := romfs

APP_TITLE   := 明星三缺一风格迷你麻将
APP_AUTHOR  := HomebrewDev
APP_VERSION := 0.1.0

CXXFLAGS    := -g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES) -std=gnu++17
export LD   := $(CXX)
LDFLAGS     = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map) -Wl,-z,notext

LIBS        := -lSDL2_image -ljpeg -lwebp -lSDL2_mixer -lSDL2_ttf -lSDL2 -lvorbisidec -logg -lmpg123 -lopusfile -lopus -lmodplug -lfreetype -lharfbuzz -lfreetype -lbz2 -lpng -lz -lEGL -lGLESv2 -lglapi -ldrm_nouveau -lnx -lm

LIBDIRS     := $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))

export OFILES   := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o)
export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export ROMFS_DIR := $(CURDIR)/$(ROMFS)
export ROMFS_T   := $(OUTPUT).romfs

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf $(ROMFS_T)

else

$(OUTPUT).nro : $(OUTPUT).elf $(OUTPUT).nacp $(ROMFS_T)
$(OUTPUT).elf : $(OFILES)

$(ROMFS_T): $(ROMFS_DIR)
	@build_romfs $(ROMFS_DIR) $@

%.o : %.cpp
	@echo $(notdir $<)
	@$(CXX) -MMD -MP -MF $(DEPSDIR)/$*.d $(CXXFLAGS) $(INCLUDE) -c $< -o $@

-include $(DEPSDIR)/*.d
endif
