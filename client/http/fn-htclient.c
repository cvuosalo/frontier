/*
 * FroNTier client API
 * 
 * Author: Sergey Kosyakov
 *
 * $Header$
 *
 * $Id$
 *
 */
 
#include <fn-htclient.h>

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>


#define MAX_NAME_LEN	128
#define URL_FMT_SRV	"http://%127[^/]/%127s"
#define URL_FMT_PROXY	"http://%s"

extern void *(*frontier_mem_alloc)(size_t size);
extern void (*frontier_mem_free)(void *p);
 
FrontierHttpClnt *frontierHttpClnt_create(int *ec)
 {
  FrontierHttpClnt *c;
  
  c=frontier_mem_alloc(sizeof(FrontierHttpClnt));
  if(!c)
   {
    *ec=FRONTIER_EMEM;
    FRONTIER_MSG(*ec);
    return c;
   }
  bzero(c,sizeof(FrontierHttpClnt));
  c->socket=-1;
  
  *ec=FRONTIER_OK;
  return c;
 }
 
 
int frontierHttpClnt_addServer(FrontierHttpClnt *c,const char *url)
 {
  FrontierUrlInfo *fui;
  int ec=FRONTIER_OK;
  
  if(c->total_server+1>=FRONTIER_SRV_MAX_NUM) 
   {
    frontier_setErrorMsg(__FILE__,__LINE__,"config error: server list is too long");
    return FRONTIER_ECFG;
   }
  
  fui=frontier_CreateUrlInfo(url,&ec);
  if(!fui) return ec;
  
  if(!fui->path)
   {
    frontier_setErrorMsg(__FILE__,__LINE__,"config error: server %s: path is missing",fui->host);
    frontier_DeleteUrlInfo(fui);    
    return FRONTIER_ECFG;
   }
   
  c->server[c->total_server]=fui;
  c->total_server++;
  
  return FRONTIER_OK;
 }

 
int frontierHttpClnt_addProxy(FrontierHttpClnt *c,const char *url)
 {
  FrontierUrlInfo *fui;
  int ec=FRONTIER_OK;
  
  if(c->total_proxy+1>=FRONTIER_SRV_MAX_NUM)
   {
    frontier_setErrorMsg(__FILE__,__LINE__,"config error: server list is too long");
    return FRONTIER_ECFG;
   }
    
  fui=frontier_CreateUrlInfo(url,&ec);
  if(!fui) return ec;
  
  c->proxy[c->total_proxy]=fui;
  c->total_proxy++;
  
  return FRONTIER_OK;
 }
 
 
 
void frontierHttpClnt_setCacheRefreshFlag(FrontierHttpClnt *c,int is_refresh)
 {
  c->is_refresh=is_refresh;
 }
 
 
 
static int http_read(FrontierHttpClnt *c)
 {
  //printf("\nhttp_read\n");
  //bzero(c->buf,FRONTIER_HTTP_BUF_SIZE);
  c->data_pos=0;  
  c->data_size=frontier_read(c->socket,c->buf,FRONTIER_HTTP_BUF_SIZE);
  return c->data_size;
 }

 
static int read_char(FrontierHttpClnt *c,char *ch)
 {
  int ret;
  
  if(c->data_pos+1>c->data_size)
   {
    ret=http_read(c);
    if(ret<=0) return ret;
   }
  *ch=c->buf[c->data_pos];
  c->data_pos++;
  return 1;
 }

 
static int test_char(FrontierHttpClnt *c,char *ch)
 {
  int ret;
  
  if(c->data_pos+1>c->data_size)
   {
    ret=http_read(c);
    if(ret<=0) return ret;
   }
  *ch=c->buf[c->data_pos];
  return 1;
 }
 
 
static int read_line(FrontierHttpClnt *c,char *buf,int buf_len)
 {
  int i;
  int ret;
  char ch,next_ch;
  
  i=0;
  while(1)
   {
    ret=read_char(c,&ch);
    if(ret<0) return ret;
    if(!ret) break;
    
    if(ch=='\r' || ch=='\n')
     {
      ret=test_char(c,&next_ch);
      if(ret<0) return ret;
      if(!ret) break;      
      if(ch=='\r' && next_ch=='\n') read_char(c,&ch);	// Just ignore
      break;
     }
    buf[i]=ch;
    i++;
    if(i>=buf_len-1) break;
   }
  buf[i]=0;
  return i;
 }
   
  
#define FN_REQ_BUF 8192
 
