#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <endian.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>

#include "dv.h"
#include "es.h"
#include "ls.h"
#include "rt.h"
#include "n2h.h"

// global variables
// you may want to take a look at each header files (es.h, ls.h, rt.h)
// to learn about what information and helper functions are already available to you
extern struct el *g_lst;  // 2-D linked list of parsed event config file
extern struct link *g_ls; // current host's link state storage
extern struct rte *g_rt;  // current host's routing table

void numbyte_handler(ssize_t numBytes, unsigned int size, const char* fname1,
						const char* fname2)  {
	if (numBytes < 0) {
		printf("%s failed in %s.\n",fname1, fname2);
		perror(NULL);
		exit(1);
	}
	else if(!numBytes) {
		printf("%s return 0 in %s.\n",fname1, fname2);
		perror(NULL);
		exit(1);
	}
	else if ((unsigned int)numBytes != size) {
		printf("%s returns unexpected return value in %s.\n",fname1, fname2);
		perror(NULL);
		exit(1);
	}
}

/* construct packet based on provided args */
uint16_t* construct_pack(node *n_lst, uint16_t *c_lst, int num){
	uint16_t *p = (uint16_t *)malloc(4 * (num + 1));
	uint8_t *w = (uint8_t *)p;
	w[0] = 7;
	w[1] = 1;
	p[1] = htons((uint16_t)num);
	for(int i = 0; i < num; i++){
		p[2 * (i + 1)] = htons(n_lst[i]);
		p[2 * (i + 1) + 1] = htons(c_lst[i]);
	}
	return p;
}

/* send the packet to all neighbors and destroy the packets */
void send_all_nghb(uint16_t *p, int num) {
	struct link *l;
	unsigned int size = 4 * (num + 1);
	ssize_t numBytes;
	for(l = g_ls->next; l != g_ls; l = l->next) {
		numBytes = sendto(l->sockfd, p, size,0,
			(struct sockaddr *)(&l->peer_addr),sizeof(l->peer_addr));
		numbyte_handler(numBytes,size,"sendto()","send_all_nghb()");
	}
	free(p);
}
void send_all_nghb_except(uint16_t *p, int num, node n) {
	struct link *l;
	unsigned int size = 4 * (num + 1);
	ssize_t numBytes;
	for(l = g_ls->next; l != g_ls; l = l->next) {
		if(l->peer == n){
			continue;
		}
		numBytes = sendto(l->sockfd, p, size,0,
			(struct sockaddr *)(&l->peer_addr),sizeof(l->peer_addr));
		numbyte_handler(numBytes,size,"sendto()","send_all_nghb()");
	}
	free(p);
}

void update_cost_via_n(cost c, node n, cost old){
	struct rte* rt;
	uint16_t clst[255];
	node nlst[255];
	int num = 0;
	for (rt = g_rt->next; rt != g_rt; rt = rt->next){
		// if going straight to neighbor or link through neighbor is tear down
		if((rt->d == n || c == UINT16_MAX) && rt->nh == n){ 
			rt->c = c;
			clst[num] = rt->c;
			nlst[num] = rt->d;
			num ++;
			print_rte(rt);
		}
		else if (rt->nh == n) { // if going through neighbor
			rt->c += c - old;
			clst[num] = rt->c;
			nlst[num] = rt->d;
			num ++;
			print_rte(rt);
		}
	}
	if(num) {
		send_all_nghb(construct_pack(nlst,clst,num),num);
	}
}

// this function is our "entrypoint" to processing "list of [event set]s"
void walk_event_set_list(int pupdate_interval, int evset_interval, int verbose)
{
	struct el *es; // event set, as an element of the global list
	UNUSED(verbose);
	assert(g_lst->next);
	
	for (es = g_lst->next; es != g_lst; es = es->next)
	{
		process_event_set(es);
		dv_process_updates(pupdate_interval, evset_interval);
		print_rt();
	}
	
	dv_process_updates(pupdate_interval, 0);
	
	close_all_sock();
	// printf("Process end\n");
}

