.PHONY: all overlay sysmodule clean timed

# Default target: run overlay and sysmodule builds and print elapsed time
all:
	@start=$$(date +%s); \
	$(MAKE) overlay $(MAKEFLAGS); \
	$(MAKE) sysmodule $(MAKEFLAGS); \
	end=$$(date +%s); \
	elapsed=$$((end-start)); \
	printf "Total elapsed: %02d:%02d\n" $$((elapsed/60)) $$((elapsed%60));

overlay:
	$(MAKE) -C overlay $(MAKEFLAGS)
	@rm -rf out/switch/.overlays
	@mkdir -p out/switch
	@mv -f overlay/out/switch/.overlays out/switch

sysmodule:
	$(MAKE) -C sysmodule $(MAKEFLAGS)
	@rm -rf out/atmosphere out/config out/switch/pad-macro
	@mkdir -p out out/switch
	@mv -f sysmodule/out/atmosphere out
	@mv -f sysmodule/out/config out
	@mv -f sysmodule/out/switch/* out/switch

clean:
	$(MAKE) -C overlay clean
	$(MAKE) -C sysmodule clean