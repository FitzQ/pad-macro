.PHONY: all overlay sysmodule clean

all: overlay sysmodule

overlay:
	$(MAKE) -C overlay $(MAKEFLAGS)
	@rm -rf out/switch/.overlay
	@mkdir -p out/switch
	@mv -f overlay/out/switch/.overlay out/switch

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