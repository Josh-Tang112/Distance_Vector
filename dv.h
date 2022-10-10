#ifndef _DV_H_
#define _DV_H_

#include "es.h"

void walk_event_set_list(int pupdate_interval, int evset_interval, int verbose);

void process_event_set(struct el *es);

void dv_process_updates(int pupdate_interval, int evset_intervals);

void dispatch_single_event(struct es *ev);

void send_periodic_updates();

#endif