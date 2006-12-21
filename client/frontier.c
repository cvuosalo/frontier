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

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <frontier_client/frontier.h>
#include "fn-internal.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <fcntl.h>


int frontier_log_level;
char *frontier_log_file;
void *(*frontier_mem_alloc)(size_t size);
void (*frontier_mem_free)(void *ptr);
static int initialized=0;

static char frontier_id[FRONTIER_ID_SIZE];
static char frontier_api_version[]=FNAPI_VERSION;

static chan_seqnum = 0;
static void channel_delete(Channel *chn);

char *frontier_str_ncopy(const char *str, size_t len)
 {
  char *ret;
  
  ret=frontier_mem_alloc(len+1);
  if(!ret) return ret;
  bcopy(str,ret,len);
  ret[len]=0;

  return ret;
 }


char *frontier_str_copy(const char *str)
 {
  int len=strlen(str);
  char *ret;
  
  ret=frontier_mem_alloc(len+1);
  if(!ret) return ret;
  bcopy(str,ret,len);
  ret[len]=0;

  return ret;
 }
 

int frontier_init(void *(*f_mem_alloc)(size_t size),void (*f_mem_free)(void *ptr))
{
  return(frontier_initdebug(f_mem_alloc,f_mem_free,
		getenv(FRONTIER_ENV_LOG_FILE),getenv(FRONTIER_ENV_LOG_LEVEL)));
}

int frontier_initdebug(void *(*f_mem_alloc)(size_t size),void (*f_mem_free)(void *ptr),
			const char *logfilename, const char *loglevel)
 {
  uid_t uid;
  struct passwd *pwent;
  pid_t pid;
  
  if(initialized) return FRONTIER_OK;

  if(!f_mem_alloc) {f_mem_alloc=malloc; f_mem_free=free;}
  if(!f_mem_free) {f_mem_alloc=malloc; f_mem_free=free;}

  frontier_mem_alloc=f_mem_alloc;
  frontier_mem_free=f_mem_free;
    
  if(!loglevel) 
   {
    frontier_log_level=FRONTIER_LOGLEVEL_NOLOG;
    frontier_log_file=(char*)0;
   }
  else
   {
    if(strcasecmp(loglevel,"warning")==0 || strcasecmp(loglevel,"info")==0) frontier_log_level=FRONTIER_LOGLEVEL_WARNING;
    else if(strcasecmp(loglevel,"error")==0) frontier_log_level=FRONTIER_LOGLEVEL_ERROR;
    else if(strcasecmp(loglevel,"nolog")==0) frontier_log_level=FRONTIER_LOGLEVEL_NOLOG;
    else frontier_log_level=FRONTIER_LOGLEVEL_DEBUG;
    
    if(!logfilename)
     {
      frontier_log_file=(char*)0;
     }
    else
     {
      int fd=open(logfilename,O_CREAT|O_APPEND|O_WRONLY,0644);
      if(fd<0) 
       {
        printf("Can not open log file %s. Log is disabled.\n",logfilename);
	frontier_log_level=FRONTIER_LOGLEVEL_NOLOG;
        frontier_log_file=(char*)0;
       }
      else
       {
        close(fd);
        frontier_log_file=frontier_str_copy(logfilename);
       }
     }
   }

  uid=getuid();
  pwent=getpwuid(uid);
  pid=getpid();

  snprintf(frontier_id,FRONTIER_ID_SIZE,"%s %d %s(%d) %s",frontier_api_version,pid,pwent->pw_name,uid,pwent->pw_gecos);
  
  initialized=1;
  
  return FRONTIER_OK;
 }



