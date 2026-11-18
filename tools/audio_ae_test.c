/* Synthetic audio regression checks; includes the private callback for offline rendering. */
#include "../src/audio.c"
#include "../src/sfx_game.c"
#include <sys/stat.h>
#include <unistd.h>

static void put32(FILE *f,uint32_t v) {
    unsigned char b[4]={v,v>>8,v>>16,v>>24}; assert(fwrite(b,1,4,f)==4);
}
static void mock_gin(const char *dir,const char *name,unsigned rate) {
    char path[1024];snprintf(path,sizeof path,"%s/%s",dir,name);
    FILE *f=fopen(path,"wb");assert(f);
    assert(fwrite("Gnsu20\0\0",1,8,f)==8);
    float rpm[2]={1000,8000};assert(fwrite(rpm,4,2,f)==2);
    put32(f,2);put32(f,2);put32(f,4096);put32(f,rate);
    for(int j=0;j<2;j++){put32(f,0);put32(f,2048);put32(f,4095);}
    for(int i=0;i<128*19;i++)fputc(0,f);
    assert(!fclose(f));
}
static void test_selection(void) {
    char root[]="/tmp/audio-ae-XXXXXX",dir[128],path[1024];assert(mkdtemp(root));
    snprintf(dir,sizeof dir,"%s/SOUND",root);assert(!mkdir(dir,0700));
    snprintf(dir,sizeof dir,"%s/SOUND/ENGINE",root);assert(!mkdir(dir,0700));
    const char *names[]={"GIN_GTi_Pull_01b_shrt.gin","GIN_GTi_01b_DCL.gin","GIN_VW_Drag_GTi.gin"};
    for(int i=0;i<3;i++)mock_gin(dir,names[i],i==2?16000:32000);
    assert(audio_load_ginsu_sweeps(root,"GOLF")==2);
    assert(g_engine.gin.accel_sweep.sample_rate==32000);
    assert(g_engine.gin.decel_sweep.sample_rate==32000);
    for(int i=0;i<3;i++){snprintf(path,sizeof path,"%s/%s",dir,names[i]);assert(!unlink(path));}
    assert(!rmdir(dir));snprintf(dir,sizeof dir,"%s/SOUND",root);assert(!rmdir(dir));assert(!rmdir(root));
    free_sweep(&g_engine.gin.accel_sweep);free_sweep(&g_engine.gin.decel_sweep);
    memset(&g_engine.gin,0,sizeof g_engine.gin);
}
static void test_rpm_endpoints(void) {
    uint32_t ascending[3]={100,500,1000},descending[3]={1000,500,100};
    GinsuSweep s;memset(&s,0,sizeof s);
    s.rpm_min=1000;s.rpm_max=7000;s.n_rpm=2;
    s.rpm_pos=ascending;
    assert(gin_rpm_at(&s,50)==1000);assert(gin_rpm_at(&s,1050)==7000);
    assert(gin_rpm_at(&s,300)==2500);assert(gin_rpm_at(&s,750)==5500);
    s.rpm_pos=descending;
    assert(gin_rpm_at(&s,50)==7000);assert(gin_rpm_at(&s,1050)==1000);
    assert(gin_rpm_at(&s,750)==2500);assert(gin_rpm_at(&s,300)==5500);
    puts("RPM curve: ascending/descending interpolation and endpoint clamping: PASS");
}
static void test_sine(float rpm,float load,float hz,unsigned rate) {
    /* Coherent tones through the native grain mixer, measured at 44.1 kHz.
       DFT separates the existing auxiliary turbo tone from the test signal. */
    int16_t src[321],out[44100];uint32_t count=rate/100;
    uint32_t grain[2]={0,count},positions[3]={100,90,80};
    for(unsigned i=0;i<=count;i++)src[i]=(int16_t)(10000*sin(6.283185307179586*hz*i/rate));
    Ginsu20State *g=&g_engine.gin;
    for(int a=0;a<2;a++) {
        GinsuSweep *sw=a?&g->accel_sweep:&g->decel_sweep;
        sw->samples=src;sw->total_samples=count+1;sw->sample_rate=rate;
        sw->n_grain=1;sw->grain=grain;
        sw->rpm_min=1000;sw->rpm_max=7000;sw->n_rpm=2;sw->rpm_pos=positions;
        /* At coast the grain midpoint extends beyond the descending RPM
         * curve. The old clamp pitched this 1 kHz input down to 143 Hz. */
        g->grain_rpm[a]=load ? 3000 : gin_rpm_at(sw,count*.5f);
    }
    g->has_gin_data=1;
    g_engine.current_rpm=g_engine.target_rpm=rpm;
    g_engine.current_load=g_engine.load=load;g_engine.master_volume=1;
    audio_cb(NULL,(Uint8 *)out,sizeof out);
    double re=0,im=0;
    for(int i=0;i<44100;i++) {
        double phase=6.283185307179586*hz*i/44100;
        re+=out[i]*cos(phase);im+=out[i]*sin(phase);
    }
    double amp=2*hypot(re,im)/44100,bus=10000*.55*11000/32768;
    double x=3.141592653589793*hz/rate,sinc=sin(x)/x;
    double expected=bus*sinc*sinc; /* first-order-hold frequency response */
    printf("%s sine: rate %u, %.0f Hz in/out, amplitude %.3f, linear-SRC expected %.3f, bus %.3f, loss %.3f dB\n",
           load?"full-load":"coast endpoint",rate,hz,amp,expected,bus,20*log10(amp/bus));
    assert(fabs(amp/expected-1)<.01);
    memset(g,0,sizeof *g);
}
static void test_impact(void) {
    int16_t sine[4410],out[4410]={0};
    for(int i=0;i<4410;i++)sine[i]=(int16_t)(10000*sin(6.283185307179586*1000*i/44100));
    SfxGame g;memset(&g,0,sizeof g);
    g.mix=sfx_player_create(44100);assert(g.mix);
    g.rate=g.since_impact=44100;g.have_impact=1;
    g.nlight=g.nmedium=g.nheavy=1;
    g.impact[0].pcm=sine;g.impact[0].rate=44100;g.impact[0].frames=4410;
    g.impact_gain[0]=.5f;
    SfgInput in;memset(&in,0,sizeof in);in.master=1;in.hit=.45f;
    sfg_render(&g,&in,out,4410);
    double re=0,im=0;
    for(int i=0;i<4410;i++) {
        double phase=6.283185307179586*1000*i/44100;
        re+=out[i]*cos(phase);im+=out[i]*sin(phase);
        assert(abs(out[i]-(int)(sine[i]*.4f))<=1);
    }
    double amp=2*hypot(re,im)/4410;
    assert(fabs(amp-4000)<2);
    printf("impact sine: 1000 Hz in/out, amplitude %.3f, expected 4000, gain %.6f\n",amp,amp/10000);
    /* A burst within the 80 ms guard cannot add another impact. */
    g.since_impact=0;memset(out,0,sizeof out);
    sfg_render(&g,&in,out,441);
    for(int i=0;i<441;i++)assert(out[i]==0);
    g.since_impact=44100;in.master=0;memset(out,0,sizeof out);
    sfg_render(&g,&in,out,441);
    for(int i=0;i<441;i++)assert(out[i]==0);
    sfx_player_destroy(g.mix);
}
static void test_wall_burst(void) {
    audio_wall_contact(0,0,1);g_hit=0;
    int events=0;
    for(int i=0;i<120;i++) {
        audio_wall_contact(20,4,1.0f/60);
        if(g_hit>0)events++;
        g_hit=0;
    }
    assert(events==1);
    audio_wall_contact(0,0,.05f);audio_wall_contact(20,4,.02f);assert(g_hit==0);
    audio_wall_contact(0,0,.13f);audio_wall_contact(20,4,.02f);assert(g_hit>0);
    audio_wall_contact(0,0,1);g_hit=0;
    puts("wall: one event in 120 contact ticks; separation rearms: PASS");
}
int main(void) {
    test_selection();test_rpm_endpoints();
    float hz[4]={1000,2000,6000,10000};
    for(int i=0;i<4;i++){test_sine(3000,1,hz[i],32000);test_sine(3000,1,hz[i],24000);}
    test_sine(1000,0,1000,32000);
    test_impact();test_wall_burst();audio_selftest();
    puts("audio_ae_test: PASS");return 0;
}
