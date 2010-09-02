/*
** $Id: gen.h,v 1.7 2010/05/13 19:27:23 rolf Exp $
**
** Rolf Riesen, April 2010, Sandia National Laboratories
**
*/
#ifndef _GEN_H_
#define _GEN_H_

#define FALSE		(0)
#define TRUE		(1)

typedef enum {LOCAL_NVRAM, SSD} nvram_type_t;

void gen_nic(int rank, int router, int port, int aggregator, int aggregator_port,
	int nvram, int nvram_port, int ss, int ss_port);
void gen_router(int id, int num_ports, int wormhole);
void gen_link(int Arouter, int Aport, int Brouter, int Bport);

void reset_router_list(void);
int next_router(int *id, int *wormhole);
void reset_router_nics(int router);
int next_router_nic(int router, int *port);
void reset_router_links(int router);
int next_router_link(int router, int *link_id, int *port);

void reset_nic_list(void);
int next_nic(int *id, int *router, int *port, int *aggregator, int *aggregator_port,
	    int *nvram, int *nvram_port, int *ss, int *ss_port, char **label);

void reset_router_nvram(int router);
void reset_nvram_list(void);
int next_nvram(int *id, int *router, int *port, nvram_type_t *type);
void gen_nvram(int id, int router, int port, nvram_type_t type);
int next_router_nvram(int router, int *port);

#endif /* _GEN_H_ */
