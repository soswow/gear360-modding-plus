#include "g360display.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct rectangle { int32_t top, bottom, left, right; };
struct drawing { struct rectangle rect; unsigned char pixels[2304]; };
static int mode, opens, closes, ioctls;
static struct drawing seen;

int g360display_test_open(const char *path, int flags)
{
    assert(strcmp(path,"/dev/ssd1309-ctrl")==0 && flags==2);
    ++opens;
    return mode==1 ? -1 : 42;
}
int g360display_test_ioctl(int fd, unsigned long request, void *arg)
{
    struct rectangle *r=arg;
    assert(fd==42);
    ++ioctls;
    if(ioctls==1) {
        assert(request==0x49105302UL);
        memcpy(&seen,arg,sizeof seen);
        r=&seen.rect;
    } else assert(ioctls==2 && request==0x40105304UL);
    assert(r->top==0 && r->bottom==31 && r->left==0 && r->right==71);
    if((mode==2 || mode==3) && ioctls==1) return mode==2 ? -1 : 1;
    if((mode==4 || mode==5) && ioctls==2) return mode==4 ? -1 : 1;
    return 0;
}
int g360display_test_close(int fd)
{
    assert(fd==42); ++closes;
    return mode==6 ? -1 : 0;
}
static void reset(int m) { mode=m; opens=closes=ioctls=0; memset(&seen,0xff,sizeof seen); }
static unsigned nonzero(const unsigned char *p)
{
    unsigned k,n=0;
    for(k=0;k<2304;++k) { assert(p[k]<=1); n+=p[k]; }
    return n;
}
int main(void)
{
    struct { unsigned char pre[16],pixels[2304],post[16]; } guarded;
    unsigned char expected[2304],before[2304];
    static const unsigned char r[5]={127,9,25,41,70};
    unsigned x,y,k;
    memset(&guarded,0xa5,sizeof guarded);
    assert(g360display_render_center("READY",guarded.pixels)==0);
    assert(nonzero(guarded.pixels)>0);
    /* READY has width29 and begins at (21,12); its initial R is checked
     * against the independent existing 5x7 glyph definition. */
    for(x=0;x<5;++x) for(y=0;y<7;++y)
        assert(guarded.pixels[(12+y)*72+21+x]==((r[x]>>y)&1u));
    for(y=0;y<32;++y) for(x=0;x<72;++x)
        if(y<12 || y>18 || x<21 || x>49) assert(guarded.pixels[y*72+x]==0);
    for(k=0;k<16;++k) assert(guarded.pre[k]==0xa5 && guarded.post[k]==0xa5);
    assert(g360display_render_center("CAPTURING...",guarded.pixels)==0);
    assert(nonzero(guarded.pixels)>0);
    assert(g360display_render_center("SAVING...",guarded.pixels)==0);
    assert(g360display_render_center("ERROR",guarded.pixels)==0);
    assert(g360display_render_center("0123456789AZ",guarded.pixels)==0);
    assert(g360display_render_center(" /:-.",guarded.pixels)==0);
    memcpy(before,guarded.pixels,sizeof before);
    assert(g360display_render_center("1234567890123",guarded.pixels)==-1);
    assert(memcmp(before,guarded.pixels,sizeof before)==0);
    assert(g360display_render_center("ready",guarded.pixels)==-1);
    assert(memcmp(before,guarded.pixels,sizeof before)==0);
    assert(g360display_render_center(0,guarded.pixels)==-1);
    assert(g360display_render_center("READY",0)==-1);
    assert(g360display_render_center("",guarded.pixels)==0);
    assert(nonzero(guarded.pixels)==0);
    /* Two and three lines: each centred, the block centred vertically.
       Two lines start at rows 7 and 17, three at rows 2, 12 and 22. */
    assert(g360display_render_center("READY\nR",guarded.pixels)==0);
    for(x=0;x<5;++x) for(y=0;y<7;++y) {
        assert(guarded.pixels[(7+y)*72+21+x]==((r[x]>>y)&1u));
        assert(guarded.pixels[(17+y)*72+33+x]==((r[x]>>y)&1u));
    }
    for(y=0;y<32;++y) for(x=0;x<72;++x)
        if(y<7 || (y>13 && y<17) || y>23) assert(guarded.pixels[y*72+x]==0);
    assert(g360display_render_center("R\n\nR",guarded.pixels)==0);
    for(x=0;x<5;++x) for(y=0;y<7;++y) {
        assert(guarded.pixels[(2+y)*72+33+x]==((r[x]>>y)&1u));
        assert(guarded.pixels[(12+y)*72+33+x]==0);
        assert(guarded.pixels[(22+y)*72+33+x]==((r[x]>>y)&1u));
    }
    assert(g360display_render_center("SETTINGS 1/3\nMODE: SINGLE\nCAPTURE: SET",guarded.pixels)==0);
    memcpy(before,guarded.pixels,sizeof before);
    assert(g360display_render_center("A\nB\nC\nD",guarded.pixels)==-1);
    assert(g360display_render_center("OK\n1234567890123",guarded.pixels)==-1);
    assert(g360display_render_center("OK\nok",guarded.pixels)==-1);
    assert(memcmp(before,guarded.pixels,sizeof before)==0);
    /* The bounded validation pass also supports text/destination aliasing. */
    strcpy((char*)guarded.pixels,"READY");
    assert(g360display_render_center((char*)guarded.pixels,guarded.pixels)==0);
    assert(g360display_render_center("READY",expected)==0);
    assert(memcmp(expected,guarded.pixels,sizeof expected)==0);
    /* The status corner: rows 0-6, right-aligned; text below it. */
    {
        static const unsigned char seven[5]={1,113,9,5,3};
        static const char *const fan[7]={".XXXXXXX.","X.......X","..XXXXX..",".X.....X.","...XXX...",".........","....X...."};
        g360display_status s={.wifi=G360DISPLAY_WIFI_HOME,.battery=87,.mode=G360DISPLAY_MODE_NONE};
        unsigned lit;
        /* Search overrides the AP icon and progresses from dot to full fan. */
        s.wifi=G360DISPLAY_WIFI_AP; s.wifi_searching=1;
        s.wifi_frame=0;
        assert(g360display_render_status("",&s,guarded.pixels)==0);
        for(y=0;y<7;++y) for(x=0;x<9;++x)
            assert(guarded.pixels[y*72+56+x]==(y==6 && x==4));
        s.wifi_frame=1;
        assert(g360display_render_status("",&s,guarded.pixels)==0);
        assert(guarded.pixels[2*72+58] && !guarded.pixels[0*72+57] && guarded.pixels[6*72+60]);
        s.wifi_frame=2;
        assert(g360display_render_status("",&s,guarded.pixels)==0);
        for(y=0;y<7;++y) for(x=0;x<9;++x)
            assert(guarded.pixels[y*72+56+x]==(fan[y][x]=='X'));
        s.wifi_searching=0; s.wifi_frame=0; s.ap_connected=0;
        assert(g360display_render_status("",&s,guarded.pixels)==0);
        memcpy(before,guarded.pixels,sizeof before);
        s.ap_connected=1;
        assert(g360display_render_status("",&s,guarded.pixels)==0);
        assert(memcmp(before,guarded.pixels,sizeof before)!=0);
        /* AP centre is a filled 3x3 at x=58..60, with no outer ring. */
        for(y=2;y<=4;++y) for(x=58;x<=60;++x) assert(guarded.pixels[y*72+x]);
        s.wifi=G360DISPLAY_WIFI_OFF; s.ap_connected=0;
        assert(g360display_render_status("",&s,guarded.pixels)==0);
        for(y=0;y<7;++y) for(x=56;x<65;++x) assert(!guarded.pixels[y*72+x]);
        s.wifi_error=1;
        assert(g360display_render_status("",&s,guarded.pixels)==0);
        assert(guarded.pixels[0*72+57]);
        s.wifi=G360DISPLAY_WIFI_HOME; s.wifi_error=0;
        assert(g360display_render_status("READY",&s,guarded.pixels)==0);
        /* READY moves down to row 17, still centred across. */
        for(x=0;x<5;++x) for(y=0;y<7;++y)
            assert(guarded.pixels[(17+y)*72+21+x]==((r[x]>>y)&1u));
        for(y=7;y<17;++y) for(x=0;x<72;++x) assert(guarded.pixels[y*72+x]==0);
        for(y=24;y<32;++y) for(x=0;x<72;++x) assert(guarded.pixels[y*72+x]==0);
        /* The battery icon is 4 x 7 at the right edge (68-71): a terminal on
           top, walls, a floor, and an interior that fills from the bottom. 87%
           is four rows. The fan is 3 px before it (56-64); nothing else is lit. */
        {
            static const char *const full4[7]={".XX.","X..X","XXXX","XXXX","XXXX","XXXX","XXXX"};
            for(y=0;y<7;++y) {
                for(x=0;x<4;++x) assert(guarded.pixels[y*72+68+x]==(full4[y][x]=='X'));
                for(x=0;x<9;++x) assert(guarded.pixels[y*72+56+x]==(fan[y][x]=='X'));
                for(x=65;x<68;++x) assert(guarded.pixels[y*72+x]==0);
                for(x=0;x<56;++x) assert(guarded.pixels[y*72+x]==0);
            }
        }
        /* The six fill steps: 0-20, 21-40, 41-60, 61-80, 81-99, 100 %. */
        {
            static const int percents[][2]={{0,0},{1,0},{20,0},{21,1},{40,1},{41,2},{60,2},{61,3},{80,3},{81,4},{99,4},{100,5}};
            unsigned n;
            for(n=0;n<sizeof percents/sizeof percents[0];++n) {
                s.battery=percents[n][0];
                assert(g360display_render_status("",&s,guarded.pixels)==0);
                for(y=0;y<7;++y) for(x=0;x<4;++x) {
                    int wall=(x==0||x==3), lit_row;
                    int expect;
                    if(y==0) expect=(x==1||x==2);
                    else if(y==6) expect=1;
                    else { lit_row=(int)y>5-percents[n][1]; expect=wall||lit_row; }
                    assert(guarded.pixels[y*72+68+x]==expect);
                }
            }
            s.battery=87;
        }
        /* Charging: the walls rise from the floor, one row per frame; the
           terminal and floor are always lit and the fill is unchanged
           (here 0-20 %, as the owner drew it). */
        {
            unsigned f;
            s.charging=1; s.battery=10;
            for(f=0;f<G360DISPLAY_CHARGE_FRAMES;++f) {
                s.charge_frame=(int)f;
                assert(g360display_render_status("",&s,guarded.pixels)==0);
                for(y=0;y<7;++y) for(x=0;x<4;++x) {
                    int expect;
                    if(y==0) expect=(x==1||x==2);
                    else if(y==6) expect=1;
                    else expect=(x==0||x==3) && y+f>5;
                    assert(guarded.pixels[y*72+68+x]==expect);
                }
            }
            /* The last frame is the plain shell, and the fill shows inside. */
            s.battery=61; s.charge_frame=5;
            assert(g360display_render_status("",&s,guarded.pixels)==0);
            assert(guarded.pixels[1*72+69]==0 && guarded.pixels[2*72+69]==0 && guarded.pixels[3*72+69]==1 &&
                   guarded.pixels[1*72+68]==1 && guarded.pixels[0*72+68]==0);
            s.charging=0; s.charge_frame=0; s.battery=87;
        }
        /* The frame doesn't matter while not charging. */
        s.charge_frame=3;
        assert(g360display_render_status("READY",&s,guarded.pixels)==0);
        memcpy(before,guarded.pixels,sizeof before);
        s.charge_frame=0;
        assert(g360display_render_status("READY",&s,guarded.pixels)==0);
        assert(memcmp(before,guarded.pixels,sizeof before)==0);
        /* 100% is not wider: the corner stays where it was. */
        s.battery=100;
        assert(g360display_render_status("READY",&s,guarded.pixels)==0);
        for(y=0;y<7;++y) {
            for(x=0;x<9;++x) assert(guarded.pixels[y*72+56+x]==(fan[y][x]=='X'));
            for(x=0;x<56;++x) assert(guarded.pixels[y*72+x]==0);
        }
        (void)seven;
        /* The mode icon, left of the fan with 3 px between: one camera
           (9 wide) or a camera with a second behind it (11 wide). */
        {
            static const char *const one[7]={"..XXX....","XXXXXXXXX","X.......X","X..XXX..X","X..X.X..X","X..XXX..X","XXXXXXXXX"};
            static const char *const two[7]={"..XXX......","XXXXXXXX.XX","X......X..X","X..XXX.X..X","X..X.X.X..X","X..XXX.X..X","XXXXXXXX.XX"};
            s.battery=87; s.charging=0; s.wifi=G360DISPLAY_WIFI_HOME;
            s.mode=G360DISPLAY_MODE_SINGLE;
            assert(g360display_render_status("READY",&s,guarded.pixels)==0);
            for(y=0;y<7;++y) {
                for(x=0;x<9;++x) assert(guarded.pixels[y*72+(56-3-9)+x]==(one[y][x]=='X'));
                for(x=0;x<9;++x) assert(guarded.pixels[y*72+56+x]==(fan[y][x]=='X'));
                for(x=0;x<44;++x) assert(guarded.pixels[y*72+x]==0);
            }
            s.mode=G360DISPLAY_MODE_SWEEP;
            assert(g360display_render_status("READY",&s,guarded.pixels)==0);
            for(y=0;y<7;++y) {
                for(x=0;x<11;++x) assert(guarded.pixels[y*72+(56-3-11)+x]==(two[y][x]=='X'));
                for(x=0;x<42;++x) assert(guarded.pixels[y*72+x]==0);
            }
            /* With an unknown battery the fan is at the edge, the icon beside it. */
            s.battery=-1; s.wifi=G360DISPLAY_WIFI_OFF;
            assert(g360display_render_status("",&s,guarded.pixels)==0);
            for(y=0;y<7;++y) for(x=0;x<11;++x) assert(guarded.pixels[y*72+(63-3-11)+x]==(two[y][x]=='X'));
            /* The self-timer: a watch face at 3 o'clock, 7 wide, 3 px left of the
               mode icon (or of the fan when there is no mode icon). */
            {
                static const char *const watch[7]={"..XXX..",".X.X.X.","X..X..X","X..XXXX","X.....X",".X...X.","..XXX.."};
                s.battery=87; s.wifi=G360DISPLAY_WIFI_HOME; s.mode=G360DISPLAY_MODE_SINGLE; s.timer=1;
                assert(g360display_render_status("READY",&s,guarded.pixels)==0);
                for(y=0;y<7;++y) {
                    for(x=0;x<7;++x) assert(guarded.pixels[y*72+(56-3-9-3-7)+x]==(watch[y][x]=='X'));
                    for(x=0;x<9;++x) assert(guarded.pixels[y*72+(56-3-9)+x]==(one[y][x]=='X'));
                    for(x=0;x<34;++x) assert(guarded.pixels[y*72+x]==0);
                }
                s.mode=G360DISPLAY_MODE_NONE;
                assert(g360display_render_status("READY",&s,guarded.pixels)==0);
                for(y=0;y<7;++y) {
                    for(x=0;x<7;++x) assert(guarded.pixels[y*72+(56-3-7)+x]==(watch[y][x]=='X'));
                    for(x=0;x<46;++x) assert(guarded.pixels[y*72+x]==0);
                }
                s.timer=0;
            }
            s.mode=G360DISPLAY_MODE_NONE;
        }
        /* Unknown battery: only the Wi-Fi icon, at the right edge. The
           access point icon is 11 wide, the no-network icon 9. */
        s.battery=-1; s.wifi=G360DISPLAY_WIFI_AP;
        assert(g360display_render_status("",&s,guarded.pixels)==0);
        for(lit=0,k=0;k<2304;++k) if(guarded.pixels[k]) { assert(k%72>=61 && k/72<7); ++lit; }
        assert(lit>0 && guarded.pixels[3*72+66]==1 && guarded.pixels[0*72+61]==0);
        s.wifi=G360DISPLAY_WIFI_OFF; s.wifi_error=1;
        assert(g360display_render_status("",&s,guarded.pixels)==0);
        for(k=0;k<2304;++k) if(guarded.pixels[k]) assert(k%72>=63 && k/72<7);
        assert(guarded.pixels[4*72+66]==1 && guarded.pixels[4*72+67]==0 && guarded.pixels[5*72+67]==1);
        /* Two lines sit at rows 12 and 22. */
        s.battery=5; s.wifi=G360DISPLAY_WIFI_HOME; s.wifi_error=0;
        assert(g360display_render_status("READY\nR",&s,guarded.pixels)==0);
        for(x=0;x<5;++x) for(y=0;y<7;++y) {
            assert(guarded.pixels[(12+y)*72+21+x]==((r[x]>>y)&1u));
            assert(guarded.pixels[(22+y)*72+33+x]==((r[x]>>y)&1u));
        }
        assert(g360display_render_status("SETTINGS 1/3\nMODE: SINGLE",&s,guarded.pixels)==0);
        /* RAW on: an R, 3 px left of the (absent) timer and mode icons' place; sounds on: the notes. */
        s.battery=50; s.wifi=G360DISPLAY_WIFI_HOME; s.mode=G360DISPLAY_MODE_SINGLE; s.timer=0; s.raw=1; s.sound=0;
        assert(g360display_render_status("",&s,guarded.pixels)==0);
        /* Battery 4 + 3 + Wi-Fi 9 + 3 + mode 9 + 3 puts the R at columns 36-40. */
        for(x=0;x<5;++x) for(y=0;y<7;++y) assert(guarded.pixels[y*72+36+x]==((r[x]>>y)&1u));
        for(k=0;k<2304;++k) if(guarded.pixels[k] && k%72<36) assert(0);
        s.sound=1; s.raw=0;
        assert(g360display_render_status("",&s,guarded.pixels)==0);
        /* The notes: 7 wide, their beam on row 0 from column 2 (R absent: columns 34-40). */
        for(x=0;x<7;++x) assert(guarded.pixels[0*72+34+x]==(x>=2));
        for(k=0;k<2304;++k) if(guarded.pixels[k] && k%72<34) assert(0);
        s.raw=1;
        assert(g360display_render_status("",&s,guarded.pixels)==0);
        for(x=0;x<5;++x) for(y=0;y<7;++y) assert(guarded.pixels[y*72+36+x]==((r[x]>>y)&1u));
        for(x=0;x<7;++x) assert(guarded.pixels[0*72+26+x]==(x>=2));
        s.raw=s.sound=0;
        /* A single line of digits is drawn at double size, rows 13-26, centred. */
        assert(g360display_render_status("7",&s,guarded.pixels)==0);
        {   /* "7" is {1,113,9,5,3}: column x=1 has bits 0,4,5,6; each pixel becomes 2 x 2. */
            static const unsigned char seven[5]={1,113,9,5,3};
            unsigned lit_big=0;
            for(y=9;y<32;++y) for(x=0;x<72;++x) lit_big+=guarded.pixels[y*72+x];
            for(x=0;x<5;++x) for(y=0;y<7;++y) if((seven[x]>>y)&1u) {
                unsigned dx,dy;
                for(dx=0;dx<2;++dx) for(dy=0;dy<2;++dy) assert(guarded.pixels[(13+2*y+dy)*72+31+2*x+dx]==1);
                lit_big-=4;
            }
            assert(lit_big==0);
        }
        /* Up to six digits are doubled; seven are normal size; two lines or other characters stay normal size. */
        assert(g360display_render_status("123456",&s,guarded.pixels)==0);
        for(lit=0,k=13*72;k<17*72;++k) lit+=guarded.pixels[k];
        assert(lit>0); /* double size: rows 13-26 */
        assert(g360display_render_status("1234567",&s,guarded.pixels)==0);
        for(lit=0,k=13*72;k<17*72;++k) lit+=guarded.pixels[k];
        assert(lit==0); /* normal size, row 17 */
        assert(g360display_render_status("12 SHOTS",&s,guarded.pixels)==0);
        /* Refusals leave the pixels alone. */
        memcpy(before,guarded.pixels,sizeof before);
        assert(g360display_render_status("A\nB\nC",&s,guarded.pixels)==-1);
        assert(g360display_render_status("ready",&s,guarded.pixels)==-1);
        s.battery=101; assert(g360display_render_status("A",&s,guarded.pixels)==-1);
        s.battery=-2; assert(g360display_render_status("A",&s,guarded.pixels)==-1);
        s.battery=50; s.charge_frame=6; assert(g360display_render_status("A",&s,guarded.pixels)==-1);
        s.charge_frame=-1; assert(g360display_render_status("A",&s,guarded.pixels)==-1);
        s.charge_frame=0;
        s.battery=50; s.wifi=3; assert(g360display_render_status("A",&s,guarded.pixels)==-1);
        s.wifi=-1; assert(g360display_render_status("A",&s,guarded.pixels)==-1);
        s.wifi=G360DISPLAY_WIFI_AP; s.mode=3; assert(g360display_render_status("A",&s,guarded.pixels)==-1);
        s.mode=-1; assert(g360display_render_status("A",&s,guarded.pixels)==-1);
        s.mode=0;
        s.wifi=G360DISPLAY_WIFI_AP;
        assert(g360display_render_status(0,&s,guarded.pixels)==-1);
        assert(g360display_render_status("A",0,guarded.pixels)==-1);
        assert(g360display_render_status("A",&s,0)==-1);
        assert(memcmp(before,guarded.pixels,sizeof before)==0);
        for(k=0;k<16;++k) assert(guarded.pre[k]==0xa5 && guarded.post[k]==0xa5);
        /* The status text font also has %, in ordinary text too. */
        assert(g360display_render_center("100%",guarded.pixels)==0);
        assert(g360display_render_status("OUTPUT 2/7\nRAW+JPEG",&s,guarded.pixels)==0);
        assert(g360display_render_status("OUTPUT\n- RAW+JPEG -",&s,guarded.pixels)==0);
        reset(0);
        s.battery=42; s.charging=1;
        assert(g360display_render_status("READY",&s,before)==0);
        assert(g360display_show_status("READY",&s)==0 && opens==1 && ioctls==2 && closes==1);
        assert(memcmp(seen.pixels,before,sizeof before)==0);
        reset(0);
        assert(g360display_show_status("A\nB\nC",&s)==-1 && opens==0);
        reset(1);
        assert(g360display_show_status("READY",&s)==-1 && opens==1 && closes==0);
    }
    assert(g360display_render_center("READY",expected)==0);
    reset(0);
    assert(g360display_show("READY")==0 && opens==1 && ioctls==2 && closes==1);
    assert(memcmp(seen.pixels,expected,sizeof expected)==0);
    reset(0);
    assert(g360display_clear()==0 && opens==1 && ioctls==2 && closes==1);
    assert(nonzero(seen.pixels)==0);
    reset(0);
    assert(g360display_show("unsupported")==-1 && opens==0 && ioctls==0 && closes==0);
    for(k=1;k<=6;++k) {
        reset((int)k);
        assert(g360display_show("READY")==-1 && opens==1);
        assert(closes==(k==1 ? 0 : 1));
        assert(ioctls==(k==1 ? 0 : k==2 || k==3 ? 1 : 2));
    }
    puts("g360display render, ABI and device-failure tests passed");
    return 0;
}
