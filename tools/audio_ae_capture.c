/* Offline capture of the production dummy-device callback; no game loop or assets embedded. */
#include "../src/audio.c"
#include "../src/sfx_game.c"

int main(int argc, char **argv) {
    if (argc != 6) return 2; /* root, engine|crash, car|strength, rpm|repeat, output.pcm */
    if (SDL_Init(SDL_INIT_AUDIO)) return 3;
    SDL_AudioDeviceID dev = audio_init();
    if (!dev) return 4;
    SDL_PauseAudioDevice(dev, 1);
    int16_t pcm[1024];
    if (!strcmp(argv[2], "engine")) {
        int sweeps = audio_load_ginsu_sweeps(argv[1], argv[3]);
        if (!sweeps && !audio_load_engine_bank(argv[1], audio_bank_for_car(argv[3]))) return 5;
        g_engine.current_rpm = g_engine.target_rpm = (float)atof(argv[4]);
        g_engine.current_load = g_engine.load = 1;
        g_engine.master_volume = 1;
        fprintf(stderr, "car=%s bank=%d sweeps=%d\n", argv[3], audio_bank_for_car(argv[3]), sweeps);
        for (int i=0;i<3;i++) fprintf(stderr,"band=%d rate=%.0f frames=%u base_hz=%.3f\n",i,g_engine.accel_bands[i].srate,g_engine.accel_bands[i].sample_count,g_engine.accel_bands[i].base_freq);
        if(sweeps) {
            GinsuSweep *sw=&g_engine.gin.accel_sweep;
            fprintf(stderr,"sweep rate=%u rpm=%.2f..%.2f markers=%d grains=%d samples=%u\n",sw->sample_rate,sw->rpm_min,sw->rpm_max,sw->n_rpm,sw->n_grain,sw->total_samples);
        }
        for (int k=0;k<44;k++) audio_cb(NULL,(Uint8 *)pcm,sizeof pcm);
    } else {
        g_sfx_game = sfg_create(argv[1],44100);
        if(!g_sfx_game || !(sfg_groups(g_sfx_game)&SFG_HAS_IMPACT)) return 6;
        SfxGame *g=g_sfx_game;
        fprintf(stderr,"impact classes=%d/%d/%d\n",g->nlight,g->nmedium,g->nheavy);
    }
    FILE *f=fopen(argv[5],"wb"); if(!f)return 7;
    for(int k=0;k<130;k++) {
        if(!strcmp(argv[2],"crash") && (k==0 || (atoi(argv[4]) && k%8==0))) g_hit=(float)atof(argv[3]);
        audio_cb(NULL,(Uint8 *)pcm,sizeof pcm);
        if(fwrite(pcm,1,sizeof pcm,f)!=sizeof pcm)return 8;
    }
    fclose(f); SDL_CloseAudioDevice(dev); SDL_Quit(); return 0;
}
