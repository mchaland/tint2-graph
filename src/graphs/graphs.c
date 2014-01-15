/**************************************************************************
*
* Tint2 : graphs
*
* Copyright (C) 2011 Francisco J. Vazquez (fjvazquezaraujo@gmail.com)
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License version 2
* as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**************************************************************************/

#include <string.h>
#include <stdio.h>
#include <cairo.h>
#include <cairo-xlib.h>
#include <pango/pangocairo.h>
#include <stdlib.h>
#include <ctype.h>

#include "window.h"
#include "server.h"
#include "panel.h"
#include "graphs.h"
#include "timer.h"
#include "common.h"

#define CPU_SAMPLE_COUNT 5
#define NET_SAMPLE_COUNT 5

int graphs_enabled;
int graphs_ngraphs;
int graphs_ncurves;
int graphs_cpu_pos;
int graphs_mem_pos;
int graphs_net_pos;
int graphs_curves_per_pos[MAXGRAPHS];
int graphs_use_bars;
int graphs_graph_width;
int graphs_cpu_nsamples_avg;
int graphs_net_nsamples_avg;
char *graphs_netiface;
int graphs_netmaxup;
int graphs_netmaxdown;
char *graphs_lclick_command;
char *graphs_rclick_command;

float **graph_values;  // circular array list
static int pos_x = 0;  // current position in graph_values
static timeout* graphs_timeout;


void default_graphs()
{
	graphs_enabled = 0;
	graphs_ngraphs = 0;
	graphs_ncurves = 0;
	graphs_cpu_pos = -1;
	graphs_mem_pos = -1;
	graphs_net_pos = -1;
	graphs_use_bars = 0;
	graphs_graph_width = 50;
	graphs_cpu_nsamples_avg = 1;
	graphs_net_nsamples_avg = 2;
	graphs_netiface = 0;
    graphs_netmaxup = 0;
    graphs_netmaxdown = 0;
	graphs_timeout = 0;
	graphs_lclick_command = 0;
	graphs_rclick_command = 0;
}

void cleanup_graphs()
{
	if (graphs_netiface) g_free(graphs_netiface);
	if (graphs_lclick_command) g_free(graphs_lclick_command);
	if (graphs_rclick_command) g_free(graphs_rclick_command);
	if (graphs_timeout) stop_timeout(graphs_timeout);
	int i;
	for (i = 0; i < graphs_ncurves; i++)
	{
		free(graph_values[i]);
	}
	free(graph_values);
}

int update_cpugraph()
{
	if (graphs_cpu_pos == -1) return 0;

	struct cpu_info {
		unsigned long long last_total;
		unsigned long long last_active_total, last_wait_total;
		int cur_idx;
		double active[CPU_SAMPLE_COUNT], wait[CPU_SAMPLE_COUNT];
		float active_perc, wait_perc;
	};
	static struct cpu_info cpu = { .cur_idx=0 };

	FILE *fp;
	if ((fp = fopen("/proc/stat", "r")) == NULL) {
		return 0;
	}
	char buf[256];
	while (!feof(fp)) {
		if (fgets(buf, 255, fp) == NULL) {
			break;
		}
		if (strncmp(buf, "cpu", 3) == 0) {
			unsigned long long cpu_user;
			unsigned long long cpu_system;
			unsigned long long cpu_nice;
			unsigned long long cpu_idle;
			unsigned long long cpu_iowait;
			unsigned long long cpu_irq;
			unsigned long long cpu_softirq;
			unsigned long long cpu_steal;
			unsigned long long cpu_total;
			unsigned long long cpu_active_total;
			unsigned long long cpu_wait_total;
			sscanf(buf, "%*s %llu %llu %llu %llu %llu %llu %llu %llu",
				   &cpu_user, &cpu_nice, &cpu_system, &cpu_idle,
				   &cpu_iowait, &cpu_irq, &cpu_softirq, &cpu_steal);
			cpu_total = cpu_user + cpu_nice + cpu_system + cpu_idle +
					cpu_iowait + cpu_irq + cpu_softirq + cpu_steal;
			cpu_active_total = cpu_user + cpu_nice + cpu_system + cpu_steal;
			cpu_wait_total = cpu_total - cpu_active_total - cpu_idle;
			double time = (cpu_total - cpu.last_total);
			cpu.active[cpu.cur_idx] = (cpu_active_total - cpu.last_active_total) / time;
			cpu.wait[cpu.cur_idx] = (cpu_wait_total - cpu.last_wait_total) / time;
			cpu.last_total = cpu_total;
			cpu.last_active_total = cpu_active_total;
			cpu.last_wait_total = cpu_wait_total;
			double curtmp1 = 0;
			double curtmp2 = 0;
			int i;
			// Average the samples
			for (i = 0; i < graphs_cpu_nsamples_avg; i++) {
				int idx2 = (cpu.cur_idx + CPU_SAMPLE_COUNT - i) % CPU_SAMPLE_COUNT;
				curtmp1 += cpu.active[idx2];
				curtmp2 += cpu.wait[idx2];
			}
			cpu.active_perc = curtmp1 / (float) graphs_cpu_nsamples_avg;
			cpu.wait_perc = curtmp2 / (float) graphs_cpu_nsamples_avg;
			cpu.cur_idx = (cpu.cur_idx + 1) % CPU_SAMPLE_COUNT;
			break; // Ignore the rest
		}
	}
	fclose(fp);

	graph_values[graphs_cpu_pos][pos_x] = cpu.active_perc;
	graph_values[graphs_cpu_pos+1][pos_x] = cpu.active_perc + cpu.wait_perc;

	return 0;
}

