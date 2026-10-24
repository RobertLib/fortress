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

static void solid_architecture(const Assets *assets, bool shots) {
  Level lv; parse(&lv, COURT);
  lv.decoration_count=lv.wall_box_count=lv.hanging_count=lv.torch_count=lv.fire_light_count=0;
  Session session={.lives=3,.next_life=40000}; Game *g=game_create(&lv,0,&session,NULL);
  Renderer r; renderer_init(&r,assets); Image *fb=image_new(SCREEN_W,SCREEN_H);
  // From a raised path, a low wall has a stone cap, with ground and taller
  // masonry still visible beyond it. The first wall cannot end the column.
  lv.has_heights=true; lv.heights[5*lv.w+3]=1.5f;
  lv.grid[5*lv.w+5]='1'; lv.world.cells[5*lv.w+5].outside=0;
  lv.ceilings[5*lv.w+5]=1;
  lv.grid[5*lv.w+10]='1'; lv.world.cells[5*lv.w+10].outside=0;
  lv.ceilings[5*lv.w+10]=2.5f;
  aim(g,3.5f,5.5f,1,0); render_view(&r,fb,g);
  int center=SCREEN_W/2;
  CHECK(fabsf(r.depth[300*SCREEN_W+center]-(2-1.125f)*VIEW_H/140.5f)<.01f);
  CHECK(!r.floor_mask[300*SCREEN_W+center]);
  CHECK(fabsf(r.depth[270*SCREEN_W+center]-2*VIEW_H/110.5f)<.01f);
  CHECK(r.floor_mask[270*SCREEN_W+center]);
  CHECK(fabsf(r.depth[140*SCREEN_W+center]-6.5f)<.01f);
  if(shots) screenshot(fb,"build/world-wall-cap.bmp");
  // Furniture beyond the low wall is clipped per pixel, too.
  lv.wall_boxes[0]=(WallBox){10,5,-1,0,BOX_WARDROBE}; lv.wall_box_count=1;
  render_view(&r,fb,g);
  CHECK(r.depth[240*SCREEN_W+center]>6 && r.depth[240*SCREEN_W+center]<6.5f);
  lv.wall_box_count=0;
  // A covered room behind that wall also has a top, rather than an open
  // box that reveals the floor when viewed from above.
  lv.world.cells[5*lv.w+6].outside=0; lv.ceilings[5*lv.w+6]=1;
  render_view(&r,fb,g);
  CHECK(fabsf(r.depth[260*SCREEN_W+center]-(2-1.125f)*VIEW_H/100.5f)<.01f);
  CHECK(!r.floor_mask[260*SCREEN_W+center]);
  image_free(fb); renderer_free(&r); game_destroy(g); level_free(&lv);

  parse(&lv,
    "@outside 1 1 5 9\n@ceiling 6 0 7 11 2.5\n@ceiling 6 5 1 1 1\n"
    "1111111111111\n1.....1.....1\n1.....1.....1\n1.....1.....1\n1.....1.....1\n"
    "1P....D.....1\n1.....1.....1\n1.....1.....1\n1.....1.....1\n1.....1.....1\n1111111111111\n");
  lv.decoration_count=lv.wall_box_count=lv.hanging_count=lv.torch_count=lv.fire_light_count=0;
  g=game_create(&lv,0,&session,NULL); renderer_init(&r,assets); fb=image_new(SCREEN_W,SCREEN_H);
  // Both faces of the header sit flush with the wall, whether its leaf is
  // closed, partly open or fully retracted. A clear sky remains above it.
  for(int side=0;side<2;side++) for(int state=0;state<3;state++) {
    aim(g,side?9.5f:3.5f,5.5f,side?-1:1,0);
    g->doors[0].open=state*.5f; render_view(&r,fb,g);
    CHECK(fabsf(r.depth[40*SCREEN_W+center]-2.5f)<.01f);
    if(!side && state==2) CHECK(r.depth[160*SCREEN_W+center]>3);
    if(shots && !side && state==2) screenshot(fb,"build/world-door-header.bmp");
  }
  aim(g,1.5f,5.5f,1,0); render_view(&r,fb,g);
  CHECK(isinf(r.depth[center]));
  CHECK(fabsf(r.depth[40*SCREEN_W+center]-4.5f)<.01f);
  image_free(fb); renderer_free(&r); game_destroy(g); level_free(&lv);
}

