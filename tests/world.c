#include "render.h"
#include "savegame.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); exit(1); } } while (0)
static const char *COURT =
  "@horror 1\n@outside 1 1 11 9\n@terrain grass 1 1 11 9\n"
  "1111111111111\n1...........1\n1...........1\n1...........1\n1...........1\n"
  "1P..........1\n1...........1\n1...........1\n1...........1\n1...........1\n1111111111111\n";

static void parse(Level *lv, const char *text) {
  char err[160]; bool ok = level_parse(lv, text, err, sizeof err);
  if (!ok) fprintf(stderr, "%s\n", err);
  CHECK(ok);
}
static void aim(Game *g, float x, float y, float dx, float dy) {
  g->player.x=x; g->player.y=y; g->player.dir_x=dx; g->player.dir_y=dy;
  g->player.plane_x=-dy*.66f; g->player.plane_y=dx*.66f;
}
static void screenshot(const Image *fb, const char *name) {
  SDL_Surface *s=SDL_CreateSurfaceFrom(fb->w,fb->h,SDL_PIXELFORMAT_ARGB8888,fb->px,fb->w*4);
  CHECK(s && SDL_SaveBMP(s,name)); SDL_DestroySurface(s);
}

static void validation(void) {
  const char *bad[] = {
    "@outside", "@outside -1 1 2 2", "@outside 1 1 99 3", "@outside 1 1 2 2 extra",
    "@outside 1 1 0 3", "@terrain mud 1 1 3 3", "@terrain grass 1 1 3",
    "@prop dragon 3 3", "@prop oak 0 0", "@prop oak 1 5", "@prop well 3 3 extra",
    "@prop oak 3 3\n@prop grave 3 3", "@monk 3 3 BROTHER", "@monk 3 3 |Hello",
    "@monk 3 3 BROTHER|", "@landmark 3 3 nan GARDEN", "@landmark 3 3 0 GARDEN",
    "@landmark 0 0 3 GARDEN", "@objective", "@destination",
  };
  char text[2048],err[160];
  for (size_t i=0;i<sizeof bad/sizeof bad[0];i++) {
    snprintf(text,sizeof text,"%s%s\n",COURT,bad[i]);
    Level lv; CHECK(!level_parse(&lv,text,err,sizeof err)); CHECK(strstr(err,"world directive")); level_free(&lv);
  }
  Level lv; parse(&lv,"11111\n1P..1\n11111\n");
  CHECK(!lv.world.cells && !world_outside(&lv,1,1) && world_ground(&lv,1,1)==GROUND_STONE);
  level_free(&lv);
  snprintf(text,sizeof text,"%s@prop well 4 5\n@monk 8 5 BROTHER ANSELM|The gate stands beyond the garden.\n@landmark 6 5 4 CLOISTER\n",COURT);
  parse(&lv,text);
  CHECK(!world_outside(&lv,-1,0) && !world_outside(&lv,lv.w,lv.h));
  CHECK(lv.has_ceilings && lv.world.prop_count==2 && lv.world.monk_count==1 && lv.horror.note_count==1);
  CHECK(world_landmark(&lv,6.5f,5.5f) && !world_landmark(&lv,1.5f,1.5f));
  Session session={.lives=3,.next_life=40000}; Game *g=game_create(&lv,0,&session,NULL);
  CHECK(game_is_blocked(g,4.5f,5.5f,g->player.radius,NULL));
  CHECK(!game_is_blocked(g,4.5f,4.5f,g->player.radius,NULL));
  // A monk is a peaceful actor, not an enemy or a kill/score target.
  CHECK(g->enemy_count==0 && g->total_enemies==0);
  aim(g,7.5f,5.5f,1,0);
  Input use={.weapon=-1,.use=true}; game_update(g,.01f,&use);
  CHECK(g->horror.reading==0 && g->horror.notes_read==1 && world_monk_note(&lv,0));
  float time=g->time; g->player.venom=2; g->player.venom_timer=.01f;
  Input idle={.weapon=-1}; game_update(g,2,&idle);
  CHECK(g->time==time && g->player.venom==2);
  char path[1024]; snprintf(path,sizeof path,"%stest-world-save.dat",SDL_GetBasePath());
  CHECK(savegame_write(path,g,err,sizeof err));
  CHECK(savegame_load(path,&lv,1,&g,&session,err,sizeof err));
  CHECK(g->horror.reading==0 && g->horror.notes_read==1);
  WorldCell original=lv.world.cells[3*lv.w+3];
  lv.world.cells[3*lv.w+3].outside=0;
  Game *before=g; CHECK(!savegame_load(path,&lv,1,&g,&session,err,sizeof err) && g==before);
  lv.world.cells[3*lv.w+3]=original;
  lv.world.cells[3*lv.w+3].ground=GROUND_GRAVEL;
  CHECK(!savegame_load(path,&lv,1,&g,&session,err,sizeof err) && g==before);
  lv.world.cells[3*lv.w+3]=original;
  lv.world.props[0].x++;
  CHECK(!savegame_load(path,&lv,1,&g,&session,err,sizeof err) && g==before);
  lv.world.props[0].x--;
  CHECK(savegame_load(path,&lv,1,&g,&session,err,sizeof err)); CHECK(SDL_RemovePath(path));
  use.use=true; game_update(g,.01f,&use); CHECK(g->horror.reading==-1 && !use.use);
  // Masonry blocks conversations, even within the normal interaction radius.
  aim(g,7.65f,4.7f,.728f,.685f); lv.grid[5*lv.w+7]='1'; CHECK(horror_near_note(g)<0);
  game_destroy(g); level_free(&lv);
}

