/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#define __user
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include "dmx2.h"
#include "dsc_mtk.h"
#include "protocol.h"

/* Wire-level CA_SET_PID/CA_SET_DESCR messages must NEVER be passed directly to
 * vendor ioctls. The backend uses its own public-API CAS token/keyslot and the
 * vendor's source/PID/key interface, validated by the isolated session probe.
 */
#define FILTERS 64
struct section_filter { int fd; uint8_t demux, number; uint16_t pid; };
static volatile sig_atomic_t stopping;
static int fatal_error;
struct ca_backend { int fd; struct dscmb_keyslot slot; uint16_t pids[32]; unsigned count; };
static void ca_close(struct ca_backend *ca) {
    if(ca->fd<0)return;
    for(unsigned i=0;i<ca->count;i++) {
        unsigned pid=ca->pids[i];ioctl(ca->fd,CA_REMOVE_PID,&pid);
    }
    close(ca->fd);ca->fd=-1;ca->count=0;
}
static int ca_open(struct ca_backend *ca) {
    uint8_t bytes[8];uint64_t token=0;
    if(fread(bytes,1,sizeof(bytes),stdin)!=sizeof(bytes))return -1;
    for(unsigned i=0;i<8;i++)token=(token<<8)|bytes[i];
    memset(bytes,0,sizeof(bytes));if(!token)return -1;
    ca->fd=open("/dev/dvb0.ca2",O_RDWR|O_NONBLOCK|O_CLOEXEC);
    if(ca->fd<0)return -1;
    struct dscmb_slot_info state={0};
    if(ioctl(ca->fd,CA_GET_SLOT_INFO,&state)<0 || state.type!=CA_DESCR || state.flags!=CA_DES_UNUSED) {
        puts("CA_DEVICE_NOT_UNUSED");ca_close(ca);return -1;
    }
    unsigned source=0;
    if(ioctl(ca->fd,CA_SET_SOURCE,&source)<0 || ioctl(ca->fd,CA_SET_KEYTOKEN,&token)<0) {
        perror("CA_API_TOKEN");ca_close(ca);memset(&token,0,sizeof(token));return -1;
    }
    memset(&token,0,sizeof(token));
    ca->slot.flt_type=E_DSC_FLT_2_KEYS;
    if(ioctl(ca->fd,CA_DSC_GET_KEYSLOT,&ca->slot)<0 || ca->slot.engine_id>=3 || ca->slot.keyslot>=16) {
        puts("CA_KEYSLOT_FAILED");ca_close(ca);return -1;
    }
    struct dscmb_set_type type={.engine_id=ca->slot.engine_id,.keyslot=ca->slot.keyslot,.dsc_type=E_DSC_TYPE_CSA};
    if(ioctl(ca->fd,CA_DSC_SETTYPE,&type)!=1) {perror("CA_CSA_TYPE");ca_close(ca);return -1;}
    printf("CA_READY device=ca2 engine=%u slot=%u source=0\n",ca->slot.engine_id,ca->slot.keyslot);
    return 0;
}
static int ca_pid(struct ca_backend *ca,unsigned pid,int enable) {
    if(ca->fd<0)return 0;
    unsigned index=0;while(index<ca->count && ca->pids[index]!=pid)index++;
    if(enable) {
        if(index<ca->count)return 0;
        if(ca->count==32)return -1;
        if(ioctl(ca->fd,CA_ADD_PID,&pid)<0)return -1;
        ca->pids[ca->count++]=(uint16_t)pid;
    } else if(index<ca->count) {
        if(ioctl(ca->fd,CA_REMOVE_PID,&pid)<0)return -1;
        ca->pids[index]=ca->pids[--ca->count];
    }
    return 0;
}
static int ca_key(struct ca_backend *ca,uint32_t parity,const uint8_t *cw) {
    if(ca->fd<0)return 0;
    uint8_t key[16]={0};memcpy(key,cw,8);
    struct dscmb_key_iv request={.engine_id=ca->slot.engine_id,.keyslot=ca->slot.keyslot,
        .key_type=parity?E_DSC_KEY_ODD:E_DSC_KEY_EVEN,.key=(uintptr_t)key};
    int result=ioctl(ca->fd,CA_DSC_SETKEY,&request);memset(key,0,sizeof(key));
    return result==1?0:-1;
}
static void stop_signal(int sig) { (void)sig; stopping = 2; }
static long seconds_now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec;
}
static int send_all(int fd, const uint8_t *p, size_t n) {
    while (n) {
        ssize_t r = send(fd, p, n, MSG_NOSIGNAL);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        p += r; n -= (size_t)r;
    }
    return 0;
}
static void close_filter(struct section_filter *f) {
    if (f->fd >= 0) { ioctl(f->fd, DMX2_STOP); close(f->fd); f->fd = -1; }
}
static int section_open(uint16_t pid, const uint8_t *bits, const uint8_t *mask,
                        const uint8_t *mode, uint32_t timeout, uint32_t flags) {
    int fd = open("/dev/dvb0.demux20", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    struct dmx2_filter_settings f = {.input=DMX2_IN_FRONTEND, .fd=-1, .output=DMX2_OUT_TAP};
    f.ts.pid=pid; f.ts.data_type=DMX2_TS_DATA_SECTION;
    memcpy(f.ts.section.section_bits.filter,bits,16);
    memcpy(f.ts.section.section_bits.mask,mask,16);
    memcpy(f.ts.section.section_bits.mode,mode,16);
    f.ts.section.timeout=timeout;
    f.ts.section.flags=flags & 3;
    if (ioctl(fd, DMX2_SET_TS_FILTER, &f) < 0 || ioctl(fd, DMX2_START) < 0) {
        int error=errno; close(fd); errno=error; return -1;
    }
    return fd;
}
static int read_pmt(uint16_t pid, uint16_t sid, uint8_t *capmt, size_t capacity) {
    uint8_t bits[16]={2,(uint8_t)(sid>>8),(uint8_t)sid}, mask[16]={0xff,0xff,0xff}, mode[16]={0};
    int fd=section_open(pid,bits,mask,mode,0,DMX2_CHECK_CRC);
    if (fd<0) return -1;
    struct pollfd p={.fd=fd,.events=POLLIN};
    uint8_t section[8192]; int result=-1;
    if (poll(&p,1,5000)>0) {
        ssize_t n=read(fd,section,sizeof(section));
        if(n>=3) {
            size_t length=3+(mtk_be16(section+1)&0x0fff);
            if(length<=(size_t)n) result=mtk_capmt(section,length,sid,pid,capmt,capacity);
        }
    }
    ioctl(fd,DMX2_STOP); close(fd); return result;
}
static int pat_open(void) {
    uint8_t bits[16]={0},mask[16]={0xff},mode[16]={0};
    return section_open(0,bits,mask,mode,0,DMX2_CHECK_CRC);
}
static int pat_find(int fd,uint16_t service,uint16_t tsid) {
    long deadline=seconds_now()+5;
    while(!stopping && seconds_now()<deadline) {
        struct pollfd poller={.fd=fd,.events=POLLIN};uint8_t data[4096];
        if(poll(&poller,1,300)<=0)continue;
        ssize_t n=read(fd,data,sizeof(data));
        if(n<3)continue;
        for(size_t at=0;at+3<=(size_t)n;) {
            size_t length=3+(mtk_be16(data+at+1)&0x0fff);
            if(length>(size_t)n-at)break;
            int pid=mtk_pat_pmt(data+at,length,service,tsid);
            if(pid>0)return pid;
            at+=length;
        }
    }
    return -1;
}
int main(int argc,char **argv) {
    int hardware=argc>=5 && !strcmp(argv[1],"--hardware-from-cas-token");
    if((argc!=5 && argc!=6) || (!hardware && strcmp(argv[1],"--receive-only"))) {
        fprintf(stderr,"usage: %s --receive-only|--hardware-from-cas-token SERVICE PMT_PID SECONDS [TSID]\n",argv[0]); return 2;
    }
    unsigned long service=strtoul(argv[2],NULL,0), pmt=strtoul(argv[3],NULL,0);
    int duration=atoi(argv[4]);
    unsigned long tsid=argc==6?strtoul(argv[5],NULL,0):0;
    if(!service || service>65535 || pmt>=8191 || tsid>65535 || duration<0 || duration>300) return 2;
    signal(SIGTERM,stop_signal); signal(SIGINT,stop_signal); signal(SIGPIPE,SIG_IGN);
    pid_t parent=getppid();
    if(prctl(PR_SET_PDEATHSIG,SIGTERM)<0 || getppid()!=parent)return 1;
    setvbuf(stdout,NULL,_IOLBF,0);
    int lock=open("/data/local/tmp/oscam-mtk/bridge.lock",O_CREAT|O_RDWR|O_CLOEXEC,0600);
    if(lock<0 || flock(lock,LOCK_EX|LOCK_NB)<0) {puts("BRIDGE_ALREADY_RUNNING_OR_LOCK_FAILED");return 73;}
    int pat=pat_open();
    if(pat<0) {perror("PAT_OPEN");return 1;}
    int discovered=pat_find(pat,(uint16_t)service,(uint16_t)tsid);
    if(discovered<0 || (pmt && discovered!=(int)pmt)) {puts("PAT_SERVICE_UNAVAILABLE");close(pat);return 1;}
    pmt=(unsigned)discovered;
    struct section_filter filters[FILTERS];
    for(int i=0;i<FILTERS;i++) filters[i].fd=-1;
    uint8_t capmt[8192];
    int capmt_size=read_pmt((uint16_t)pmt,(uint16_t)service,capmt,sizeof(capmt));
    if(capmt_size<0) { fprintf(stderr,"PMT_UNAVAILABLE_OR_SERVICE_MISMATCH errno=%d\n",errno); return 1; }
    printf("PMT_READY service=%lu pmt_pid=%lu capmt_bytes=%d\n",service,pmt,capmt_size);
    int encrypted=mtk_capmt_encrypted(capmt,(size_t)capmt_size);
    if(encrypted<=0) {puts(encrypted==0?"FREE_TO_AIR":"INVALID_CA_PMT");close(pat);return encrypted==0?20:1;}
    uint8_t pmt_bits[16]={2,(uint8_t)(service>>8),(uint8_t)service};
    uint8_t pmt_mask[16]={0xff,0xff,0xff},zero[16]={0};
    int pmt_watch=section_open((uint16_t)pmt,pmt_bits,pmt_mask,zero,0,DMX2_CHECK_CRC);
    if(pmt_watch<0) {perror("PMT_WATCH");return 1;}
    // Build the whitelist from the validated CA_PMT elementary stream entries.
    uint16_t allowed[64];unsigned allowed_count=0;
    size_t body=capmt[3]==0x82?6:4;
    size_t next=body+6+(mtk_be16(capmt+body+4)&0x0fff);
    while(next<(size_t)capmt_size && allowed_count<64) {
        if((size_t)capmt_size-next<5)return 1;
        allowed[allowed_count++]=mtk_be16(capmt+next+1)&0x1fff;
        next+=5+(mtk_be16(capmt+next+3)&0x0fff);
    }
    struct ca_backend ca={.fd=-1};
    if(hardware && ca_open(&ca)<0)return 1;
    int sock=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);
    if(sock<0) {perror("socket");ca_close(&ca);return 1;}
    struct sockaddr_un addr={.sun_family=AF_UNIX};
    snprintf(addr.sun_path,sizeof(addr.sun_path),"/data/local/tmp/oscam-mtk/camd.socket");
    struct timeval timeout={.tv_sec=3};
    setsockopt(sock,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    if(connect(sock,(struct sockaddr*)&addr,sizeof(addr))<0) {perror("connect");close(sock);ca_close(&ca);return 1;}
    const char name[]="Philips-MTK-bridge/0.1";
    uint8_t hello[7+sizeof(name)-1];
    mtk_put32(hello,0xffff0001);mtk_put16(hello+4,1);hello[6]=sizeof(name)-1;
    memcpy(hello+7,name,sizeof(name)-1);
    if(send_all(sock,hello,sizeof(hello))<0 || send_all(sock,capmt,(size_t)capmt_size)<0) {
        perror("initial send");close(sock);ca_close(&ca);return 1;
    }
    uint8_t input[16384];size_t used=0;long deadline=seconds_now()+duration,last_cw=seconds_now();
    unsigned ecms=0,cws=0;int rc=1;
    while(!stopping && (!duration || seconds_now()<deadline)) {
        struct pollfd pfds[FILTERS+3];
        pfds[0]=(struct pollfd){.fd=sock,.events=POLLIN};
        for(int i=0;i<FILTERS;i++)pfds[i+1]=(struct pollfd){.fd=filters[i].fd,.events=POLLIN};
        pfds[FILTERS+1]=(struct pollfd){.fd=pat,.events=POLLIN};
        pfds[FILTERS+2]=(struct pollfd){.fd=pmt_watch,.events=POLLIN};
        int ready=poll(pfds,FILTERS+3,500);
        if(ready<0) {if(errno==EINTR)continue;perror("poll");break;}
        if(seconds_now()-last_cw>45) {puts("CW_TIMEOUT");fatal_error=1;break;}
        for(int table=0;table<2;table++)if(pfds[FILTERS+1+table].revents&POLLIN) {
            uint8_t section[8192];ssize_t n=read(table?pmt_watch:pat,section,sizeof(section));
            for(size_t at=0;n>0 && at+3<=(size_t)n;) {
                size_t length=3+(mtk_be16(section+at+1)&0x0fff);
                if(length>(size_t)n-at) {fatal_error=1;stopping=1;break;}
                int changed=0;
                if(!table) {
                    int found=mtk_pat_pmt(section+at,length,(uint16_t)service,(uint16_t)tsid);
                    changed=found<0 || (found>0 && found!=(int)pmt);
                } else {
                    uint8_t next[8192];
                    int size=mtk_capmt(section+at,length,(uint16_t)service,(uint16_t)pmt,next,sizeof(next));
                    changed=size!=capmt_size || (size>0 && memcmp(next,capmt,(size_t)size));
                }
                if(changed) {puts("TUNING_TABLE_CHANGED");rc=10;stopping=1;break;}
                at+=length;
            }
        }
        if(stopping)break;
        if(pfds[0].revents&(POLLHUP|POLLERR|POLLNVAL)) {puts("OSCAM_DISCONNECTED");break;}
        if(pfds[0].revents&POLLIN) {
            ssize_t got=recv(sock,input+used,sizeof(input)-used,0);
            if(got<=0) {puts("OSCAM_CLOSED");break;}
            used+=(size_t)got;
            for(;;) {
                int length=mtk_frame_size(input,used);
                if(length<0) {fprintf(stderr,"UNSUPPORTED_OSCAM_FRAME opcode=%08x\n",mtk_be32(input));stopping=1;break;}
                if(!length)break;
                uint32_t op=mtk_be32(input);
                if(op==0xffff0002) printf("OSCAM_SERVER protocol=%u name=%.*s\n",mtk_be16(input+4),input[6],input+7);
                else if(op==0x403c6f2b) {
                    unsigned number=input[6];uint16_t pid=mtk_be16(input+7);
                    if(number>=FILTERS || input[4]!=0 || input[5]!=0) {puts("FILTER_OUT_OF_RANGE");stopping=1;break;}
                    close_filter(&filters[number]);
                    filters[number]=(struct section_filter){.fd=section_open(pid,input+9,input+25,input+41,
                        mtk_be32(input+57),mtk_be32(input+61)),.demux=input[5],.number=number,.pid=pid};
                    if(filters[number].fd<0) {fprintf(stderr,"FILTER_OPEN_FAILED pid=%u errno=%d\n",pid,errno);stopping=1;break;}
                    if(ecms<4)printf("ECM_FILTER_START number=%u pid=%u\n",number,pid);
                } else if(op==0x00006f2a) {
                    if(input[6]<FILTERS)close_filter(&filters[input[6]]);
                } else if(op==0x40086f87) {
                    unsigned pid=mtk_be32(input+5);int32_t index=(int32_t)mtk_be32(input+9);
                    int valid=0;for(unsigned a=0;a<allowed_count;a++)if(allowed[a]==pid)valid=1;
                    if(!valid || (index!=0 && index!=-1) || ca_pid(&ca,pid,index>=0)<0) {
                        fprintf(stderr,"PID_BIND_FAILED pid=%u errno=%d\n",pid,errno);stopping=1;break;
                    }
                    printf("CW_PID_ASSOCIATION pid=%u index=%d hardware_applied=%d\n",pid,index,hardware);
                } else if(op==0x40106f86) {
                    uint32_t parity=mtk_be32(input+9);
                    if(parity>1 || mtk_be32(input+5)!=0) {puts("INVALID_CW_TARGET");stopping=1;break;}
                    if(ca_key(&ca,parity,input+13)<0) {fprintf(stderr,"CW_HARDWARE_FAILED errno=%d\n",errno);stopping=1;break;}
                    cws++;last_cw=seconds_now();
                    if(cws<4 || cws%60==0)printf("CW_RECEIVED index=%u parity=%u count=%u hardware_applied=%d\n",mtk_be32(input+5),parity,cws,hardware);
                    // Secrets stay in process memory only and are never printed.
                    memset(input+13,0,8);
                } else if(op==0x400c6f88) {
                    printf("CW_MODE index=%u algorithm=%u mode=%u\n",mtk_be32(input+5),mtk_be32(input+9),mtk_be32(input+13));
                    if(mtk_be32(input+5)!=0 || mtk_be32(input+9)!=0 || mtk_be32(input+13)>1) {
                        puts("UNSUPPORTED_CW_MODE");stopping=1;break;
                    }
                }
                used-=(size_t)length;memmove(input,input+length,used);
            }
            if(used==sizeof(input)) {puts("OSCAM_FRAME_TOO_LARGE");break;}
        }
        for(int i=0;i<FILTERS&&!stopping;i++) if(filters[i].fd>=0 && (pfds[i+1].revents&POLLIN)) {
            uint8_t section[8192];ssize_t n=read(filters[i].fd,section,sizeof(section));
            if(n<0 && (errno==EAGAIN||errno==EINTR))continue;
            if(n<=0) {fprintf(stderr,"FILTER_READ_FAILED number=%d errno=%d\n",i,errno);stopping=1;break;}
            size_t at=0;
            while((size_t)n-at>=3) {
                size_t length=3+(mtk_be16(section+at+1)&0x0fff);
                if(length>(size_t)n-at || length>4096) {puts("SECTION_TRUNCATED");stopping=1;break;}
                uint8_t frame[4102];mtk_put32(frame,0xffff0000);frame[4]=filters[i].demux;frame[5]=filters[i].number;
                memcpy(frame+6,section+at,length);
                if(send_all(sock,frame,length+6)<0) {stopping=1;break;}
                ecms++;if(ecms<4 || ecms%60==0)printf("FILTER_DATA_SENT pid=%u sections=%u\n",filters[i].pid,ecms);
                at+=length;
            }
        }
    }
    for(int i=0;i<FILTERS;i++)close_filter(&filters[i]);
    ca_close(&ca);
    ioctl(pat,DMX2_STOP);close(pat);ioctl(pmt_watch,DMX2_STOP);close(pmt_watch);
    close(sock);close(lock);memset(input,0,sizeof(input));
    printf("SUMMARY sections=%u cw_messages=%u hardware_applied=%d\n",ecms,cws,hardware);
    if(rc!=10 && !fatal_error && cws>0 && ((!stopping && duration && seconds_now()>=deadline) || stopping==2))rc=0;
    return rc;
}