int update_memgraph()
{
	if (graphs_mem_pos == -1) return 0;

	long long unsigned int memtotal = 0, memfree = 0, buffers = 0, cached = 0;

	FILE *fp;
	if ((fp = fopen("/proc/meminfo", "r")) == NULL) {
		return 0;
	}
	char buf[256];
	while (!feof(fp)) {
		if (fgets(buf, 255, fp) == NULL) {
			break;
		}
		if (strncmp(buf, "MemTotal:", 9) == 0) {
			sscanf(buf, "%*s %llu", &memtotal);
		} else if (strncmp(buf, "MemFree:", 8) == 0) {
			sscanf(buf, "%*s %llu", &memfree);
		} else if (strncmp(buf, "Buffers:", 8) == 0) {
			sscanf(buf, "%*s %llu", &buffers);
		} else if (strncmp(buf, "Cached:", 7) == 0) {
			sscanf(buf, "%*s %llu", &cached);
		}
	}
	fclose(fp);

	long long unsigned int used = memtotal - memfree;
	long long unsigned int bufcach = buffers + cached;

	graph_values[graphs_mem_pos][pos_x] = (used - bufcach) / (float) memtotal;
	graph_values[graphs_mem_pos+1][pos_x] = used / (float) memtotal;

	return 0;
}

int update_netgraph()
{
	if (graphs_net_pos == -1 || graphs_netiface == NULL) return 0;

	struct net_stat {
		long long last_down, last_up;
		int cur_idx;
		long long down[NET_SAMPLE_COUNT], up[NET_SAMPLE_COUNT];
		double down_rate, up_rate;
		double max_down, max_up;
	};
	static struct net_stat net = { .cur_idx=0, .max_down=0, .max_up=0 };

	double max(double v1, double v2) { return v1 > v2 ? v1 : v2; }
	double min(double v1, double v2) { return v1 < v2 ? v1 : v2; }

	FILE *fp;
	if (!(fp = fopen("/proc/net/dev", "r"))) {
		return 0;
	}
	char buf[256];
	// Ignore first two lines
	fgets(buf, 255, fp);
	fgets(buf, 255, fp);
	static int first_run = 1;
	while (!feof(fp)) {
		if (fgets(buf, 255, fp) == NULL) {
			break;
		}
		char *p = buf;
		while (isspace((int) *p)) p++;
		char *curdev = p;
		while (*p && *p != ':') p++;
		if (*p == '\0') continue;
		*p = '\0';

		if (strcmp(curdev, graphs_netiface)) continue;

		long long down, up;
		sscanf(p+1, "%lld %*d %*d %*d %*d %*d %*d %*d %lld", &down, &up);
		if (down < net.last_down) net.last_down = 0; // Overflow
		if (up < net.last_up) net.last_up = 0; // Overflow
		net.down[net.cur_idx] = (down - net.last_down);
		net.up[net.cur_idx] = (up - net.last_up);
		net.last_down = down;
		net.last_up = up;
		if (first_run) {
			first_run = 0;
			break;
		}

		unsigned int curtmp1 = 0;
		unsigned int curtmp2 = 0;
		// Average the samples
		int i;
		for (i = 0; i < graphs_net_nsamples_avg; i++) {
			curtmp1 += net.down[(net.cur_idx + NET_SAMPLE_COUNT - i) %
                                NET_SAMPLE_COUNT];
			curtmp2 += net.up[(net.cur_idx + NET_SAMPLE_COUNT - i) %
                              NET_SAMPLE_COUNT];
		}
		net.down_rate = curtmp1 / (double) graphs_net_nsamples_avg;
		net.up_rate = curtmp2 / (double) graphs_net_nsamples_avg;
        if (graphs_netmaxdown > 0)
        {
            net.down_rate /= (float) graphs_netmaxdown;
            net.down_rate = min(1.0, net.down_rate);
        }
        else
            {
            // Normalize by maximum speed (a priori unknown,
            // so we must do this all the time).
            if (net.max_down < net.down_rate)
            {
                for (i = 0; i < graphs_graph_width; i++)
                {
                    graph_values[graphs_net_pos][i] *= (net.max_down /
                                                        net.down_rate);
                }
                net.max_down = net.down_rate;
                net.down_rate = 1.0;
            }
            else if (net.max_down != 0) net.down_rate /= net.max_down;
        }
        if (graphs_netmaxup > 0)
        {
            net.up_rate /= (float) graphs_netmaxup;
            net.up_rate = min(1.0, net.up_rate);
        }
        else
        {
            if (net.max_up < net.up_rate)
            {
                for (i = 0; i < graphs_graph_width; i++)
                {
                    graph_values[graphs_net_pos+1][i] *= (net.max_up /
                                                          net.up_rate);
                }
                net.max_up = net.up_rate;
                net.up_rate = 1.0;
            }
            else if (net.max_up != 0) net.up_rate /= net.max_up;
        }
		net.cur_idx = (net.cur_idx + 1) % NET_SAMPLE_COUNT;
		break; // Ignore the rest
	}
	fclose(fp);

	graph_values[graphs_net_pos][pos_x] = net.down_rate;
	graph_values[graphs_net_pos+1][pos_x] = net.up_rate;

	return 0;
}

