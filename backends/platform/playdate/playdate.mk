# Playdate-specific makefile

PLAYDATE_PRODUCT = scummvm.pdx

$(info ====== PLAYDATE.MK DEBUG ======)
$(info EXECUTABLE=$(EXECUTABLE))
$(info EXEPRE=$(EXEPRE))
$(info EXEEXT=$(EXEEXT))
$(info OBJS count=$(words $(OBJS)))
$(info MODULES=$(MODULES))
$(info ================================)

all: $(PLAYDATE_PRODUCT)

clean: playdate_clean

playdate_clean:
	$(RM) -r $(PLAYDATE_PRODUCT)
	$(RM) $(EXECUTABLE)

# Create Playdate PDX bundle
$(PLAYDATE_PRODUCT): $(EXECUTABLE)
	@echo "====== Creating Playdate PDX bundle ======"
	@echo "EXECUTABLE file: $(EXECUTABLE)"
	@ls -lah $(EXECUTABLE) || echo "ERROR: $(EXECUTABLE) not found!"
	@mkdir -p $(PLAYDATE_PRODUCT)
	@cp $(EXECUTABLE) $(PLAYDATE_PRODUCT)/pdex.bin
	@# Copy themes if they exist
	@if [ -d "gui/themes" ]; then \
		mkdir -p $(PLAYDATE_PRODUCT)/themes; \
		cp -r gui/themes/*.dat $(PLAYDATE_PRODUCT)/themes/ 2>/dev/null || true; \
	fi
	@# Copy engine data if it exists
	@if [ -d "dists/engine-data" ]; then \
		mkdir -p $(PLAYDATE_PRODUCT)/engine-data; \
		cp dists/engine-data/*.dat $(PLAYDATE_PRODUCT)/engine-data/ 2>/dev/null || true; \
	fi
	@# Create pdxinfo file
	@echo "name=ScummVM" > $(PLAYDATE_PRODUCT)/pdxinfo
	@echo "author=ScummVM Team" >> $(PLAYDATE_PRODUCT)/pdxinfo
	@echo "description=AGI and SCI game engine" >> $(PLAYDATE_PRODUCT)/pdxinfo
	@echo "bundleID=org.scummvm.playdate" >> $(PLAYDATE_PRODUCT)/pdxinfo
	@echo "version=$(VERSION)" >> $(PLAYDATE_PRODUCT)/pdxinfo
	@echo "buildNumber=1" >> $(PLAYDATE_PRODUCT)/pdxinfo
	@echo "imagePath=icon" >> $(PLAYDATE_PRODUCT)/pdxinfo
	@echo "Playdate PDX bundle created at $(PLAYDATE_PRODUCT)"

.PHONY: playdate_clean
