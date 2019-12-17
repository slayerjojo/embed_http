PROGRAM := http
OBJDIR	:= obj
SRCEXTS := .c
CPPFLAGS := -g -DDEBUG -errchk=longptr64 -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast
CFLAGS :=
CFLAGS +=
LDFLAGS	+= -Wl,-Map,mapfile
LDFLAGS	+= -lm
LDFLAGS	+= -lpthread
LDFLAGS	+= -lstdc++
CXX = gcc
RM = rm -rf

SHELL = /bin/sh
SOURCES = $(shell find ./ -name "*$(SRCEXTS)")
OBJS = $(foreach x,$(SRCEXTS), $(patsubst ./%$(x),$(OBJDIR)/%.o,$(filter %$(x),$(SOURCES))))
OBJDIRS	= $(sort $(dir $(OBJS)))
DEPS = $(patsubst %.o,%.d,$(OBJS))

.PHONY : all clean debug data install doc

all : $(PROGRAM)

include $(DEPS)

$(OBJDIR)/%.d : %$(SRCEXTS)
	mkdir -p $(OBJDIR)
	$(CXX) -o $@  -MM -MD -MT '$(OBJDIR)/$(patsubst %.cpp,%.o,$<)' $<
$(OBJDIR)/main.o : main.c
	$(CXX) -o $@ -c $(CPPFLAGS) $<
$(OBJDIR)/%.o : %$(SRCEXTS) %.h
	$(CXX) -o $@ -c $(CPPFLAGS) $<
$(PROGRAM) : $(OBJS)
	$(CXX) -o $(PROGRAM) $(OBJS) $(LDFLAGS)

clean :
	$(RM) $(OBJDIR)/*