void update_graphs(void* arg)
{
	update_cpugraph();
	update_memgraph();
	update_netgraph();
	int i;
	for (i=0 ; i < nb_panel ; i++)
		panel1[i].graphs.area.redraw = 1;
	panel_refresh = 1;
}

void init_graphs()
{
	if (!graphs_enabled || !graphs_ngraphs)
		return;

	graphs_timeout = add_timeout(10, 1000, update_graphs, 0);
	int i;
	graph_values = (float **) malloc(graphs_ncurves * sizeof(float *));
	for (i = 0; i < graphs_ncurves; i++)
	{
		unsigned int gsize = graphs_graph_width * sizeof(float);
		graph_values[i] = malloc(gsize);
		memset(graph_values[i], 0, gsize);
	}
}

void init_graphs_panel(void *p)
{
	if (!graphs_enabled || !graphs_ngraphs)
		return;

	Panel *panel =(Panel*)p;
	Graphs *graphs = &panel->graphs;

	if (graphs->area.bg == 0)
		graphs->area.bg = &g_array_index(backgrounds, Background, 0);
	graphs->area.parent = p;
	graphs->area.panel = p;
	graphs->area._draw_foreground = draw_graphs;
	graphs->area.size_mode = SIZE_BY_CONTENT;
	graphs->area._resize = resize_graphs;

	graphs->area.resize = 1;
	graphs->area.on_screen = 1;
}


void draw_graphs (void *obj, cairo_t *c)
{
	Graphs *graphs = obj;

	cairo_set_line_width (c, 1.0);
	int i, i2, g, cv;
	int x = graphs->area.paddingxlr + graphs->area.bg->border.width + 1;
	int y1 = graphs->area.height - graphs->area.bg->border.width - graphs->area.paddingy;
	for (g = 0, cv = 0; g < graphs_ngraphs; g++)
	{
		for (i2 = 0; i2 < graphs_curves_per_pos[g]; i2++, cv++)
		{
			cairo_set_source_rgba (c, graphs->g[cv].color[0], graphs->g[cv].color[1],
									  graphs->g[cv].color[2], graphs->g[cv].alpha);
			for (i = 0; i < graphs_graph_width; i++)
			{
				int idx = (pos_x + 1 + i + graphs_graph_width) % graphs_graph_width;
				int y2 = y1 - graph_values[cv][idx] * (y1 - graphs->area.bg->border.width -
									graphs->area.paddingy);
				if (graphs_use_bars) cairo_move_to (c, x + i, y1);
				cairo_line_to (c, x + i, y2);
			}
			cairo_stroke (c);
		}
		x += graphs->area.paddingx + graphs_graph_width;
	}
	pos_x = (pos_x + 1) % graphs_graph_width;
}


int resize_graphs (void *obj)
{
	Graphs *graphs = obj;
	int ret = 0;

	graphs->area.redraw = 1;

	if (panel_horizontal) {
		int new_size = (2*graphs->area.paddingxlr) + (2*graphs->area.bg->border.width) +
				   graphs_ngraphs * (graphs_graph_width) +
				   (graphs_ngraphs - 1) * (graphs->area.paddingx);
		if (new_size != graphs->area.width) {
			graphs->area.width = new_size + 1;
			ret = 1;
		}
	}

	return ret;
}


void graphs_action(int button)
{
	char *command = 0;
	switch (button) {
		case 1:
		command = graphs_lclick_command;
		break;
		case 3:
		command = graphs_rclick_command;
		break;
	}
	tint_exec(command);
}

