/********************************************************************** 
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_SYS_SIGNAL_H
#include <sys/signal.h>
#endif
#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#include <gtk/gtk.h>

#include "capstr.h"
#include "log.h"
#include "game.h"
#include "packets.h"
#include "version.h"

#include "chatline.h"
#include "civclient.h"
#include "dialogs.h"           /* popdown_races_dialog() */
#include "gui_main.h"
#include "packhand.h"

#include "clinet.h"

struct connection aconnection;

extern gint gdk_input_id;

extern int errno;		/* See discussion in server/sernet.c  --dwp */


void get_net_input(gpointer data, gint fid, GdkInputCondition condition);


/**************************************************************************
...
**************************************************************************/
int connect_to_server(char *name, char *hostname, int port, char *errbuf)
{
  /* use name to find TCPIP address of server */
  struct sockaddr_in src;
  struct hostent *ph;
  long address;
  struct packet_req_join_game req;

  if(port==0)
    port=DEFAULT_SOCK_PORT;
  
  if(!hostname)
    hostname="localhost";
  
  if(isdigit((size_t)*hostname)) {
    if((address = inet_addr(hostname)) == -1) {
      strcpy(errbuf, "Invalid hostname");
      return -1;
    }
    src.sin_addr.s_addr = address;
    src.sin_family = AF_INET;
  }
  else if ((ph = gethostbyname(hostname)) == NULL) {
    strcpy(errbuf, "Failed looking up host");
    return -1;
  }
  else {
    src.sin_family = ph->h_addrtype;
    memcpy((char *) &src.sin_addr, ph->h_addr, ph->h_length);
  }
  
  src.sin_port = htons(port);
  
  /* ignore broken pipes */
  signal (SIGPIPE, SIG_IGN);
  
  if((aconnection.sock = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
    strcpy(errbuf, mystrerror(errno));
    return -1;
  }
  
  if(connect(aconnection.sock, (struct sockaddr *) &src, sizeof (src)) < 0) {
    strcpy(errbuf, mystrerror(errno));
    close(aconnection.sock);
    return -1;
  }

  aconnection.buffer.ndata=0;

  gdk_input_id = gdk_input_add(aconnection.sock,
	GDK_INPUT_READ|GDK_INPUT_EXCEPTION, get_net_input, NULL);
  
  /* now send join_request package */

  strcpy(req.name, name);
  req.major_version=MAJOR_VERSION;
  req.minor_version=MINOR_VERSION;
  req.patch_version=PATCH_VERSION;
  strcpy(req.capability, our_capability);

  send_packet_req_join_game(&aconnection, &req);

  return 0;
}

void disconnect_from_server(void)
{
  append_output_window("Disconnecting from server.");
  close(aconnection.sock);
  remove_net_input();
  set_client_state(CLIENT_PRE_GAME_STATE);
}

/**************************************************************************/
void get_net_input(gpointer data, gint fid, GdkInputCondition condition)
{
  if (read_socket_data(fid, &aconnection.buffer)>0) {  
    int type;
    char *packet;
    
    while((packet=get_packet_from_connection(&aconnection, &type))) {
      handle_packet_input(packet, type);
    }
  }
  else {
    append_output_window("Lost connection to server!");
    freelog(LOG_NORMAL, "lost connection to server");
    close(fid);
    remove_net_input();
    popdown_races_dialog(); 
    set_client_state(CLIENT_PRE_GAME_STATE);
  }
}

/**************************************************************************
...
**************************************************************************/
void close_server_connection(void)
{
  close(aconnection.sock);
}


/**************************************************************************
  Get the list of servers from the metaserver
**************************************************************************/
int get_meta_list(GtkWidget *list, char *errbuf)
{
  struct sockaddr_in addr;
  struct hostent *ph;
  int s, i;
  FILE *f;
  char *row[6];
  char  buf[6][64];
  char *proxy_url = (char *)NULL;
  char urlbuf[512];
  char *urlpath;
  char *server;
  int port;
  char str[512];

  if ((proxy_url = getenv("http_proxy"))) {
    if (strncmp(proxy_url,"http://",strlen("http://"))) {
      strcpy(errbuf, "Invalid $http_proxy value, must start with 'http://'");
      return -1;
    }
    strncpy(urlbuf,proxy_url,511);
  } else {
    if (strncmp(METALIST_ADDR,"http://",strlen("http://"))) {
      strcpy(errbuf, "Invalid metaserver URL, must start with 'http://'");
      return -1;
    }
    strncpy(urlbuf,METALIST_ADDR,511);
  }
  server = &urlbuf[strlen("http://")];

  {
    char *s;
    if ((s = strchr(server,':'))) {
      port = atoi(&s[1]);
      if (!port) {
        port = 80;
      }
      s[0] = '\0';
      ++s;
      while (isdigit(s[0])) {++s;}
    } else {
      port = 80;
      if (!(s = strchr(server,'/'))) {
        s = &server[strlen(server)];
      }
    }  /* s now points past the host[:port] part */

    if (s[0] == '/') {
      s[0] = '\0';
      ++s;
    } else if (s[0]) {
      strcpy(errbuf, "Invalid $http_proxy value, cannot find separating '/'");
      /* which is obligatory if more characters follow */
      return -1;
    }
    urlpath = s;
  }

  if ((ph = gethostbyname(server)) == NULL) {
    strcpy(errbuf, "Failed looking up host");
    return -1;
  } else {
    addr.sin_family = ph->h_addrtype;
    memcpy((char *) &addr.sin_addr, ph->h_addr, ph->h_length);
  }
  
  addr.sin_port = htons(port);
  
  if((s = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
    strcpy(errbuf, mystrerror(errno));
    return -1;
  }
  
  if(connect(s, (struct sockaddr *) &addr, sizeof (addr)) < 0) {
    strcpy(errbuf, mystrerror(errno));
    close(s);
    return -1;
  }

  f=fdopen(s,"r+");
  fprintf(f,"GET %s%s%s HTTP/1.0\r\n\r\n",
    proxy_url ? "" : "/",
    urlpath,
    proxy_url ? METALIST_ADDR : "");
  fflush(f);

#define NEXT_FIELD p=strstr(p,"<TD>"); if(p==NULL) continue; p+=4;
#define END_FIELD  p=strstr(p,"</TD>"); if(p==NULL) continue; *p++='\0';
#define GET_FIELD(x) NEXT_FIELD (x)=p; END_FIELD

  gtk_clist_freeze(GTK_CLIST(list));
  gtk_clist_clear(GTK_CLIST(list));

  for (i=0; i<6; i++)
    row[i]=buf[i];

  while(fgets(str,512,f)!=NULL)  {
    if(!strncmp(str,"<TR BGCOLOR",11))  {
      char *name,*port,*version,*status,*players,*metastring;
      char *p;

      p=strstr(str,"<a"); if(p==NULL) continue;
      p=strchr(p,'>');    if(p==NULL) continue;
      name=++p;
      p=strstr(p,"</a>"); if(p==NULL) continue;
      *p++='\0';

      GET_FIELD(port);
      GET_FIELD(version);
      GET_FIELD(status);
      GET_FIELD(players);
      GET_FIELD(metastring);

      sprintf(row[0], "%.63s", name);
      sprintf(row[1], "%.63s", port);
      sprintf(row[2], "%.63s", version);
      sprintf(row[3], "%.63s", status);
      sprintf(row[4], "%.63s", players);
      sprintf(row[5], "%.63s", metastring);

      gtk_clist_append(GTK_CLIST(list), row);
    }
  }
  gtk_clist_thaw(GTK_CLIST(list));
  fclose(f);

  return 0;
}