static void distant_wall_lighting(const Assets *assets, bool shots) {
  // A flat material isolates lighting from the painted mortar. A continuous
  // facade must not alternate indoor/outdoor fog between adjacent columns.
  Assets flat=*assets;
  flat.walls[WT_STONE]=image_new(TEX,TEX);
  flat.walls_dark[WT_STONE]=image_new(TEX,TEX);
  image_fill(flat.walls[WT_STONE],0xffb4b4b4u);
  image_fill(flat.walls_dark[WT_STONE],0xff7e7e7eu);
  char text[2048];
  int len=snprintf(text,sizeof text,"@horror 1\n@outside 1 1 29 27\n@ceiling 0 6 31 1 2.5\n@ceiling 15 6 1 1 1\n");
  for(int y=0;y<29;y++) {
    for(int x=0;x<31;x++)
      text[len++]=y==6?(x==15?'D':'1'):x==0||y==0||x==30||y==28?'1':x==10&&y==24?'P':'.';
    text[len++]='\n';
  }
  text[len]=0;
  Level lv; parse(&lv,text);
  for(int x=0;x<lv.w;x++) lv.world.cells[6*lv.w+x].outside=0;
  lv.decoration_count=lv.wall_box_count=lv.hanging_count=lv.torch_count=lv.fire_light_count=0;
  Session session={.lives=3,.next_life=40000}; Game *g=game_create(&lv,0,&session,NULL);
  Renderer r; renderer_init(&r,&flat); Image *fb=image_new(SCREEN_W,SCREEN_H);
  const float directions[][2]={{.25f,-.9682458f},{-.25f,-.9682458f},{.6f,-.8f}};
  for(int view=0;view<3;view++) for(int state=0;state<3;state++) {
    float dx=directions[view][0],dy=directions[view][1];
    aim(g,15.5f-dx*18,7-dy*18,dx,dy);
    g->doors[0].open=state*.5f; render_view(&r,fb,g);
    const int spans[][2]={{220,290},{315,325},{350,420}};
    for(int span=0;span<3;span++) for(int x=spans[span][0];x<spans[span][1];x++) {
      int at=138*SCREEN_W+x;
      int a=(fb->px[at]>>16)&255,b=(fb->px[at+1]>>16)&255;
      CHECK(abs(a-b)<=3);
      // The thin roof edge must not fight with the wall on the same plane.
      int edge[2];
      for(int n=0;n<2;n++) {
        float camera=2.0f*(x+n)/SCREEN_W-1;
        float dist=(7-g->player.y)/(dy+g->player.plane_y*camera);
        int y=(int)floorf(VIEW_H/2.0f+VIEW_H*(r.eye_z-2.56f)/dist);
        edge[n]=(fb->px[y*SCREEN_W+x+n]>>16)&255;
      }
      CHECK(abs(edge[0]-edge[1])<=3);
    }
    if(shots && state==2) {
      char path[80]; snprintf(path,sizeof path,"build/world-distant-lighting-%d.bmp",view);
      screenshot(fb,path);
    }
  }
  image_free(fb); renderer_free(&r); game_destroy(g); level_free(&lv);
  image_free(flat.walls[WT_STONE]); image_free(flat.walls_dark[WT_STONE]);
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
    if(n==0) {
      // The inn's exterior and internal doors have one-storey openings in
      // a taller timber building. Check both sides of both actual headers.
      const float views[][3]={{57.5f,14.5f,-1},{57.5f,9.5f,1},{57.5f,10.5f,-1},{57.5f,6.5f,1}};
      for(int v=0;v<4;v++) for(int state=0;state<3;state++) {
        for(int i=0;i<g->door_count;i++) if(g->doors[i].kind!='Z') g->doors[i].open=state*.5f;
        aim(g,views[v][0],views[v][1],0,views[v][2]); render_view(&r,fb,g);
        CHECK(fabsf(r.depth[40*SCREEN_W+SCREEN_W/2]-(v?1.5f:2.5f))<.01f);
      }
      if(shots) {
        aim(g,57.5f,18.5f,0,-1); render_view(&r,fb,g); render_hud(&r,fb,g);
        screenshot(fb,"build/world-inn-facade.bmp");
      }
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
  bool shots=argc>1&&!strcmp(argv[1],"--screenshots");
  validation(); rendering(&assets); solid_architecture(&assets,shots);
  distant_wall_lighting(&assets,shots); campaign(&assets,shots);
  assets_free(&assets); SDL_Quit(); puts("Outdoor terrain, roofs, peaceful monks, dialogue and save compatibility passed.");
  return 0;
}
