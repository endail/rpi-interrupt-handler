CXX := g++
INCDIR := include
SRCDIR := src
BUILDDIR := build
BINDIR := bin
SRCEXT := cpp
LIBS := -lrt -lcrypt -pthread
INC := -I $(INCDIR)
CFLAGS :=	-O2 \
			-pipe \
			-fomit-frame-pointer \
			-Wall \
			-Wfatal-errors \
			-Werror=format-security \
			-Wl,-z,relro \
			-Wl,-z,now \
			-Wl,-z,defs	\
			-Wl,--hash-style=gnu \
			-Wl,--as-needed \
			-D_FORTIFY_SOURCE=2 \
			-fstack-clash-protection \
			-v


########################################################################

# if dev env var, set compiler to rpi toolchain compiler
ifneq ($(DEV),)
	CXX := ${RPI_TOOLCHAIN}/bin/arm-linux-gnueabihf-g++.exe
endif


# https://stackoverflow.com/a/39895302/570787
ifeq ($(PREFIX),)
	PREFIX := /usr/local
endif


ifeq ($(GITHUB_ACTIONS),false)
# only include these flags on rpi, not gha
	CFLAGS := 	-march=native \
				-mfpu=vfp \
				-mfloat-abi=hard \
				$(CFLAGS)
endif


CXXFLAGS := -std=c++11 \
			-fexceptions \
			$(CFLAGS)

########################################################################



.PHONY: all
all: 	dirs \
		$(BUILDDIR)/Interrupter.o \
		test

.PHONY: dirs
dirs:
ifeq ($(OS),Windows_NT)
	if not exist $(BUILDDIR) mkdir $(BUILDDIR)
	if not exist $(BINDIR) mkdir $(BINDIR)
else
	mkdir -p $(BUILDDIR)
	mkdir -p $(BINDIR)
endif

$(BUILDDIR)/%.o: $(SRCDIR)/%.$(SRCEXT)
	$(CXX) $(CXXFLAGS) $(INC) -c -o $@ $<

.PHONY: test
test: $(BUILDDIR)/test.o
	$(CXX) $(CXXFLAGS) $(INC) \
		-o $(BINDIR)/test \
		$(BUILDDIR)/test.o \
		$(BUILDDIR)/Interrupter.o \
		$(LIBS) -lwiringPi

.PHONY: clean
clean:
ifeq ($(OS),Windows_NT)
	del /S /Q $(BUILDDIR)
	del /S /Q $(BINDIR)
else
	rm -rfv $(BUILDDIR)
	rm -rfv $(BINDIR)
endif
	rmdir $(BUILDDIR)
	rmdir $(BINDIR)