int frontierHttpClnt_open(FrontierHttpClnt *c,const char *url)
 {
  int ret;
  int len;
  struct sockaddr_in *sin;
  struct addrinfo *addr;
  char buf[FN_REQ_BUF];
  FrontierUrlInfo *fui_proxy;
  FrontierUrlInfo *fui_server;
  int is_proxy;
  
  fui_proxy=c->proxy[c->cur_proxy];
  fui_server=c->server[c->cur_server];
  
  if(!fui_server)
   {
    frontier_setErrorMsg(__FILE__,__LINE__,"no available server");
    return FRONTIER_ECFG;
   }   
  
  if(fui_proxy)
   {
    frontier_log(FRONTIER_LOGLEVEL_DEBUG,__FILE__,__LINE__,"using proxy %s",fui_proxy->host);
    if(!fui_proxy->addr)
     {
      ret=frontier_resolv_host(fui_proxy);
      if(ret) return ret;
     }
    addr=fui_proxy->addr;
    is_proxy=1;
   }
  else
   {
    if(!fui_server->addr)
     {
      ret=frontier_resolv_host(fui_server);
      if(ret) return ret;
     }
    addr=fui_server->addr;
    is_proxy=0;
   }
     
  do
   {
    c->socket=frontier_socket();
    if(c->socket<0) return c->socket;
    
    sin=(struct sockaddr_in*)(addr->ai_addr);
    if(is_proxy) 
     {
      sin->sin_port=htons((unsigned short)(fui_proxy->port));
     }
    else
     {
      sin->sin_port=htons((unsigned short)(fui_server->port));
     }
  
    ret=frontier_connect(c->socket,addr->ai_addr,addr->ai_addrlen);
    if(ret==0) break;
    
    close(c->socket);        
    addr=addr->ai_next;
   }while(addr);
  
  if(ret) return ret;

  bzero(buf,FN_REQ_BUF);
  
  if(is_proxy)
   {
    len=snprintf(buf,FN_REQ_BUF,"GET %s/%s HTTP/1.0\r\nHost: %s\r\n",fui_server->url,url,fui_server->host);
   }
  else
   {
    len=snprintf(buf,FN_REQ_BUF,"GET /%s/%s HTTP/1.0\r\nHost: %s\r\n",fui_server->path,url,fui_server->host);
   }
  if(len>=FN_REQ_BUF)
   {
    frontier_setErrorMsg(__FILE__,__LINE__,"request is bigger than %d bytes",FN_REQ_BUF);
    return FRONTIER_EIARG;
   }
   
  if(c->is_refresh)
   {
    ret=snprintf(buf+len,FN_REQ_BUF-len,"Pragma: no-cache\r\n\r\n");
   }
  else
   {
    ret=snprintf(buf+len,FN_REQ_BUF-len,"\r\n");
   }
  if(ret>=FN_REQ_BUF-len)
   {
    frontier_setErrorMsg(__FILE__,__LINE__,"request is bigger than %d bytes",FN_REQ_BUF);
    return FRONTIER_EIARG;
   }
  len+=ret;
   
  frontier_log(FRONTIER_LOGLEVEL_DEBUG,__FILE__,__LINE__,"request <%s>",buf);
   
  ret=frontier_write(c->socket,buf,len);
  if(ret<0) return ret;

  // Read status line
  ret=read_line(c,buf,FN_REQ_BUF);
  if(ret<0) return ret;
  if(ret==0)
   {
    frontier_setErrorMsg(__FILE__,__LINE__,"empty response from server");
    return FRONTIER_ENETWORK;    
   }
  frontier_log(FRONTIER_LOGLEVEL_DEBUG,__FILE__,__LINE__,"status line <%s>",buf);
  if(strncmp(buf,"HTTP/1.0 200 ",13) && strncmp(buf,"HTTP/1.1 200 ",13))
   {
    frontier_setErrorMsg(__FILE__,__LINE__,"bad server response (%s)",buf);
    return FRONTIER_ENETWORK;        
   }
      
  do
   {   
    ret=read_line(c,buf,FN_REQ_BUF);
    if(ret<0) return ret;
    frontier_log(FRONTIER_LOGLEVEL_DEBUG,__FILE__,__LINE__,"got line <%s>",buf);
   }while(*buf);
            
  return FRONTIER_OK;
 }
 
 
 
int frontierHttpClnt_read(FrontierHttpClnt *c,char *buf,int buf_len)
 {
  int ret;
  int available;
  
  if(c->data_pos+1>c->data_size)
   {
    ret=http_read(c);
    if(ret<=0) return ret;
   }
   
  available=c->data_size-c->data_pos;
  available=buf_len>available?available:buf_len;
  bcopy(c->buf+c->data_pos,buf,available);
  c->data_pos+=available;
    
  return available;
 }
 
 
void frontierHttpClnt_close(FrontierHttpClnt *c)
 {
  frontier_socket_close(c->socket);
  c->socket=-1;
 }
 
 
void frontierHttpClnt_delete(FrontierHttpClnt *c)
 {
  int i;
  
  if(!c) return;
  
  for(i=0;i<FRONTIER_SRV_MAX_NUM; i++) frontier_DeleteUrlInfo(c->server[i]);
  for(i=0;i<FRONTIER_SRV_MAX_NUM; i++) frontier_DeleteUrlInfo(c->proxy[i]);
  
  if(c->socket>=0) frontierHttpClnt_close(c);
  
  frontier_mem_free(c);
 }
