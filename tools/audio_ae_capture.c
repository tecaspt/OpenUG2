/* Offline capture of the production dummy-device callback; no game loop or assets embedded. */
#ifndef AUDIO_AE_ENGINE_SOURCE
#define AUDIO_AE_ENGINE_SOURCE "../src/audio.c"
#endif
#ifndef AUDIO_AE_GAME_SOURCE
#define AUDIO_AE_GAME_SOURCE "../src/sfx_game.c"
#endif
#include AUDIO_AE_ENGINE_SOURCE
#include AUDIO_AE_GAME_SOURCE

int main(int argc, char **argv) {
    if (argc != 6) return 2; /* root, engine|coast|grain|coast-grain|crash|wall|source, car|strength|bank, rpm|repeat|index, output.pcm */
    if (!strcmp(argv[2], "source")) {
        SfxBank b; char path[1024];
        snprintf(path,sizeof path,"%s/%s",argv[1],argv[3]);
        if (!sfx_bank_open_file(&b,path)) return 5;
        int k=atoi(argv[4]);SfxClip c;
        if (!sfx_clip_from_bank(&c,&b,k)) return 5;
        fprintf(stderr,"index=%d rate=%u frames=%u channels=%u codec=%u level=%u\n",
                k,c.rate,c.frames,b.samples[k].channels,b.samples[k].codec,b.samples[k].level);
        FILE *f=fopen(argv[5],"wb");if(!f)return 7;
        int ok=fwrite(c.pcm,2,c.frames,f)==c.frames && !fclose(f);
        sfx_clip_free(&c);sfx_bank_close(&b);return ok?0:8;
    }
    if (strcmp(argv[2],"engine") && strcmp(argv[2],"crash") && strcmp(argv[2],"grain") && strcmp(argv[2],"wall") && strcmp(argv[2],"coast") && strcmp(argv[2],"coast-grain")) return 2;
    if (SDL_Init(SDL_INIT_AUDIO)) return 3;
    SDL_AudioDeviceID dev = audio_init();
    if (!dev) return 4;
    SDL_PauseAudioDevice(dev, 1);
    /* The device starts before being paused; reset random/auxiliary state so
     * callback scheduling cannot affect an offline comparison. */
    g_rng=22222;memset(&g_aux,0,sizeof g_aux);
    int16_t pcm[1024];
    if (!strcmp(argv[2], "engine") || !strcmp(argv[2],"grain") || !strcmp(argv[2],"coast") || !strcmp(argv[2],"coast-grain")) {
        int sweeps = audio_load_ginsu_sweeps(argv[1], argv[3]);
        if (!sweeps && !audio_load_engine_bank(argv[1], audio_bank_for_car(argv[3]))) return 5;
        g_engine.current_rpm = g_engine.target_rpm = (float)atof(argv[4]);
        g_engine.current_load = g_engine.load = !strcmp(argv[2],"coast") ? 0 : 1;
        g_engine.master_volume = 1;
        fprintf(stderr, "car=%s bank=%d sweeps=%d\n", argv[3], audio_bank_for_car(argv[3]), sweeps);
        for (int i=0;i<3;i++) fprintf(stderr,"band=%d rate=%.0f frames=%u base_hz=%.3f\n",i,g_engine.accel_bands[i].srate,g_engine.accel_bands[i].sample_count,g_engine.accel_bands[i].base_freq);
        if(sweeps) {
            GinsuSweep *sw=!strcmp(argv[2],"coast-grain") ? &g_engine.gin.decel_sweep : &g_engine.gin.accel_sweep;
            fprintf(stderr,"sweep rate=%u rpm=%.2f..%.2f markers=%d grains=%d samples=%u\n",sw->sample_rate,sw->rpm_min,sw->rpm_max,sw->n_rpm,sw->n_grain,sw->total_samples);
        }
        if (!strcmp(argv[2],"grain") || !strcmp(argv[2],"coast-grain")) {
            GinsuSweep *sw=!strcmp(argv[2],"coast-grain") ? &g_engine.gin.decel_sweep : &g_engine.gin.accel_sweep;
            if(!sweeps)return 5;
            float pos=gin_pos_at(sw,g_engine.target_rpm);int j=0;
            while(j+1<sw->n_grain && sw->grain[j+1]<=pos)j++;
            uint32_t start=sw->grain[j],count=sw->grain[j+1]-start;
            fprintf(stderr,"grain=%d start=%u end=%u recorded_rpm=%.3f\n",j,start,start+count,gin_rpm_at(sw,start+count*.5f));
            FILE *f=fopen(argv[5],"wb");if(!f)return 7;
            int ok=fwrite(sw->samples+start,2,count,f)==count && !fclose(f);
            SDL_CloseAudioDevice(dev);SDL_Quit();return ok?0:8;
        }
        for (int k=0;k<44;k++) audio_cb(NULL,(Uint8 *)pcm,sizeof pcm);
    } else {
        g_sfx_game = sfg_create(argv[1],44100);
        if(!g_sfx_game || !(sfg_groups(g_sfx_game)&SFG_HAS_IMPACT)) return 6;
        SfxGame *g=g_sfx_game;
        fprintf(stderr,"impact classes=%d/%d/%d\n",g->nlight,g->nmedium,g->nheavy);
        float strength=!strcmp(argv[2],"wall") ? fminf(.45f,((float)atof(argv[3])-2.5f)*.025f) : (float)atof(argv[3]);
        strength=fminf(1,strength/.45f);
        const int *set=strength<.35f?g->light:strength<.7f?g->medium:g->heavy;
        int count=strength<.35f?g->nlight:strength<.7f?g->nmedium:g->nheavy;
        uint32_t seed=g->seed*1664525u+1013904223u;
        int k=set[(seed>>8)%(uint32_t)count];
        fprintf(stderr,"first_index=%d rate=%u frames=%u strength=%.6f\n",k,g->impact[k].rate,g->impact[k].frames,strength);
    }
    FILE *f=fopen(argv[5],"wb"); if(!f)return 7;
    for(int k=0;k<130;k++) {
        if(!strcmp(argv[2],"crash") && (k==0 || (atoi(argv[4]) && k%8==0))) g_hit=(float)atof(argv[3]);
        if(!strcmp(argv[2],"wall"))
            audio_wall_contact(k==0 || atoi(argv[4]) ? (float)atof(argv[3]) : 0,4,1024.0f/44100);
        audio_cb(NULL,(Uint8 *)pcm,sizeof pcm);
        if(fwrite(pcm,1,sizeof pcm,f)!=sizeof pcm)return 8;
    }
    fclose(f); SDL_CloseAudioDevice(dev);
    sfg_destroy(g_sfx_game);g_sfx_game=NULL;
    if(g_engine.gin.has_gin_data) {
        if(!g_engine.gin.decel_is_alias)free_sweep(&g_engine.gin.decel_sweep);
        free_sweep(&g_engine.gin.accel_sweep);
    }
    for(int b=0;b<3;b++){free_band(&g_engine.accel_bands[b]);free_band(&g_engine.decel_bands[b]);}
    free_band(&g_engine.idle_band);
    SDL_Quit(); return 0;
}
