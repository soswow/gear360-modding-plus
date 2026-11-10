#include "storage.h"
#include "storage_platform.h"

#define SLOT_BYTES 20u
#define ROOT_LIMIT 400u

static void put32(unsigned char *p, uint32_t v)
{
    p[0]=(unsigned char)v; p[1]=(unsigned char)(v>>8);
    p[2]=(unsigned char)(v>>16); p[3]=(unsigned char)(v>>24);
}
static uint32_t get32(const unsigned char *p)
{
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static uint32_t checksum(const unsigned char *p)
{
    uint32_t hash=2166136261u;
    unsigned k;
    for(k=0;k<16;++k) { hash^=p[k]; hash*=16777619u; }
    return hash;
}
static int format_path(char *out, size_t size, const char *parent, const char *name)
{
    int n=snprintf(out,size,"%s/%s",parent,name);
    return n>=0 && (size_t)n<size ? 0 : -1;
}
static int sync_dir(const char *path)
{
    int fd=open(path,ST_RDONLY|ST_DIRECTORY|ST_NOFOLLOW|ST_CLOEXEC);
    int ok;
    if(fd<0) return -1;
    ok=fsync(fd);
    if(close(fd)!=0) ok=-1;
    return ok;
}
static int directory(const char *path)
{
    int fd;
    if(mkdir(path,0755)!=0 && errno!=EEXIST) return -1;
    fd=open(path,ST_RDONLY|ST_DIRECTORY|ST_NOFOLLOW|ST_CLOEXEC);
    if(fd<0) return -1;
    return close(fd);
}
static int sync_parent(const char *path)
{
    char parent[512];
    size_t length=strlen(path), k;
    while(length>1 && path[length-1]=='/') --length;
    k=length;
    while(k>0 && path[k-1]!='/') --k;
    if(k==0) return sync_dir(".");
    while(k>1 && path[k-1]=='/') --k;
    memcpy(parent,path,k); parent[k]='\0';
    return sync_dir(parent);
}
static int write_all(int fd, const unsigned char *data, size_t size)
{
    size_t done=0;
    while(done<size) {
        ptrdiff_t n=write(fd,data+done,size-done);
        if(n<0 && errno==EINTR) continue;
        if(n<=0 || (size_t)n>size-done) return -1;
        done+=(size_t)n;
    }
    return 0;
}
static int store_slot(int fd, uint32_t counter)
{
    unsigned char record[SLOT_BYTES];
    memcpy(record,"G360CTR1",8);
    put32(record+8,counter); put32(record+12,~counter); put32(record+16,checksum(record));
    if(lseek(fd,0,0)!=0 || write_all(fd,record,sizeof record)!=0 ||
       ftruncate(fd,SLOT_BYTES)!=0 || fsync(fd)!=0) return -1;
    return 0;
}
/* 1 valid,0 invalid,-1 IO. Reads one extra byte to reject trailing damage. */
static int load_slot(int fd, uint32_t *counter)
{
    unsigned char record[SLOT_BYTES+1];
    size_t used=0;
    if(lseek(fd,0,0)!=0) return -1;
    while(used<sizeof record) {
        ptrdiff_t n=read(fd,record+used,sizeof record-used);
        if(n<0 && errno==EINTR) continue;
        if(n<0) return -1;
        if(n==0) break;
        used+=(size_t)n;
    }
    if(used!=SLOT_BYTES || memcmp(record,"G360CTR1",8)!=0 ||
       get32(record+16)!=checksum(record) || get32(record+12)!=~get32(record+8) ||
       get32(record+8)>G360_STORAGE_MAX_COUNTER) return 0;
    *counter=get32(record+8);
    return 1;
}
static int open_slot(const char *path, int *created)
{
    int fd=open(path,ST_RDWR|ST_CREAT|ST_EXCL|ST_NOFOLLOW|ST_CLOEXEC,0644);
    *created=fd>=0;
    if(fd<0 && errno==EEXIST) fd=open(path,ST_RDWR|ST_NOFOLLOW|ST_CLOEXEC);
    return fd;
}
static int digit(char c) { return c>='0' && c<='9'; }
static int timestamp_valid(const char *p)
{
    unsigned k;
    if(p==0) return 0;
    for(k=0;k<15;++k) if(k==8 ? p[k]!='-' : !digit(p[k])) return 0;
    if(p[15]!='\0') return 0;
    /* Disallow nonsensical date/time fields while allowing leap seconds. */
    return (p[4]-'0')*10+p[5]-'0'>=1 && (p[4]-'0')*10+p[5]-'0'<=12 &&
           (p[6]-'0')*10+p[7]-'0'>=1 && (p[6]-'0')*10+p[7]-'0'<=31 &&
           (p[9]-'0')*10+p[10]-'0'<=23 && (p[11]-'0')*10+p[12]-'0'<=59 &&
           (p[13]-'0')*10+p[14]-'0'<=60;
}
static uint32_t photo_counter(const char *name)
{
    uint32_t value=0;
    unsigned k;
    size_t length=strlen(name);
    if(length!=25 || name[8]!='-' || name[15]!='-' || name[21]!='.') return 0;
    for(k=0;k<21;++k) {
        if(k==8 || k==15) continue;
        if(!digit(name[k])) return 0;
        if(k>=16) value=value*10u+(uint32_t)(name[k]-'0');
    }
    if(!((name[22]=='j'||name[22]=='J') && (name[23]=='p'||name[23]=='P') && (name[24]=='g'||name[24]=='G')) &&
       !((name[22]=='d'||name[22]=='D') && (name[23]=='n'||name[23]=='N') && (name[24]=='g'||name[24]=='G'))) return 0;
    return value;
}
static int scan_photos(const char *path, uint32_t *maximum)
{
    st_dir *dir=opendir(path);
    int ok=0;
    if(dir==0) return -1;
    for(;;) {
        st_dirent *item;
        uint32_t value;
        errno=0;
        item=st_readdir(dir);
        if(item==0) { if(errno!=0) ok=-1; break; }
        value=photo_counter(item->d_name);
        if(value>*maximum) *maximum=value;
    }
    if(closedir(dir)!=0) ok=-1;
    return ok;
}
void g360_storage_close(g360_storage *s)
{
    if(s==0) return;
    if(s->counter_fd[0]>=0) (void)close(s->counter_fd[0]);
    if(s->counter_fd[1]>=0) (void)close(s->counter_fd[1]);
    if(s->lock_fd>=0) (void)close(s->lock_fd);
    s->counter_fd[0]=s->counter_fd[1]=s->lock_fd=-1;
    s->opened=0;
}
int g360_storage_open(g360_storage *s, const char *root)
{
    char path[512];
    uint32_t values[2]={0,0}, maximum=0;
    int created[2]={0,0}, valid[2], status=G360_STORAGE_IO;
    unsigned k;
    if(s==0) return G360_STORAGE_ARGUMENT;
    memset(s,0,sizeof *s); s->lock_fd=s->counter_fd[0]=s->counter_fd[1]=-1;
    if(root==0 || root[0]=='\0' || strlen(root)>ROOT_LIMIT) return G360_STORAGE_ARGUMENT;
    if(format_path(s->photos,sizeof s->photos,root,"photos") ||
       format_path(s->state,sizeof s->state,root,"state")) return G360_STORAGE_ARGUMENT;
    if(directory(root)||sync_parent(root)||directory(s->photos)||directory(s->state)||sync_dir(root)) goto fail;
    if(format_path(path,sizeof path,s->state,"storage.lock")) goto fail;
    s->lock_fd=open(path,ST_RDWR|ST_CREAT|ST_NOFOLLOW|ST_CLOEXEC,0644);
    if(s->lock_fd<0) goto fail;
    if(flock(s->lock_fd,LOCK_EX|LOCK_NB)!=0) { status=G360_STORAGE_BUSY; goto fail; }
    for(k=0;k<2;++k) {
        if(format_path(path,sizeof path,s->state,k==0 ? "counter.0" : "counter.1")) goto fail;
        s->counter_fd[k]=open_slot(path,&created[k]);
        if(s->counter_fd[k]<0) goto fail;
        valid[k]=load_slot(s->counter_fd[k],&values[k]);
        if(valid[k]<0) goto fail;
    }
    if(!valid[0] && !valid[1] && !(created[0] && created[1])) {
        status=G360_STORAGE_CORRUPT; goto fail;
    }
    s->slot=valid[1] && (!valid[0] || values[1]>values[0]) ? 1u : 0u;
    maximum=values[s->slot];
    if(scan_photos(s->photos,&maximum)!=0) goto fail;
    s->counter=maximum;
    /* Repair a torn/missing alternate slot without rewriting the valid high
       slot. A higher photo sequence repairs stale/deleted state at open. */
    for(k=0;k<2;++k) {
        unsigned repair=k==0 ? s->slot^1u : s->slot;
        if((!valid[repair] || values[repair]!=maximum) &&
           store_slot(s->counter_fd[repair],maximum)!=0) goto fail;
    }
    if(sync_dir(s->state)!=0) goto fail;
    s->opened=1;
    return G360_STORAGE_OK;
fail:
    g360_storage_close(s);
    return status;
}
static int reserve_file(const char *path)
{
    int fd=open(path,ST_RDWR|ST_CREAT|ST_EXCL|ST_NOFOLLOW|ST_CLOEXEC,0644);
    int saved_errno;
    if(fd<0) return errno==EEXIST ? 1 : -1;
    if(fsync(fd)!=0) { saved_errno=errno; (void)close(fd); errno=saved_errno; return -1; }
    return close(fd)==0 ? 0 : -1;
}
/* jpg==0 reserves only the DNG, dng==0 only the JPEG (not both). */
static int reserve_names(g360_storage *s, const char stamp[16], char stem[64],
                         char *jpg, char *dng)
{
    char name[64], local_stem[64], local_jpg[512], local_dng[512];
    int n;
    if(s==0 || !s->opened || !timestamp_valid(stamp) || (jpg==0 && dng==0)) return G360_STORAGE_ARGUMENT;
    if(s->latched) return G360_STORAGE_LATCHED;
    for(;;) {
        int first=0,second=0;
        if(s->counter>=G360_STORAGE_MAX_COUNTER) return G360_STORAGE_EXHAUSTED;
        ++s->counter;
        s->slot^=1u;
        if(store_slot(s->counter_fd[s->slot],s->counter)!=0) goto io;
        n=snprintf(local_stem,sizeof local_stem,"%s-%05lu",stamp,(unsigned long)s->counter);
        if(n<0 || (size_t)n>=sizeof local_stem) goto io;
        n=snprintf(name,sizeof name,"%s.jpg",local_stem);
        if(n<0 || (size_t)n>=sizeof name || format_path(local_jpg,sizeof local_jpg,s->photos,name)) goto io;
        n=snprintf(name,sizeof name,"%s.dng",local_stem);
        if(n<0 || (size_t)n>=sizeof name || format_path(local_dng,sizeof local_dng,s->photos,name)) goto io;
        if(jpg) first=reserve_file(local_jpg);
        if(first<0) goto io;
        if(first>0) continue;
        if(dng) second=reserve_file(local_dng);
        if(second<0) goto io;
        if(second>0) continue; /* Keep our empty JPEG; never remove existing files. */
        if(sync_dir(s->photos)!=0) goto io;
        memcpy(stem,local_stem,strlen(local_stem)+1);
        if(jpg) memcpy(jpg,local_jpg,strlen(local_jpg)+1);
        if(dng) memcpy(dng,local_dng,strlen(local_dng)+1);
        return G360_STORAGE_OK;
    }
io:
    s->latched=1;
    return G360_STORAGE_IO;
}
int g360_storage_reserve_at(g360_storage *s, const char stamp[16], char stem[64],
                             char jpg[512], char dng[512], char exif_datetime[20])
{
    int status, n;
    if(stem==0 || (jpg==0 && dng==0) || exif_datetime==0) return G360_STORAGE_ARGUMENT;
    stem[0]=exif_datetime[0]='\0';
    if(jpg) jpg[0]='\0';
    if(dng) dng[0]='\0';
    status=reserve_names(s,stamp,stem,jpg,dng);
    if(status!=G360_STORAGE_OK) { stem[0]='\0'; if(jpg) jpg[0]='\0'; if(dng) dng[0]='\0'; return status; }
    n=snprintf(exif_datetime,20,"%.4s:%.2s:%.2s %.2s:%.2s:%.2s",stamp,stamp+4,stamp+6,stamp+9,stamp+11,stamp+13);
    if(n!=19) {
        s->latched=1;
        stem[0]=exif_datetime[0]='\0';
        if(jpg) jpg[0]='\0';
        if(dng) dng[0]='\0';
        return G360_STORAGE_IO;
    }
    return G360_STORAGE_OK;
}
int g360_storage_reserve_dng_at(g360_storage *s, const char stamp[16], char stem[64], char dng[512])
{
    int status;
    if(stem==0 || dng==0) return G360_STORAGE_ARGUMENT;
    stem[0]=dng[0]='\0';
    status=reserve_names(s,stamp,stem,0,dng);
    if(status!=G360_STORAGE_OK) stem[0]=dng[0]='\0';
    return status;
}
int g360_storage_timestamp(char stamp[16])
{
    capture_timespec now;
    int n;
#if defined(__APPLE__)
    struct tm tm;
    time_t seconds;
#else
    storage_tm tm;
    long seconds;
#endif
    if(stamp==0) return G360_STORAGE_ARGUMENT;
    stamp[0]='\0';
    if(clock_gettime(0,&now)!=0) return G360_STORAGE_CLOCK;
    seconds=now.tv_sec;
    if(localtime_r(&seconds,&tm)==0) return G360_STORAGE_CLOCK;
#if defined(__APPLE__)
    n=snprintf(stamp,16,"%04d%02d%02d-%02d%02d%02d",tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec);
#else
    n=snprintf(stamp,16,"%04d%02d%02d-%02d%02d%02d",tm.year+1900,tm.mon+1,tm.mday,tm.hour,tm.min,tm.sec);
#endif
    if(n!=15 || !timestamp_valid(stamp)) { stamp[0]='\0'; return G360_STORAGE_CLOCK; }
    return G360_STORAGE_OK;
}
int g360_storage_reserve(g360_storage *s, char stem[64], char jpg[512], char dng[512], char exif_datetime[20])
{
    char stamp[16];
    int status;
    if(stem==0 || (jpg==0 && dng==0) || exif_datetime==0) return G360_STORAGE_ARGUMENT;
    stem[0]=exif_datetime[0]='\0';
    if(jpg) jpg[0]='\0';
    if(dng) dng[0]='\0';
    status=g360_storage_timestamp(stamp);
    if(status!=G360_STORAGE_OK) return status;
    return g360_storage_reserve_at(s,stamp,stem,jpg,dng,exif_datetime);
}
const char *g360_storage_status_text(int status)
{
    switch(status) {
    case G360_STORAGE_OK:return "ok";
    case G360_STORAGE_ARGUMENT:return "invalid storage argument";
    case G360_STORAGE_IO:return "storage IO failure";
    case G360_STORAGE_BUSY:return "storage already in use";
    case G360_STORAGE_CORRUPT:return "both counter slots invalid";
    case G360_STORAGE_EXHAUSTED:return "five-digit counter exhausted";
    case G360_STORAGE_CLOCK:return "wall clock unavailable";
    case G360_STORAGE_LATCHED:return "storage latched after IO failure";
    default:return "unknown storage status";
    }
}