static void rendering(const Assets *assets) {
  Level lv; parse(&lv,COURT);
  // Remove automatic interior dressing to isolate the sky/roof test.
  lv.decoration_count=lv.wall_box_count=lv.hanging_count=lv.torch_count=lv.fire_light_count=0;
  Session session={.lives=3,.next_life=40000}; Game *g=game_create(&lv,0,&session,NULL);
  Renderer r; renderer_init(&r,assets); Image *fb=image_new(SCREEN_W,SCREEN_H);
  aim(g,3.5f,5.5f,1,0); render_view(&r,fb,g);
  int sky=0; for (int y=0;y<80;y++) for(int x=0;x<SCREEN_W;x++) sky+=isinf(r.depth[y*SCREEN_W+x]);
  CHECK(sky>30000); // An exterior has genuinely unoccluded sky pixels.
  int at=270*SCREEN_W+SCREEN_W/2; uint32_t grass=fb->px[at];
  CHECK(r.floor_mask[at] && isfinite(r.depth[at]));
  for(int i=0;i<lv.w*lv.h;i++) lv.world.cells[i].ground=GROUND_GRAVEL;
  render_view(&r,fb,g); CHECK(fb->px[at]!=grass);
  for(int i=0;i<lv.w*lv.h;i++) lv.world.cells[i].outside=0;
  render_view(&r,fb,g);
  CHECK(isfinite(r.depth[20*SCREEN_W+SCREEN_W/2])); // A real roof occludes that same sky.
  // Looking from a covered porch out into a court still exposes the sky
  // beyond the lintel; looking back retains the porch's roof.
  for(int y=1;y<lv.h-1;y++) for(int x=6;x<lv.w-1;x++) lv.world.cells[y*lv.w+x].outside=1;
  aim(g,5.5f,5.5f,1,0); render_view(&r,fb,g);
  CHECK(isinf(r.depth[50*SCREEN_W+SCREEN_W/2]));
  aim(g,6.5f,5.5f,-1,0); render_view(&r,fb,g);
  CHECK(isfinite(r.depth[50*SCREEN_W+SCREEN_W/2]));
  image_free(fb); renderer_free(&r); game_destroy(g); level_free(&lv);
}

