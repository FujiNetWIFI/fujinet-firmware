###############################################################################
### Generic Makefile for cc65 projects - full version with abstract options ###
### V1.3.0(w) 2010 - 2013 Oliver Schmidt & Patryk "Silver Dream !" Łogiewa  ###
###############################################################################
 
###############################################################################
### In order to override defaults - values can be assigned to the variables ###
###############################################################################
 
# Space or comma separated list of cc65 supported target platforms to build for.
# Default: c64 (lowercase!)
TARGETS := atari
 
# Name of the final, single-file executable.
# Default: name of the current dir with target name appended
PROGRAM := config
 
# Path(s) to additional libraries required for linking the program
# Use only if you don't want to place copies of the libraries in SRCDIR
# Default: none
LIBS	:= 

# Custom linker configuration file
# Use only if you don't want to place it in SRCDIR
# Default: none
CONFIG  :=
 
# Additional C compiler flags and options.
# Default: none
CFLAGS  = -Oris
 
# Additional assembler flags and options.
# Default: none
ASFLAGS =
 
# Additional linker flags and options.
# Default: none
LDFLAGS = $(LDFLAGS.$(TARGETS))
LDFLAGS.atari = --mapfile config.map
 
# Path to the directory containing C and ASM sources.
# Default: src
SRCDIR :=
 
# Path to the directory where object files are to be stored (inside respective target subdirectories).
# Default: obj
OBJDIR :=
 
# Command used to run the emulator.
# Default: depending on target platform. For default (c64) target: x64 -kernal kernal -VICIIdsize -autoload
EMUCMD :=
 
# Optional commands used before starting the emulation process, and after finishing it.
# Default: none
#PREEMUCMD := osascript -e "tell application \"System Events\" to set isRunning to (name of processes) contains \"X11.bin\"" -e "if isRunning is true then tell application \"X11\" to activate"
#PREEMUCMD := osascript -e "tell application \"X11\" to activate"
#POSTEMUCMD := osascript -e "tell application \"System Events\" to tell process \"X11\" to set visible to false"
#POSTEMUCMD := osascript -e "tell application \"Terminal\" to activate"
PREEMUCMD :=
POSTEMUCMD :=
 
# On Windows machines VICE emulators may not be available in the PATH by default.
# In such case, please set the variable below to point to directory containing
# VICE emulators. 
#VICE_HOME := "C:\Program Files\WinVICE-2.2-x86\"
VICE_HOME :=
 
# Options state file name. You should not need to change this, but for those
# rare cases when you feel you really need to name it differently - here you are
STATEFILE := Makefile.options
 
###################################################################################
####  DO NOT EDIT BELOW THIS LINE, UNLESS YOU REALLY KNOW WHAT YOU ARE DOING!  ####
###################################################################################
 
###################################################################################
### Mapping abstract options to the actual compiler, assembler and linker flags ###
### Predefined compiler, assembler and linker flags, used with abstract options ###
### valid for 2.14.x. Consult the documentation of your cc65 version before use ###
###################################################################################
 
# Compiler flags used to tell the compiler to optimise for SPEED
define _optspeed_
  CFLAGS += -Oris
endef
 
# Compiler flags used to tell the compiler to optimise for SIZE
define _optsize_
  CFLAGS += -Or
endef
 
# Compiler and assembler flags for generating listings
define _listing_
  CFLAGS += --listing $$(@:.o=.lst)
  ASFLAGS += --listing $$(@:.o=.lst)
  REMOVES += $(addsuffix .lst,$(basename $(OBJECTS)))
endef
 
# Linker flags for generating map file
define _mapfile_
  LDFLAGS += --mapfile $$@.map
  REMOVES += $(PROGRAM).map
endef
 
# Linker flags for generating VICE label file
define _labelfile_
  LDFLAGS += -Ln $$@.lbl
  REMOVES += $(PROGRAM).lbl
endef
 
# Linker flags for generating a debug file
define _debugfile_
  LDFLAGS += -Wl --dbgfile,$$@.dbg
  REMOVES += $(PROGRAM).dbg
endef
 
###############################################################################
###  Defaults to be used if nothing defined in the editable sections above  ###
###############################################################################
 
# Presume the C64 target like the cl65 compile & link utility does.
# Set TARGETS to override.
ifeq ($(TARGETS),)
  TARGETS := c64
endif
 
# Presume we're in a project directory so name the program like the current
# directory. Set PROGRAM to override.
ifeq ($(PROGRAM),)
  PROGRAM := $(notdir $(CURDIR))
endif
 
# Presume the C and asm source files to be located in the subdirectory 'src'.
# Set SRCDIR to override.
ifeq ($(SRCDIR),)
  SRCDIR := src
endif
 
# Presume the object and dependency files to be located in the subdirectory
# 'obj' (which will be created). Set OBJDIR to override.
ifeq ($(OBJDIR),)
  OBJDIR := obj
