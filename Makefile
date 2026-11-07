# Targets:
#   make          build the console version and the FLTK GUI
#   make cli      console version only (no FLTK needed)
#   make gui      FLTK GUI only
CC       ?= cc
CXX      ?= c++
CFLAGS   ?= -std=c11 -O2 -Wall -Wextra -pedantic
CXXFLAGS ?= -std=c++11 -O2 -Wall

FLTK_CXX := $(shell fltk-config --cxxflags 2>/dev/null)
FLTK_LD  := $(shell fltk-config --ldflags 2>/dev/null)

all: cli gui
cli: chess_cli
gui: chess_gui

chess_engine.o: chess_engine.c chess_engine.h
	$(CC) $(CFLAGS) -c chess_engine.c -o $@

chess_cli: chess_cli.c chess_engine.o chess_engine.h
	$(CC) $(CFLAGS) chess_cli.c chess_engine.o -o $@

chess_gui: chess_gui.cpp chess_engine.o chess_engine.h
	@command -v fltk-config >/dev/null || { echo "fltk-config not found: install FLTK dev package (e.g. libfltk1.3-dev)"; exit 1; }
	$(CXX) $(CXXFLAGS) $(FLTK_CXX) chess_gui.cpp chess_engine.o $(FLTK_LD) -pthread -o $@

clean:
	rm -f chess_cli chess_gui chess_engine.o

.PHONY: all cli gui clean
