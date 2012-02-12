/*
	belle-sip - SIP (RFC3261) library.
    Copyright (C) 2010  Belledonne Communications SARL

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "belle_sip_internal.h"

/*
 Listening points: base, udp
*/

struct belle_sip_listening_point{
	belle_sip_object_t base;
	belle_sip_stack_t *stack;
	belle_sip_list_t *channels;
	char *addr;
	int port;
};

static void belle_sip_listening_point_init(belle_sip_listening_point_t *lp, belle_sip_stack_t *s, const char *address, int port){
	lp->port=port;
	lp->addr=belle_sip_strdup(address);
	lp->stack=s;
}

static void belle_sip_listening_point_uninit(belle_sip_listening_point_t *lp){
	belle_sip_list_free_with_data(lp->channels,(void (*)(void*))belle_sip_object_unref);
	belle_sip_free(lp->addr);
}


static void belle_sip_listening_point_add_channel(belle_sip_listening_point_t *lp, belle_sip_channel_t *chan){
	lp->channels=belle_sip_list_append(lp->channels,belle_sip_object_ref(chan));
}

belle_sip_channel_t *belle_sip_listening_point_create_channel(belle_sip_listening_point_t *obj, const char *dest, int port){
	belle_sip_channel_t *chan=BELLE_SIP_OBJECT_VPTR(obj,belle_sip_listening_point_t)->create_channel(obj,dest,port);
	if (chan){
		belle_sip_listening_point_add_channel(obj,chan);
	}
	return chan;
}

#if 0
static void belle_sip_listening_point_remove_channel(belle_sip_listening_point_t *lp, belle_sip_channel_t *chan){
	lp->channels=belle_sip_list_remove(lp->channels,chan);
	belle_sip_object_unref(chan);
}
#endif

BELLE_SIP_DECLARE_NO_IMPLEMENTED_INTERFACES(belle_sip_listening_point_t);
BELLE_SIP_INSTANCIATE_CUSTOM_VPTR(belle_sip_listening_point_t)={
	{ 
		BELLE_SIP_VPTR_INIT(belle_sip_listening_point_t, belle_sip_object_t,FALSE),
		(belle_sip_object_destroy_t)belle_sip_listening_point_uninit,
		NULL,
		NULL
	},
	NULL,
	NULL
};

const char *belle_sip_listening_point_get_ip_address(const belle_sip_listening_point_t *lp){
	return lp->addr;
}

int belle_sip_listening_point_get_port(const belle_sip_listening_point_t *lp){
	return lp->port;
}

const char *belle_sip_listening_point_get_transport(const belle_sip_listening_point_t *lp){
	return BELLE_SIP_OBJECT_VPTR(lp,belle_sip_listening_point_t)->transport;
}


int belle_sip_listening_point_get_well_known_port(const char *transport){
	if (strcasecmp(transport,"UDP")==0 || strcasecmp(transport,"TCP")==0 ) return 5060;
	if (strcasecmp(transport,"DTLS")==0 || strcasecmp(transport,"TLS")==0 ) return 5061;
	belle_sip_error("No well known port for transport %s", transport);
	return -1;
}

static belle_sip_channel_t *_belle_sip_listening_point_get_channel(belle_sip_listening_point_t *lp,const char *peer_name, int peer_port, const struct addrinfo *addr){
	belle_sip_list_t *elem;
	belle_sip_channel_t *chan;
	
	for(elem=lp->channels;elem!=NULL;elem=elem->next){
		chan=(belle_sip_channel_t*)elem->data;
		if (belle_sip_channel_matches(chan,peer_name,peer_port,addr)){
			return chan;
		}
	}
	return NULL;
}

belle_sip_channel_t *belle_sip_listening_point_get_channel(belle_sip_listening_point_t *lp,const char *peer_name, int peer_port){
	struct addrinfo *res=NULL;
	struct addrinfo hints={0};
	char portstr[20];
	belle_sip_channel_t *chan;

	hints.ai_flags=AI_NUMERICHOST|AI_NUMERICSERV;
	snprintf(portstr,sizeof(portstr),"%i",peer_port);
	getaddrinfo(peer_name,portstr,&hints,&res);
	chan=_belle_sip_listening_point_get_channel(lp,peer_name,peer_port,res);
	if (res) freeaddrinfo(res);
	return chan;
}

struct belle_sip_udp_listening_point{
	belle_sip_listening_point_t base;
	int sock;
	belle_sip_source_t *source;
};


static void belle_sip_udp_listening_point_uninit(belle_sip_udp_listening_point_t *lp){
	if (lp->sock!=-1) close(lp->sock);
	if (lp->source) belle_sip_main_loop_remove_source(lp->base.stack->ml,lp->source);
}