endif
TARGETOBJDIR := $(OBJDIR)/$(TARGETS)
 
# On Windows it is mandatory to have CC65_HOME set. So do not unnecessarily
# rely on cl65 being added to the PATH in this scenario.
ifdef CC65_HOME
  CC := $(CC65_HOME)/bin/cl65
else
  CC := cl65
endif
 
# Default emulator commands and options for particular targets.
# Set EMUCMD to override.
c64_EMUCMD := $(VICE_HOME)xscpu64 -VICIIdsize -autostart 
c128_EMUCMD := $(VICE_HOME)x128 -kernal kernal -VICIIdsize -autoload
vic20_EMUCMD := $(VICE_HOME)xvic -kernal kernal -VICdsize -autoload
pet_EMUCMD := $(VICE_HOME)xpet -Crtcdsize -autoload
plus4_EMUCMD := $(VICE_HOME)xplus4 -TEDdsize -autoload
# So far there is no x16 emulator in VICE (why??) so we have to use xplus4 with -memsize option
c16_EMUCMD := $(VICE_HOME)xplus4 -ramsize 16 -TEDdsize -autoload
cbm510_EMUCMD := $(VICE_HOME)xcbm2 -model 510 -VICIIdsize -autoload
cbm610_EMUCMD := $(VICE_HOME)xcbm2 -model 610 -Crtcdsize -autoload
atari_EMUCMD := atari800 -windowed -xl -pal -nopatchall -run
 
ifeq ($(EMUCMD),)
  EMUCMD = $($(CC65TARGET)_EMUCMD)
endif
 
###############################################################################
### The magic begins                                                        ###
###############################################################################
 
# The "Native Win32" GNU Make contains quite some workarounds to get along with
# cmd.exe as shell. However it does not provide means to determine that it does
# actually activate those workarounds. Especially does $(SHELL) NOT contain the
# value 'cmd.exe'. So the usual way to determine if cmd.exe is being used is to
# execute the command 'echo' without any parameters. Only cmd.exe will return a
# non-empy string - saying 'ECHO is on/off'.
#
# Many "Native Win32" prorams accept '/' as directory delimiter just fine. How-
# ever the internal commands of cmd.exe generally require '\' to be used.
#
# cmd.exe has an internal command 'mkdir' that doesn't understand nor require a
# '-p' to create parent directories as needed.
#
# cmd.exe has an internal command 'del' that reports a syntax error if executed
# without any file so make sure to call it only if there's an actual argument.
ifeq ($(shell echo),)
  MKDIR = mkdir -p $1
  RMDIR = rmdir $1
  RMFILES = $(RM) $1
else
  MKDIR = mkdir $(subst /,\,$1)
  RMDIR = rmdir $(subst /,\,$1)
  RMFILES = $(if $1,del /f $(subst /,\,$1))
endif
COMMA := ,
SPACE := $(N/A) $(N/A)
define NEWLINE
 
 
endef
# Note: Do not remove any of the two empty lines above !
 
TARGETLIST := $(subst $(COMMA),$(SPACE),$(TARGETS))
 
ifeq ($(words $(TARGETLIST)),1)
 
# Set PROGRAM to something like 'myprog.c64'.
override PROGRAM := $(PROGRAM).com
 
