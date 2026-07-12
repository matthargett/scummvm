# Playdate port: package the executable into a pdx bundle with the
# official pdc tool from the Playdate SDK.

PDX_NAME = scummvm.pdx
PDX_SOURCE_DIR = pdx-source

ifdef PLAYDATE_SIMULATOR
PDX_BINARY = $(PDX_SOURCE_DIR)/pdex.so
else
PDX_BINARY = $(PDX_SOURCE_DIR)/pdex.elf
endif

all: $(PDX_NAME)

$(PDX_NAME): $(EXECUTABLE)
	$(MKDIR) $(PDX_SOURCE_DIR)
	$(CP) $(EXECUTABLE) $(PDX_BINARY)
	echo "name=ScummVM" > $(PDX_SOURCE_DIR)/pdxinfo
	echo "author=ScummVM Team" >> $(PDX_SOURCE_DIR)/pdxinfo
	echo "description=Graphic adventure engine (AGI, SCI)" >> $(PDX_SOURCE_DIR)/pdxinfo
	echo "bundleID=org.scummvm.scummvm" >> $(PDX_SOURCE_DIR)/pdxinfo
	echo "version=$(VERSION)" >> $(PDX_SOURCE_DIR)/pdxinfo
	$(PLAYDATE_SDK_PATH)/bin/pdc -sdkpath $(PLAYDATE_SDK_PATH) $(PDX_SOURCE_DIR) $(PDX_NAME)

clean: playdate-clean

playdate-clean:
	$(RM_REC) $(PDX_NAME) $(PDX_SOURCE_DIR)

.PHONY: playdate-clean