static Channel *channel_create(const char *srv,const char *proxy,int *ec)
 {
  Channel *chn;
  int ret;
  const char *p;

  chn=frontier_mem_alloc(sizeof(Channel));
  if(!chn) 
   {
    *ec=FRONTIER_EMEM;
    FRONTIER_MSG(*ec);
    return (void*)0;
   }
  bzero(chn,sizeof(Channel));

  chn->seqnum = ++chan_seqnum;
  chn->cfg=frontierConfig_get(srv,proxy,ec);
  if(!chn->cfg)
   {
    channel_delete(chn);    
    return (void*)0;
   }
  if(!chn->cfg->server_num)
   {
    *ec=FRONTIER_ECFG;
    frontier_setErrorMsg(__FILE__,__LINE__,"no servers configured");
    channel_delete(chn);    
    return (void*)0;
   }
  
  chn->ht_clnt=frontierHttpClnt_create(ec);
  if(!chn->ht_clnt || *ec)
   {
    channel_delete(chn);    
    return (void*)0;
   }
   
  do
   {
    p=frontierConfig_getServerUrl(chn->cfg);
    if(!p) break;
    ret=frontierHttpClnt_addServer(chn->ht_clnt,p);
    if(ret)
     {
      *ec=ret;
      channel_delete(chn);    
      return (void*)0;
     }
   }while(frontierConfig_nextServer(chn->cfg)==0);
   
  if(!chn->ht_clnt->total_server)
   {
    *ec=FRONTIER_ECFG;
    frontier_setErrorMsg(__FILE__,__LINE__,"no server configured");
    channel_delete(chn);    
    return (void*)0;
   }

  do
   {
    p=frontierConfig_getProxyUrl(chn->cfg);
    if(!p) break;
    ret=frontierHttpClnt_addProxy(chn->ht_clnt,p);
    if(ret)
     {
      *ec=ret;
      channel_delete(chn);    
      return (void*)0;
     }
   }while(frontierConfig_nextProxy(chn->cfg)==0);
        
  chn->reload=0;
  *ec=FRONTIER_OK; 
  return chn;
 }

static Channel *channel_create2(FrontierConfig * config, int *ec)
 {
  Channel *chn;
  int ret;
  const char *p;

  chn = frontier_mem_alloc(sizeof(Channel));
  if(!chn) {
    *ec = FRONTIER_EMEM;
    FRONTIER_MSG(*ec);
    return (void*)0;
  }
  bzero(chn, sizeof(Channel));

  chn->seqnum = ++chan_seqnum;
  chn->cfg = config;
  if(!chn->cfg) {
    *ec = FRONTIER_EMEM;
    FRONTIER_MSG(*ec);
    channel_delete(chn);    
    return (void*)0;
  }
  if(!chn->cfg->server_num) {
    *ec = FRONTIER_ECFG;
    frontier_setErrorMsg(__FILE__, __LINE__, "no servers configured");
    channel_delete(chn);    
    return (void*)0;
  }
  
  chn->ht_clnt = frontierHttpClnt_create(ec);
  if(!chn->ht_clnt || *ec) {
    channel_delete(chn);    
    return (void*)0;
  }
   
  do {
    p = frontierConfig_getServerUrl(chn->cfg);
    if(!p) {
      break;
    }
    ret = frontierHttpClnt_addServer(chn->ht_clnt,p);
    if(ret) {
      *ec = ret;
      channel_delete(chn);    
      return (void*)0;
    }
  } while(frontierConfig_nextServer(chn->cfg)==0);
   
  if(!chn->ht_clnt->total_server) {
    *ec = FRONTIER_ECFG;
    frontier_setErrorMsg(__FILE__, __LINE__, "no server configured");
    channel_delete(chn);    
    return (void*)0;
  }

  do {
    p = frontierConfig_getProxyUrl(chn->cfg);
    if(!p) {
      break;
    }
    ret = frontierHttpClnt_addProxy(chn->ht_clnt,p);
    if(ret) {
      *ec = ret;
      channel_delete(chn);    
      return (void*)0;
    }
  } while(frontierConfig_nextProxy(chn->cfg)==0);
        
  chn->reload = 0;
  *ec = FRONTIER_OK; 
  return chn;
}

static void channel_delete(Channel *chn)
 {
  if(!chn) return;
  frontierHttpClnt_delete(chn->ht_clnt);
  if(chn->resp)frontierResponse_delete(chn->resp);
  frontierConfig_delete(chn->cfg);
  frontier_mem_free(chn);
 }


FrontierChannel frontier_createChannel(const char *srv,const char *proxy,int *ec)
 {
  Channel *chn=channel_create(srv,proxy,ec);
  return (unsigned long)chn;
 }

FrontierChannel frontier_createChannel2(FrontierConfig* config, int *ec) {
  Channel *chn = channel_create2(config, ec);
  return (unsigned long)chn;
}


void frontier_closeChannel(FrontierChannel fchn)
 {
  channel_delete((Channel*)fchn);
 }

 
void frontier_setReload(FrontierChannel u_channel,int reload)
 {
  Channel *chn=(Channel*)u_channel;  
  chn->user_reload=reload;
 }
 

static int write_data(FrontierResponse *resp,void *buf,int len)
 {
  int ret;
  ret=FrontierResponse_append(resp,buf,len);
  return ret;
 }


static int prepare_channel(Channel *chn)
 {
  int ec;
  
  if(chn->resp) frontierResponse_delete(chn->resp);
  chn->resp=frontierResponse_create(&ec);  
  chn->resp->seqnum = ++chn->response_seqnum;
  if(!chn->resp) return ec;
  
  return FRONTIER_OK;
 }
 
 
