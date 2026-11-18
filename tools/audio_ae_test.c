/* Synthetic audio regression checks; includes the private callback for offline rendering. */
#include "../src/audio.c"
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
static void test_sine(void) {
    /* 1 kHz at 32 kHz into the native grain mixer, measured at 44.1 kHz.
       DFT separates the existing auxiliary turbo tone from the test signal. */
    int16_t src[321],out[44100];uint32_t grain[2]={0,320};
    for(int i=0;i<=320;i++)src[i]=(int16_t)(10000*sin(6.283185307179586*i/32));
    Ginsu20State *g=&g_engine.gin;
    for(int a=0;a<2;a++) {
        GinsuSweep *sw=a?&g->accel_sweep:&g->decel_sweep;
        sw->samples=src;sw->total_samples=321;sw->sample_rate=32000;
        sw->n_grain=1;sw->grain=grain;g->grain_rpm[a]=3000;
    }
    g->has_gin_data=1;
    g_engine.current_rpm=g_engine.target_rpm=3000;
    g_engine.current_load=g_engine.load=1;g_engine.master_volume=1;
    audio_cb(NULL,(Uint8 *)out,sizeof out);
    double re=0,im=0;
    for(int i=0;i<44100;i++) {
        double phase=6.283185307179586*1000*i/44100;
        re+=out[i]*cos(phase);im+=out[i]*sin(phase);
    }
    double amp=2*hypot(re,im)/44100,expected=10000*.55*11000/32768;
    printf("sine: input 1000 Hz amplitude 10000; output 1000 Hz amplitude %.3f expected %.3f gain %.6f\n",amp,expected,amp/10000);
    assert(fabs(amp/expected-1)<.01);
    memset(g,0,sizeof *g);
}
int main(void) {
    test_selection();test_sine();audio_selftest();
    puts("audio_ae_test: PASS");return 0;
}