// iterate through individual "event" in single [event set]
// and dispatch each event using `dispatch_single_event()`
// you wouldn't need to modify this function, but make sure you understand what it's doing!
void process_event_set(struct el *es)
{
	struct es *ev_set; // event set, as a list of single events
	struct es *ev;	   // single event

	assert(es);

	// get actual event set's head from the element `struct el`
	ev_set = es->es_head;
	assert(ev_set);

	// for each "event"s in [event set]
	for (ev = ev_set->next; ev != ev_set; ev = ev->next)
	{
		dispatch_single_event(ev);
	}
}

// dispatch a event, update data structures, and
// TODO: send link updates to current host's direct neighbors
void dispatch_single_event(struct es *ev)
{
	assert(ev);

	print_event(ev);

	node host = get_myid(), dst,i[1];
	cost c;
	uint16_t j[1];
	struct link *tmp;
	struct rte *rtable;
	switch (ev->ev_ty)
	{ 								// neighboring events
	case _es_link:
		/* add link */
		add_link_if_local(ev->peer0, ev->port0, ev->peer1, ev->port1,
						  ev->cost, ev->name);
		/* update neighbor cost to rt */
		if(host == (node)ev->peer0) {
			dst = ev->peer1;
		}
		else if(host == (node)ev->peer1) {
			dst = ev->peer0;
		}
		else {break;}
		update_rte(dst,ev->cost,dst); // since local link
		i[0] = dst;
		j[0] = ev->cost;
		rtable = find_rte(dst);
		print_rte(rtable);
		send_all_nghb(construct_pack(i, j, 1),1);
		break;
	case _ud_link:
		/* get original link info */
		tmp = find_link(ev->name);
		dst = tmp->peer;
		c = tmp->c;
		/* update link cost */
		ud_link(ev->name, ev->cost);
		// propagate the change of link cost, print rt, and send updates
		update_cost_via_n(ev->cost,dst,c);
		break;
	case _td_link:
		/* deleting link */
		tmp = find_link(ev->name);
		dst = tmp->peer;
		del_link(ev->name);
		i[0] = dst;
		j[0] = UINT16_MAX;
		// propagate the tear down of links, print rt, and send updates
		update_cost_via_n(UINT16_MAX,dst,0);
		send_all_nghb(construct_pack(i, j, 1),1);
		break;
	default:
		printf("[es]\t\tUnknown event!\n");
		break;
	}
}