static int get_data(Channel *chn,const char *uri,const char *body)
 {
  int ret=FRONTIER_OK;
  char buf[8192];

  if(!chn) 
   {
    frontier_setErrorMsg(__FILE__,__LINE__,"wrong channel");
    return FRONTIER_EIARG;
   }

  ret=prepare_channel(chn);
  if(ret) return ret;
  
  frontierHttpClnt_setCacheRefreshFlag(chn->ht_clnt,chn->reload);
  frontierHttpClnt_setFrontierId(chn->ht_clnt,frontier_id);
  
  ret=frontierHttpClnt_open(chn->ht_clnt);
  if(ret) goto end;

  if(body) ret=frontierHttpClnt_post(chn->ht_clnt,uri,body);
  else ret=frontierHttpClnt_get(chn->ht_clnt,uri);
  if(ret) goto end;
  
  while(1)
   {
    ret=frontierHttpClnt_read(chn->ht_clnt,buf,8192);
    if(ret<=0) goto end;
#if 0
    frontier_log(FRONTIER_LOGLEVEL_DEBUG,__FILE__,__LINE__,"read %d bytes from server",ret);
#endif
    ret=write_data(chn->resp,buf,ret);
    if(ret!=FRONTIER_OK)  goto end;
   }
   
end:
  frontierHttpClnt_close(chn->ht_clnt);
        
  return ret;
 }
 

#define ERR_LAST_BUF_SIZE 256

int frontier_getRawData(FrontierChannel u_channel,const char *uri)
 {
  int ret;
  
  ret=frontier_postRawData(u_channel,uri,NULL);
  
  return ret;
 }
 
 
int frontier_postRawData(FrontierChannel u_channel,const char *uri,const char *body)
 {
  Channel *chn=(Channel*)u_channel;
  int ret=FRONTIER_OK;
  FrontierHttpClnt *clnt;
  char err_last_buf[ERR_LAST_BUF_SIZE];

  if(!chn) 
   {
    frontier_setErrorMsg(__FILE__,__LINE__,"wrong channel");
    return FRONTIER_EIARG;
   }
  
  clnt=chn->ht_clnt;
  chn->reload=chn->user_reload;
  bzero(err_last_buf,ERR_LAST_BUF_SIZE);
  
  while(1)
   {
    time_t now;
    now=time(NULL);
    frontier_log(FRONTIER_LOGLEVEL_DEBUG,__FILE__,__LINE__,"querying on chan %d at %s",chn->seqnum,ctime(&now));

    ret=get_data(chn,uri,body);    
    if(ret==FRONTIER_OK) 
     {
      ret=frontierResponse_finalize(chn->resp);
      now=time(NULL);
      frontier_log(FRONTIER_LOGLEVEL_DEBUG,__FILE__,__LINE__,"chan %d response %d finished at %s",chn->seqnum,chn->resp->seqnum,ctime(&now));
      if(ret==FRONTIER_OK) break;
     }
    now=time(NULL);
    frontier_log(FRONTIER_LOGLEVEL_WARNING,__FILE__,__LINE__,"Request %d on chan %d failed at %s: %d %s",chn->resp->seqnum,chn->seqnum,ctime(&now),ret,frontier_getErrorMsg());
    snprintf(err_last_buf,ERR_LAST_BUF_SIZE,"Request %d on chan %d failed: %d %s",chn->resp->seqnum,chn->seqnum,ret,frontier_getErrorMsg());
    
    if(!chn->reload)
     {
      frontier_log(FRONTIER_LOGLEVEL_WARNING,__FILE__,__LINE__,"Trying refresh cache");
      chn->reload=1;
      continue;
     }
    chn->reload=chn->user_reload;
    
    if(clnt->cur_proxy<clnt->total_proxy)
     {
      clnt->cur_proxy++;
      frontier_log(FRONTIER_LOGLEVEL_WARNING,__FILE__,__LINE__,"Trying next proxy (possibly direct connect)");
      continue;
     }
    if(clnt->cur_server+1<clnt->total_server)
     {
      clnt->cur_server++;
      frontier_log(FRONTIER_LOGLEVEL_WARNING,__FILE__,__LINE__,"Trying next server");
      continue;      
     }    
    frontier_setErrorMsg(__FILE__,__LINE__,"No more servers/proxies, last error: %s",err_last_buf);
    break;
   }
   
  return ret;
 }

int frontier_getRetrieveZipLevel(FrontierChannel u_channel)
 {
  Channel *chn=(Channel*)u_channel;

  return frontierConfig_getRetrieveZipLevel(chn->cfg);
 }

