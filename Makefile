CXX := g++
INCDIR := include
SRCDIR := src
BUILDDIR := build
BINDIR := bin
SRCEXT := cpp
LIBS := 
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

# https://stackoverflow.com/a/39895302/570787
ifeq ($(PREFIX),)
	PREFIX := /usr/local
endif


ifeq ($(GITHUB_ACTIONS),true)
# gha needs these additional libs
	LIBS := $(LIBS) -lrt -lcrypt -pthread
else
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
		$(BUILDDIR)/InterruptHandler.o
		test

.PHONY: dirs
dirs:
	mkdir -p $(BINDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.$(SRCEXT)
	$(CXX) $(CXXFLAGS) $(INC) -c -o $@ $<

.PHONY: test
test: $(BUILDDIR)/test.o
	$(CXX) $(CXXFLAGS) $(INC) \
		-o $(BINDIR)/test \
		$(BUILDDIR)/test.o \
		$(LIBS)

.PHONY: clean
clean:
	$(RM) -r $(BUILDDIR)/*
	$(RM) -r $(BINDIR)/*
