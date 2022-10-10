/* $Id: ls.h,v 1.1 2000/02/23 01:00:30 bobby Exp bobby $
 * Link Set
 */

#ifndef _LS_H_
#define _LS_H_

#include <netinet/in.h>

struct link
{
    struct link *next; // next entry
    struct link *prev; // prev entry
    node peer;         // link peers
    struct sockaddr_in peer_addr;
    int host_port, peer_port;
    int sockfd; // underlying socket for the link. Expected to be bound to link.host_port. Used for both sending and receiving
    cost c;     // cost
    char *name;
};

void close_all_sock();
int ls_getsize();
struct link *getlinkbysock(int sock);
struct link *findlinkbypeer(node p);

int create_link_sock(int port);
int create_ls(); // Initalize module, should be called before any other link state functions, and after set_myid()
int add_link(int host_port, node peer, int peer_port,
             cost c, char *name);
int add_link_if_local(node peer0, int port0, node peer1, int port1, cost c, char *name);
int del_link(char *n);

struct link *find_link(char *n);

void print_link(struct link *i);
void print_ls();
int ud_link(char *n, int cost);

#endif
