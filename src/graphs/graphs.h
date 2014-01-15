/**************************************************************************
* Copyright (C) 2011 Francisco J. Vazquez (fjvazquezaraujo@gmail.com)
*
**************************************************************************/

#ifndef GRAPHS_H
#define GRAPHS_H

#include <sys/time.h>
#include "common.h"
#include "area.h"

#define MAXGRAPHS 3
#define MAXCURVES 6

typedef struct Graphs {
	// always start with area
	Area area;

	char *netdev;
	Color g[MAXCURVES];
} Graphs;

extern int graphs_enabled;
extern int graphs_ngraphs;
extern int graphs_ncurves;
extern int graphs_cpu_pos;
extern int graphs_mem_pos;
extern int graphs_net_pos;
extern int graphs_curves_per_pos[MAXGRAPHS];
extern int graphs_use_bars;
extern int graphs_graph_width;
extern char *graphs_netiface;
extern int graphs_netmaxup;
extern int graphs_netmaxdown;
extern char *graphs_lclick_command;
extern char *graphs_rclick_command;

void default_graphs();

void cleanup_graphs();

void init_graphs();
void init_graphs_panel(void *panel);

void draw_graphs (void *obj, cairo_t *c);

int resize_graphs (void *obj);

void graphs_action(int button);

#endif