// this function should execute for `evset_interval` seconds
// it will recv updates from neighbors, update the routing table, and send updates back
// it should also handle sending periodic updates to neighbors
// TODO: implement this function, pseudocode has been provided below for your reference
void dv_process_updates(int pupdate_interval, int evset_interval)
{
	struct timespec anchor, now;
	struct link *teller, *neighbor;
	clock_gettime(CLOCK_REALTIME,&anchor);
	int size = ls_getsize(), i = 0, rc;
	if(!size) {
		return;
	}
	/* get fsocket descriptors ready */
	struct pollfd *fds = (struct pollfd *)malloc(size * sizeof(struct pollfd));
	memset(fds,0,size * sizeof(struct pollfd));
	for(teller = g_ls->next; teller != g_ls; teller = teller->next) {
		fds[i].fd = teller->sockfd;
		fds[i].events = POLLIN;
		i++;
	}
	clock_gettime(CLOCK_REALTIME,&now);
	int timeout = 0;
	int remain, interupt = 0;
	ssize_t numBytes;
	uint16_t *p, peek[2];
	struct timespec poll_now, poll_anchor; // used to time if poll return early
	while(now.tv_sec - anchor.tv_sec < evset_interval || !evset_interval) {
		/* determine wait time */
		if (interupt && timeout){
			timeout = timeout - (poll_now.tv_sec - poll_anchor.tv_sec);
		}
		else {
			timeout = pupdate_interval;
		}
		clock_gettime(CLOCK_REALTIME,&poll_anchor);
		rc = poll(fds,size,timeout*1000); // wait for incoming traffic
		clock_gettime(CLOCK_REALTIME,&poll_now);
		interupt = 1;
		if(rc < 0){
			perror("poll() failed.\n");
			exit(1);
		}
		else if(!rc){
			interupt = 0;
			send_periodic_updates();
			clock_gettime(CLOCK_REALTIME,&now);
			continue;
		}
		/* receive the updates */
		int changed = 0;
		for(i = 0; i < size; i++) {
			if(fds[i].revents != POLLIN) {continue;}
			numBytes = recvfrom(fds[i].fd,peek,4,MSG_PEEK,NULL,NULL); // decide the size pf packet
			numbyte_handler(numBytes, 4, "recvfrom()","dv_process_updates()");
			uint16_t num = htons(peek[1]);
			p = (uint16_t *)malloc((num + 1) * 4);
			numBytes = recvfrom(fds[i].fd,p,(num + 1)*4,0,NULL,NULL); // read in packet
			numbyte_handler(numBytes, (num + 1)*4, "recvfrom()","dv_process_updates()");
			teller = getlinkbysock(fds[i].fd);
			/**************************************************/
			for(int j = 0; j < num; j++) {
				uint16_t dst_update = htons(p[2 * (j + 1)]), cost_update = htons(p[2 * (j + 1) + 1]);
				// printf("Node %d get Destination %d \t Cost %d from \t Node %d\n",
				// 	get_myid(),dst_update,cost_update,teller->peer);
				struct rte *rtable = find_rte(dst_update);
				neighbor = findlinkbypeer(dst_update);
				uint16_t c[1];
				node dst[1];
				if(dst_update == get_myid()) // the destinaton is myself
					continue;
				/* tera down packet */
				else if ((cost_update) == UINT16_MAX && rtable->nh == teller->peer) {
					if(neighbor) {
						update_rte(dst_update,neighbor->c,dst_update);
						c[0] = neighbor->c;
					}
					else {
						update_rte(dst_update,UINT16_MAX,teller->peer);
						c[0] = UINT16_MAX;
					}
					changed += 1;
				}
				/* cost update */
				/* sending neighbor is a better hop or neighbor cost to destination is changed */
				else if (rtable->c > (cost_update + teller->c) ||
				 (rtable->nh == teller->peer && (rtable->c != cost_update + teller->c))) {
					rtable->c = cost_update + teller->c;
					rtable->nh = teller->peer;
					c[0] = rtable->c;
					changed += 1;
				}
				/* if destination is a neighbor check cost against neighbor cost */
				if(neighbor && neighbor->c < rtable->c){
					rtable->c = neighbor->c;
					rtable->nh = neighbor->peer;
					c[0] = rtable->c;
					changed += 1;
				}
				if(changed){
					dst[0] = dst_update;
					rtable = find_rte(dst_update);
					print_rte(rtable);
					send_all_nghb_except(construct_pack(dst,c,1),1,teller->peer);
					changed = 0;
				}
			}
			/**************************************************/
			free(p);
		}
		clock_gettime(CLOCK_REALTIME,&now);
		remain  = evset_interval - now.tv_sec + anchor.tv_sec;
		if(remain < pupdate_interval) {
			sleep(remain); // next event set exec is earlier than next periodic update
			free(fds);
			return;
		}
		clock_gettime(CLOCK_REALTIME,&now); // update current time
	}
	free(fds);
}

// read current host's routing table, and send updates to all neighbors
// TODO: implement this function
// HINT: if you implemented a helper that handles sending to neighbors in `dispatch_single_event()`,
// you can reuse that here!
void send_periodic_updates() {
	struct rte *rtable;
	int size = 0, i = 0;
	for(rtable = g_rt->next; rtable != g_rt; rtable = rtable->next){
		size++;
	}
	node *n_lst = (node *)malloc(size * sizeof(node));
	uint16_t *c_lst = (uint16_t *)malloc(size * sizeof(uint16_t));
	for(rtable = g_rt->next; rtable != g_rt; rtable = rtable->next){
		n_lst[i] = rtable->d;
		c_lst[i] = rtable->c;
		i++;
	}
	send_all_nghb(construct_pack(n_lst,c_lst,size),size);
	free(n_lst);
	free(c_lst);
}