# Set SOURCES to something like 'src/foo.c src/bar.s'.
# Use of assembler files with names ending differently than .s is deprecated!
SOURCES := $(wildcard $(SRCDIR)/*.c)
SOURCES += $(wildcard $(SRCDIR)/*.s)
SOURCES += $(wildcard $(SRCDIR)/*.asm)
SOURCES += $(wildcard $(SRCDIR)/*.a65)
 
# Add to SOURCES something like 'src/c64/me.c src/c64/too.s'.
# Use of assembler files with names ending differently than .s is deprecated!
SOURCES += $(wildcard $(SRCDIR)/$(TARGETLIST)/*.c)
SOURCES += $(wildcard $(SRCDIR)/$(TARGETLIST)/*.s)
SOURCES += $(wildcard $(SRCDIR)/$(TARGETLIST)/*.asm)
SOURCES += $(wildcard $(SRCDIR)/$(TARGETLIST)/*.a65)
 
# Set OBJECTS to something like 'obj/c64/foo.o obj/c64/bar.o'.
OBJECTS := $(addsuffix .o,$(basename $(addprefix $(TARGETOBJDIR)/,$(notdir $(SOURCES)))))
 
# Set DEPENDS to something like 'obj/c64/foo.d obj/c64/bar.d'.
DEPENDS := $(OBJECTS:.o=.d)
 
# Add to LIBS something like 'src/foo.lib src/c64/bar.lib'.
LIBS += $(wildcard $(SRCDIR)/*.lib)
LIBS += $(wildcard $(SRCDIR)/$(TARGETLIST)/*.lib)
 
# Add to CONFIG something like 'src/c64/bar.cfg src/foo.cfg'.
CONFIG += $(wildcard $(SRCDIR)/$(TARGETLIST)/*.cfg)
CONFIG += $(wildcard $(SRCDIR)/*.cfg)
 
# Select CONFIG file to use. Target specific configs have higher priority.
ifneq ($(word 2,$(CONFIG)),)
  CONFIG := $(firstword $(CONFIG))
  $(info Using config file $(CONFIG) for linking)
endif
 
.SUFFIXES:
.PHONY: all test clean zap love
 
all: $(PROGRAM)
 
-include $(DEPENDS)
-include $(STATEFILE)
 
# If OPTIONS are given on the command line then save them to STATEFILE
# if (and only if) they have actually changed. But if OPTIONS are not
# given on the command line then load them from STATEFILE. Have object
# files depend on STATEFILE only if it actually exists.
ifeq ($(origin OPTIONS),command line)
  ifneq ($(OPTIONS),$(_OPTIONS_))
    ifeq ($(OPTIONS),)
      $(info Removing OPTIONS)
      $(shell $(RM) $(STATEFILE))
      $(eval $(STATEFILE):)
    else
      $(info Saving OPTIONS=$(OPTIONS))
      $(shell echo _OPTIONS_=$(OPTIONS) > $(STATEFILE))
    endif
    $(eval $(OBJECTS): $(STATEFILE))
  endif
else
  ifeq ($(origin _OPTIONS_),file)
    $(info Using saved OPTIONS=$(_OPTIONS_))
    OPTIONS = $(_OPTIONS_)
    $(eval $(OBJECTS): $(STATEFILE))
  endif
endif
 
# Transform the abstract OPTIONS to the actual cc65 options.
$(foreach o,$(subst $(COMMA),$(SPACE),$(OPTIONS)),$(eval $(_$o_)))
 
# Strip potential variant suffix from the actual cc65 target.
CC65TARGET := $(firstword $(subst .,$(SPACE),$(TARGETLIST)))
 
# The remaining targets.
$(TARGETOBJDIR):
	$(call MKDIR,$@)
 
vpath %.c $(SRCDIR)/$(TARGETLIST) $(SRCDIR)
 
$(TARGETOBJDIR)/%.o: %.c | $(TARGETOBJDIR)
	$(CC) -t $(CC65TARGET) -c --create-dep $(@:.o=.d) $(CFLAGS) -o $@ $<
 
vpath %.s $(SRCDIR)/$(TARGETLIST) $(SRCDIR)
 
$(TARGETOBJDIR)/%.o: %.s | $(TARGETOBJDIR)
	$(CC) -t $(CC65TARGET) -Wa -DDYN_DRV=0 -c --create-dep $(@:.o=.d) $(ASFLAGS) -o $@ $<
 
vpath %.asm $(SRCDIR)/$(TARGETLIST) $(SRCDIR)
 
$(TARGETOBJDIR)/%.o: %.asm | $(TARGETOBJDIR)
	$(CC) -t $(CC65TARGET) -c --create-dep $(@:.o=.d) $(ASFLAGS) -o $@ $<
 
vpath %.a65 $(SRCDIR)/$(TARGETLIST) $(SRCDIR)
 
$(TARGETOBJDIR)/%.o: %.a65 | $(TARGETOBJDIR)
	$(CC) -t $(CC65TARGET) -c --create-dep $(@:.o=.d) $(ASFLAGS) -o $@ $<
 
$(PROGRAM): $(CONFIG) $(OBJECTS) $(LIBS)
	$(CC) -t $(CC65TARGET) $(LDFLAGS) -o $@ $(patsubst %.cfg,-C %.cfg,$^)
 
test: $(PROGRAM)
	$(PREEMUCMD)
	$(EMUCMD) $<
	$(POSTEMUCMD)
 
clean:
	$(call RMFILES,$(OBJECTS))
	$(call RMFILES,$(DEPENDS))
	$(call RMFILES,$(REMOVES))
	$(call RMFILES,$(PROGRAM))
 
else # $(words $(TARGETLIST)),1
 
all test clean:
	$(foreach t,$(TARGETLIST),$(MAKE) TARGETS=$t $@$(NEWLINE))
 
endif # $(words $(TARGETLIST)),1
 
OBJDIRLIST := $(wildcard $(OBJDIR)/*)
 
zap:
	$(foreach o,$(OBJDIRLIST),-$(call RMFILES,$o/*.o $o/*.d $o/*.lst)$(NEWLINE))
	$(foreach o,$(OBJDIRLIST),-$(call RMDIR,$o)$(NEWLINE))
	-$(call RMDIR,$(OBJDIR))
	-$(call RMFILES,$(basename $(PROGRAM)).* $(STATEFILE))
 
love:
	@echo "Not war, eh?"

###################################################################
###  Place your additional targets in the additional Makefiles  ###
### in the same directory - their names have to end with ".mk"! ###
###################################################################
-include *.mk