static void campaign(const Assets *assets,bool shots) {
  const int offsets[]={39,41,43,41,45};
  const float views[][2][4]={
    {{3,20,1,0},{18,10,0,-1}}, {{15,19,0,-1},{15,12,0,-1}},
    {{12,20,1,0},{6,14,.8f,.6f}}, {{12,13,0,-1},{12,26,1,0}},
    {{12,16,0,-1},{19,10,0,-1}},
  };
  for(int n=0;n<LEVEL_COUNT;n++) {
    char path[128],err[160]; snprintf(path,sizeof path,"levels/%d.txt",n+1);
    char *text=SDL_LoadFile(path,NULL); CHECK(text); Level lv;
    CHECK(level_parse(&lv,text,err,sizeof err)); SDL_free(text);
    int outside=0,external_keys=0,kinds[8]={0},portals=0;
    for(int y=0;y<lv.h;y++) for(int x=0;x<lv.w;x++) outside+=world_outside(&lv,x,y)&&level_is_open(lv.grid[y*lv.w+x]);
    for(int i=0;i<lv.item_count;i++) external_keys+=lv.items[i].kind==ITEM_KEY&&lv.items[i].x>offsets[n];
    for(int i=0;i<lv.world.prop_count;i++) if(lv.world.props[i].kind>=DECO_OAK) kinds[lv.world.props[i].kind-DECO_OAK]++;
    CHECK(outside>250 && external_keys==1 && kinds[DECO_WELL-DECO_OAK]==1 && kinds[DECO_MONK-DECO_OAK]>=1);
    // Each district has two distinct open crossings of the old perimeter.
    for(int y=1;y<lv.h-1;y++) portals+=level_is_open(lv.grid[y*lv.w+offsets[n]-1])&&world_outside(&lv,offsets[n],y);
    CHECK(portals==2);
    Session session={.lives=3,.next_life=40000}; Game *g=game_create(&lv,n,&session,NULL);
    g->enemy_count=0;
    Renderer r; renderer_init(&r,assets); Image *fb=image_new(SCREEN_W,SCREEN_H);
    for(int v=0;v<2;v++) {
      const float *p=views[n][v]; aim(g,offsets[n]+p[0]+.5f,p[1]+.5f,p[2],p[3]);
      CHECK(!game_is_blocked(g,g->player.x,g->player.y,g->player.radius,NULL));
      for(int i=0;i<g->door_count;i++) if(g->doors[i].kind!='Z') g->doors[i].open=1;
      render_view(&r,fb,g); render_hud(&r,fb,g);
      if(shots) { snprintf(path,sizeof path,"build/world-%d-%d.bmp",n+1,v+1); screenshot(fb,path); }
    }
    for (int i=0;i<lv.world.monk_count;i++) {
      const Monk *m=&lv.world.monks[i];
      const int directions[][2]={{0,1},{-1,0},{1,0},{0,-1}};
      bool approach=false;
      for (int d=0;d<4;d++) {
        int dx=directions[d][0],dy=directions[d][1];
        aim(g,m->x+.5f+dx,m->y+.5f+dy,-dx,-dy);
        if(!game_is_blocked(g,g->player.x,g->player.y,g->player.radius,NULL) && horror_near_note(g)==m->note) {approach=true; break;}
      }
      CHECK(approach); g->horror.reading=-1;
      render_view(&r,fb,g); render_hud(&r,fb,g);
      if(shots&&!i) {snprintf(path,sizeof path,"build/world-%d-monk.bmp",n+1); screenshot(fb,path);}
      Input use={.weapon=-1,.use=true}; game_update(g,.01f,&use);
      CHECK(g->horror.reading==m->note && (g->horror.notes_read&(1u<<m->note)));
      render_view(&r,fb,g); render_hud(&r,fb,g);
      if(shots&&!i) {snprintf(path,sizeof path,"build/world-%d-dialogue.bmp",n+1); screenshot(fb,path);}
    }
    printf("Chapter %d: %d outdoor cells, two district entrances, mandatory key, %d props, %d monks\n",n+1,outside,lv.world.prop_count,lv.world.monk_count);
    image_free(fb); renderer_free(&r); game_destroy(g); level_free(&lv);
  }
}
int main(int argc,char **argv) {
  CHECK(SDL_Init(0)); random_seed(9187);
  Assets assets; CHECK(assets_build(&assets));
  validation(); rendering(&assets); campaign(&assets,argc>1&&!strcmp(argv[1],"--screenshots"));
  assets_free(&assets); SDL_Quit(); puts("Outdoor terrain, roofs, peaceful monks, dialogue and save compatibility passed.");
  return 0;
}