static belle_sip_channel_t *udp_create_channel(belle_sip_listening_point_t *lp, const char *dest_ip, int port){
	belle_sip_channel_t *chan=belle_sip_channel_new_udp(lp->stack,((belle_sip_udp_listening_point_t*)lp)->sock,dest_ip,port);
	return chan;
}

BELLE_SIP_DECLARE_NO_IMPLEMENTED_INTERFACES(belle_sip_udp_listening_point_t);
BELLE_SIP_INSTANCIATE_CUSTOM_VPTR(belle_sip_udp_listening_point_t)={
	{
		{ 
			BELLE_SIP_VPTR_INIT(belle_sip_udp_listening_point_t, belle_sip_listening_point_t,FALSE),
			(belle_sip_object_destroy_t)belle_sip_udp_listening_point_uninit,
			NULL,
			NULL
		},
		"UDP",
		udp_create_channel
	}
};


static int create_udp_socket(const char *addr, int port){
	struct addrinfo hints={0};
	struct addrinfo *res=NULL;
	int err;
	int sock;
	char portnum[10];
	
	snprintf(portnum,sizeof(portnum),"%i",port);
	hints.ai_family=AF_UNSPEC;
	hints.ai_socktype=SOCK_DGRAM;
	hints.ai_protocol=IPPROTO_UDP;
	hints.ai_flags=AI_NUMERICSERV;
	err=getaddrinfo(addr,portnum,&hints,&res);
	if (err!=0){
		belle_sip_error("getaddrinfo() failed for %s port %i: %s",addr,port,gai_strerror(err));
		return -1;
	}
	sock=socket(res->ai_family,res->ai_socktype,res->ai_protocol);
	if (sock==-1){
		belle_sip_error("Cannot create UDP socket: %s",strerror(errno));
		freeaddrinfo(res);
		return -1;
	}
	err=bind(sock,res->ai_addr,res->ai_addrlen);
	if (err==-1){
		belle_sip_error("udp bind() failed for %s port %i: %s",addr,port,strerror(errno));
		close(sock);
		freeaddrinfo(res);
		return -1;
	}
	freeaddrinfo(res);
	return sock;
}

/*peek data from the master socket to see where it comes from, and dispatch to matching channel.
 * If the channel does not exist, create it */
static int on_udp_data(belle_sip_udp_listening_point_t *lp, unsigned int events){
	int err;
	unsigned char buf[1];
	struct sockaddr_storage addr;
	socklen_t addrlen=sizeof(addr);

	if (events & BELLE_SIP_EVENT_READ){
		belle_sip_message("udp_listening_point: data to read.");
		err=recvfrom(lp->sock,buf,sizeof(buf),MSG_PEEK,(struct sockaddr*)&addr,&addrlen);
		if (err==-1){
			belle_sip_error("udp_listening_point: recvfrom() failed: %s",strerror(errno));
		}else{
			belle_sip_channel_t *chan;
			struct addrinfo ai={0};
			ai.ai_addr=(struct sockaddr*)&addr;
			ai.ai_addrlen=addrlen;
			chan=_belle_sip_listening_point_get_channel((belle_sip_listening_point_t*)lp,NULL,0,&ai);
			if (chan==NULL){
				chan=belle_sip_channel_new_udp_with_addr(lp->base.stack,lp->sock,&ai);
				if (chan!=NULL){
					belle_sip_message("udp_listening_point: new channel created to %s:%i",chan->peer_name,chan->peer_port);
					belle_sip_listening_point_add_channel((belle_sip_listening_point_t*)lp,chan);
				}
			}
			if (chan){
				/*notify the channel*/
				belle_sip_message("Notifying channel.");
				belle_sip_channel_process_data(chan,events);
			}
		}
	}
	return BELLE_SIP_CONTINUE;
}

belle_sip_listening_point_t * belle_sip_udp_listening_point_new(belle_sip_stack_t *s, const char *ipaddress, int port){
	belle_sip_udp_listening_point_t *lp=belle_sip_object_new(belle_sip_udp_listening_point_t);
	belle_sip_listening_point_init((belle_sip_listening_point_t*)lp,s,ipaddress,port);
	lp->sock=create_udp_socket(ipaddress,port);
	if (lp->sock==-1){
		belle_sip_object_unref(lp);
		return NULL;
	}
	lp->source=belle_sip_fd_source_new((belle_sip_source_func_t)on_udp_data,lp,lp->sock,BELLE_SIP_EVENT_READ,-1);
	belle_sip_main_loop_add_source(s->ml,lp->source);
	return BELLE_SIP_LISTENING_POINT(lp);
}

