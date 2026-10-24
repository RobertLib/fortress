// Exercise the real synthesizers and callback without opening a sound device.
#include "../src/audio.c"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s (%s)\n",__FILE__,__LINE__,#x,SDL_GetError()); exit(1); } } while (0)

static double energy(const float *data,int count) {
  double sum=0;
  for (int i=0;i<count;i++) { CHECK(isfinite(data[i]) && fabsf(data[i])<=4); sum+=data[i]*data[i]; }
  return sum/count;
}
static double callback_energy(SDL_AudioStream *sink,int frames) {
  int bytes=frames*FRAME_BYTES;
  float *samples=malloc((size_t)bytes); CHECK(samples);
  mix_audio(NULL,sink,bytes,0);
  CHECK(SDL_GetAudioStreamData(sink,samples,bytes)==bytes);
  double result=energy(samples,frames*2); free(samples); return result;
}
int main(void) {
  CHECK(SDL_Init(0)); random_seed(28); synthesize_all();
  for (int i=0;i<SND_COUNT;i++) {
    CHECK(SYNTHS[i].fn && buffers[i] && lengths[i]>0);
    CHECK(energy(buffers[i],lengths[i])>1e-8);
  }
  uint64_t rng=random_state(); prepare_music(); CHECK(random_state()==rng);
  for (int chapter=0;chapter<LEVEL_COUNT;chapter++) {
    for (int stem=0;stem<2;stem++) {
      float *data=music_stems[chapter][stem]; CHECK(data);
      CHECK(energy(data,MUSIC_LENGTH)>1e-5);
      CHECK(fabsf(data[0])<1e-5 && fabsf(data[MUSIC_LENGTH-1])<1e-4);
    }
    CHECK(memcmp(music_stems[chapter][0],music_stems[chapter][1],MUSIC_LENGTH*sizeof(float)));
    if (chapter) CHECK(memcmp(music_stems[0][0],music_stems[chapter][0],MUSIC_LENGTH*sizeof(float)));
  }
  SDL_AudioSpec spec={SDL_AUDIO_F32,2,SAMPLE_RATE};
  SDL_AudioStream *sink=SDL_CreateAudioStream(&spec,&spec); CHECK(sink);
  audio_set_scene(2,.8f,false,true); audio_set_mix(85,100);
  CHECK(callback_energy(sink,SAMPLE_RATE*2)>1e-5 && music_gain>.5f && music_blend>.75f);
  music_position=MUSIC_LENGTH-8; CHECK(callback_energy(sink,100)>0 && music_position<MUSIC_LENGTH);
  audio_set_enabled(false); CHECK(callback_energy(sink,512)==0);
  audio_set_enabled(true); audio_set_mix(85,0); CHECK(callback_energy(sink,512)==0);
  voices[0]=(Voice){.data=buffers[SND_METAL_HIT],.len=lengths[SND_METAL_HIT],.rate=1,.gain=1,.left=1,.right=.25f};
  CHECK(callback_energy(sink,512)>1e-5);
  CHECK(callback_energy(sink,SAMPLE_RATE)>0 && !voices[0].data);
  audio_set_mix(85,100); audio_set_scene(2,0,true,true);
  callback_energy(sink,SAMPLE_RATE*10); CHECK(music_gain<.001f && music_blend<.001f);
  SDL_DestroyAudioStream(sink); audio_shutdown(); SDL_Quit();
  printf("%d sound effects, five distinct two-stem scores, finite samples, loop seams, callback, mute, gains and shelter fades passed.\n",SND_COUNT);
  return 0;
}
