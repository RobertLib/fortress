// Integration checks for authored progression, tactical combat and the actual
// native UI actions. Isolated FORTRESS_USER_DATA is supplied by CTest.
#define SDL_MAIN_HANDLED
#define main fortress_entry
#include "../src/main.c"
#undef main
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s (%s)\n",__FILE__,__LINE__,#x,SDL_GetError()); exit(1); } } while (0)

static Level fixture;
static Session run;
static Game *play;
static void fixture_map(void) {
  char source[4096];
  int used=snprintf(source,sizeof source,
    "@horror 1\n@terrain stone 1 1 18 9\n@monk 2 8 BROTHER TEST|The estate remembers.\n@haven 2 7 1\n"
    "@campaign 1\n@mechanism 9 2 winch 0 0.5 RELEASE THE WINCH|THE WINCH IS RELEASED\n"
    "@mechanism 11 2 valve 1 0 RESTORE THE PUMP|THE WATER DRAINS\n"
    "@mechanism 13 2 bell 3 0 RING THE BELL|THE ABBEY IS FREE\n"
    "@drain 9 4 3 2 2\n@relic 4 7 2\n"
    "@errand 3 8 0 GRAIN TALLY~Return the tally to me.~Thank you. These supplies are yours.\n");
  for (int y=0;y<11;y++) {
    for (int x=0;x<20;x++) {
      char c=x==0 || x==19 || y==0 || y==10?'1':'.';
      if (x==5 && y>0 && y<10) c=y==5?'D':'1';
      if (x==1 && y==1) c='P';
      if (x>=9 && x<=11 && y>=4 && y<=5) c='=';
      if (x==14 && y==7) c='G';
      if (x==16 && y==7) c='S';
      if (x==18 && y==7) c='O';
      source[used++]=c;
    }
    source[used++]='\n';
  }
  source[used]=0; char err[128];
  CHECK(level_parse(&fixture,source,err,sizeof err));
  fixture.decoration_count=1; // Keep the monk; remove automatic furniture.
  fixture.decorations[0]=(Decoration){2.5,8.5,.18,0,DECO_MONK};
}
static void reset_fixture(void) {
  game_destroy(play); run=(Session){.lives=3,.next_life=40000};
  play=game_create(&fixture,0,&run,NULL);
  for (int i=0;i<play->enemy_count;i++) { play->enemies[i].state=ES_DEAD; play->enemies[i].hp=0; }
  play->horror.calm_timer=10000;
  preferences_get()->tutorials=false;
}
static void position(Game *g,float x,float y,float dx,float dy) {
  float length=hypotf(dx,dy); g->player.x=x; g->player.y=y;
  g->player.dir_x=dx/length; g->player.dir_y=dy/length;
  g->player.plane_x=-g->player.dir_y*.66f; g->player.plane_y=g->player.dir_x*.66f;
}
static void ticks(Game *g,int count) { Input idle={.weapon=-1}; for (int i=0;i<count;i++) game_update(g,.01f,&idle); }
static void use_mechanism(int index) {
  const Mechanism *m=&fixture.campaign.stations[index];
  position(play,m->x-.5f,m->y+.5f,1,0);
  Input use={.weapon=-1,.use=true}; game_update(play,.01f,&use);
}
static void progression(void) {
  reset_fixture(); CHECK(!campaign_gate_ready(play));
  use_mechanism(1); CHECK(play->campaign.done==0); // Future steps cannot be skipped.
  use_mechanism(0); CHECK(play->campaign.active==0);
  ticks(play,15); CHECK(play->campaign.work>.1f && play->campaign.done==0);
  char *path=user_file("production-progress.dat"),err[128]; CHECK(path);
  CHECK(savegame_write(path,play,err,sizeof err));
  float saved_work=play->campaign.work;
  position(play,7.5,3.5,1,0); ticks(play,1);
  CHECK(play->campaign.active==-1 && play->campaign.done==0);
  CHECK(savegame_load(path,&fixture,1,&play,&run,err,sizeof err));
  CHECK(play->campaign.active==0 && play->campaign.work==saved_work);
  ticks(play,60); CHECK(play->campaign.done==1);
  CHECK(game_cell(play,9,4)=='=');
  use_mechanism(1); CHECK(play->campaign.done==3 && game_cell(play,9,4)=='.');
  use_mechanism(2); CHECK(play->campaign.done==7 && campaign_gate_ready(play));
  int score=run.score; use_mechanism(2); CHECK(run.score==score);
  play->horror.pending_hunts=0; play->horror.warden.state=WARDEN_DORMANT;
  position(play,4.5,7.5,1,0); ticks(play,1);
  CHECK(campaign_has_relic(play,RELIC_MERCY));
  position(play,3.5,8.5,-1,0); ticks(play,1);
  CHECK(play->campaign.errand_found && !play->campaign.errand_done);
  int monk=fixture.world.monks[0].note;
  campaign_converse(play,monk); CHECK(play->campaign.errand_done && play->campaign.decoys==4 && play->campaign.braces==2);
  score=run.score; int ammo=play->player.ammo;
  campaign_converse(play,monk); CHECK(run.score==score && play->player.ammo==ammo);
  CHECK(savegame_write(path,play,err,sizeof err));
  CHECK(savegame_load(path,&fixture,1,&play,&run,err,sizeof err));
  CHECK(play->campaign.errand_done && campaign_has_relic(play,RELIC_MERCY) && play->campaign.done==7);
  score=run.score; campaign_converse(play,monk); CHECK(run.score==score);
  play->campaign.done=2; CHECK(!savegame_write(path,play,err,sizeof err)); // Invalid prerequisites are rejected.
  CHECK(savegame_load(path,&fixture,1,&play,&run,err,sizeof err));
  CHECK(play->campaign.done==7); // Failed writes preserve the previous slot.
  CHECK(SDL_RemovePath(path)); free(path);
  puts("Mechanisms, interruption, drainage, mid-objective saves, relics and one-time errands passed.");
}
static void tactical_tools(void) {
  reset_fixture(); position(play,4.5,5.5,1,0);
  Door *door=game_door_at(play,5,5); CHECK(door);
  campaign_brace_door(play); CHECK(play->campaign.braces==0 && campaign_door_braced(play,(int)(door-play->doors)));
  game_open_door(play,door,false); CHECK(door->state==DOOR_CLOSED);
  char *path=user_file("production-tools.dat"),err[128];
  CHECK(savegame_write(path,play,err,sizeof err));
  CHECK(savegame_load(path,&fixture,1,&play,&run,err,sizeof err));
  door=game_door_at(play,5,5); CHECK(campaign_door_braced(play,(int)(door-play->doors)));
  position(play,3.5,5.5,1,0); campaign_throw_decoy(play);
  CHECK(play->campaign.decoys==1 && play->campaign.decoy_timer==7 && play->campaign.decoy_x<5);
  campaign_throw_decoy(play); CHECK(play->campaign.decoys==1);
  play->horror.warden=(Warden){.state=WARDEN_HUNT,.x=8.5,.y=5.5,.remaining=20,.lost_timer=0};
  ticks(play,1);
  CHECK(fabsf(play->horror.warden.target_x-play->campaign.decoy_x)<.01f);
  play->horror.warden.state=WARDEN_DORMANT;
  campaign_update(play,10.01f); CHECK(!campaign_door_braced(play,(int)(door-play->doors)));
  game_open_door(play,door,false); CHECK(door->state==DOOR_OPENING);
  CHECK(SDL_RemovePath(path)); free(path);
  puts("Door braces, expiry, save restoration and unseen-pursuer decoys passed.");
}
static void soldier_combat(void) {
  reset_fixture(); Enemy *guard=&play->enemies[0],*knight=&play->enemies[1],*captain=&play->enemies[2];
  knight->hp=100; knight->state=ES_CHASE; knight->face_x=-1; knight->face_y=0;
  position(play,14.5,7.5,1,0);
  enemy_hit_player(knight,play,20,WEAPON_CROSSBOW); CHECK(knight->hp==96);
  position(play,18.5,7.5,-1,0); enemy_hit_player(knight,play,20,WEAPON_CROSSBOW); CHECK(knight->hp==76);
  knight->state=ES_CHASE;
  enemy_hit_from(knight,play,20,WEAPON_CROSSBOW,14.5,7.5); CHECK(knight->hp==72);
  knight->state=ES_CHASE; knight->shield_pressure=0;
  position(play,14.5,7.5,1,0);
  enemy_hit_player(knight,play,12,WEAPON_ARBALEST); enemy_hit_player(knight,play,12,WEAPON_ARBALEST);
  CHECK(knight->state==ES_PAIN && !enemy_shield_raised(knight));
  int hp=knight->hp; enemy_hit_player(knight,play,12,WEAPON_ARBALEST); CHECK(knight->hp==hp-12);
  guard->state=ES_PATROL; guard->hp=25; guard->face_x=1; guard->face_y=0;
  captain->state=ES_STAND; captain->hp=45;
  position(play,13.5,7.5,1,0); enemy_hit_player(guard,play,14,WEAPON_DAGGER);
  CHECK(enemy_dead(guard) && captain->state==ES_STAND);
  knight->state=ES_DEAD; captain->state=ES_CHASE; captain->tactic_timer=0; captain->attack_cooldown=1;
  guard->hp=25; guard->state=ES_STAND;
  position(play,18.5,5.5,0,1); enemy_update(captain,play,.01f);
  CHECK(guard->state==ES_CHASE && captain->tactic_timer>8);
  captain->state=ES_DEAD; guard->state=ES_DEAD;
  knight->hp=60; knight->state=ES_CHASE; knight->attack_cooldown=0;
  position(play,16.5,5.5,0,1); enemy_update(knight,play,.01f); CHECK(knight->state==ES_AIM && knight->timer>.7f);
  position(play,13.5,5.5,1,0); int health=play->player.health;
  for (int i=0;i<80;i++) enemy_update(knight,play,.02f);
  CHECK(play->player.health==health); // The locked charge can be sidestepped.
  knight->x=16.5; knight->y=7.5; knight->state=ES_AIM; knight->timer=.01f;
  knight->dir_x=0; knight->dir_y=-1; knight->charge_hit=false;
  position(play,16.5,5.5,0,1);
  for (int i=0;i<30;i++) enemy_update(knight,play,.02f);
  CHECK(play->player.health==health-18); // One charge deals damage only once.
  reset_fixture(); guard=&play->enemies[0];
  guard->x=12.5; guard->y=3.5; guard->hp=25; guard->state=ES_AIM; guard->timer=.01f;
  position(play,15.5,3.5,-1,0); enemy_update(guard,play,.02f);
  CHECK(play->projectile_count==1 && play->projectiles[0].kind==SHOT_HOSTILE);
  guard->state=ES_DEAD; position(play,15.5,4.5,-1,0); ticks(play,70);
  CHECK(play->player.health==100); // Enemy arrows aim at the release position; they do not home.
  guard->state=ES_AIM; guard->timer=.01f; position(play,15.5,3.5,-1,0); enemy_update(guard,play,.02f);
  guard->state=ES_DEAD; ticks(play,40); CHECK(play->player.health<100);
  puts("Frontal shield, flanking, suppression, silent backstab, captain alarm and dodgeable charge passed.");
}
static void preferences_and_saves(void) {
  preferences_defaults(); Preferences *p=preferences_get();
  CHECK(preferences_bind(ACTION_FORWARD,SDL_SCANCODE_S));
  CHECK(p->keys[ACTION_FORWARD]==SDL_SCANCODE_S && p->keys[ACTION_BACK]==SDL_SCANCODE_W);
  CHECK(!preferences_bind(ACTION_FIRE,SDL_SCANCODE_F5));
  p->sensitivity=150; p->text=150; p->bob=0; p->shake=0;
  char *path=user_file("production-options.cfg"); CHECK(path && preferences_save(path));
  preferences_defaults(); CHECK(preferences_load(path));
  CHECK(p->sensitivity==150 && p->text==150 && p->bob==0 && p->keys[ACTION_FORWARD]==SDL_SCANCODE_S);
  CHECK(SDL_SaveFile(path,"malformed",9)); CHECK(!preferences_load(path)); CHECK(p->sensitivity==150);
  const char *overflow="FORTRESS OPTIONS 99999999999999999999999999999999999\n";
  CHECK(SDL_SaveFile(path,overflow,strlen(overflow))); CHECK(!preferences_load(path) && p->sensitivity==150);
  SDL_RemovePath(path); free(path); preferences_defaults();
  CHECK(load_levels()); new_game(0,DIFFICULTY_NORMAL); begin_level();
  CHECK(!session.arcade);
  for (int i=0;i<game->enemy_count;i++) { game->enemies[i].state=ES_DEAD; game->enemies[i].hp=0; }
  game->player.health=61; game->campaign.checkpoint_requested=true; autosave_game(.1f);
  SaveInfo info; CHECK(savegame_inspect(slot_path(3),levels,levels_loaded,&info) && info.health==61 && info.run_id==session.run_id);
  CHECK(safe_to_autosave());
  game->projectile_count=1;
  game->projectiles[0]=(Projectile){.kind=SHOT_TRAP,.x=game->player.x+1,.y=game->player.y};
  CHECK(!safe_to_autosave()); game->projectile_count=0;
  game->player.health=1; game->player_dead=true; restart_level();
  CHECK(state==ST_PLAYING && game->player.health==61 && !game->player_dead);
  int matching_run=session.run_id; session.run_id=matching_run==1?2:1;
  CHECK(!restore_checkpoint()); session.run_id=matching_run;
  set_state(ST_PAUSED); act_slots(); CHECK(state==ST_SLOTS && slot_save_mode);
  slot_row=1; activate_slot(); CHECK(selected_slot==1 && slot_valid[1]);
  game->player.health=30; slot_save_mode=false; activate_slot(); CHECK(state==ST_PLAYING && game->player.health==61);
  set_state(ST_PAUSED); act_options(); CHECK(state==ST_OPTIONS);
  options_page=0; options_row=0; p->brightness=100;
  handle_state_click(200,130); CHECK(p->brightness==100);
  handle_state_click(550,130); CHECK(p->brightness==110);
  handle_state_click(490,130); CHECK(p->brightness==100);
  on_gamepad_button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER); CHECK(options_page==1);
  on_gamepad_button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT); CHECK(p->effects==90);
  on_gamepad_button(SDL_GAMEPAD_BUTTON_START); CHECK(state==ST_PAUSED);
  act_options();
  options_page=3; options_row=1; adjust_option(1); CHECK(state==ST_BINDINGS);
  binding_row=ACTION_DECOY; binding_wait=true; on_key_down(SDL_SCANCODE_G);
  CHECK(!binding_wait && p->keys[ACTION_DECOY]==SDL_SCANCODE_G);
  handle_state_key(SDL_SCANCODE_ESCAPE); CHECK(state==ST_OPTIONS);
  handle_state_key(SDL_SCANCODE_ESCAPE); CHECK(state==ST_PAUSED);
  set_state(ST_PLAYING); game->campaign.journal=true; game->player.health=54;
  on_key_down(SDL_SCANCODE_F5); game->player.health=2; on_key_down(SDL_SCANCODE_F9);
  CHECK(state==ST_PLAYING && game->player.health==54);
  on_gamepad_button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT); CHECK(game->campaign.journal);
  int page=game->campaign.journal_page;
  on_gamepad_button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER); CHECK(game->campaign.journal_page!=page);
  on_gamepad_button(SDL_GAMEPAD_BUTTON_EAST); CHECK(!game->campaign.journal);
  set_state(ST_PAUSED); act_options();
  options_page=2; options_row=2; adjust_option(1); CHECK(p->arcade);
  new_game(0,DIFFICULTY_NORMAL); CHECK(session.arcade); begin_level();
  game->player.health=5; restart_level(); CHECK(state==ST_LEVEL_START && game->player.health==100);
  preferences_defaults();
  puts("Options persistence, malformed options, rebinding, manual slots, story checkpoints and arcade retries passed.");
}
static void authored_campaign(void) {
  char *path=user_file("production-chapter.dat"),err[128]; CHECK(path);
  for (int n=0;n<LEVEL_COUNT;n++) {
    new_game(n,DIFFICULTY_NORMAL); const CampaignLayout *c=&game->level->campaign;
    CHECK(c->enabled && c->count && c->errand.present && c->has_errand_door);
    game->horror.calm_timer=10000;
    for (int i=0;i<game->enemy_count;i++) { game->enemies[i].hp=0; game->enemies[i].state=ES_DEAD; }
    position(game,c->errand.x+.5f,c->errand.y+.5f,1,0); campaign_update(game,.01f);
    CHECK(game->campaign.errand_found && !game->campaign.errand_done);
    Door *reward=game_door_at(game,c->errand_door_x,c->errand_door_y); CHECK(reward && reward->state==DOOR_CLOSED);
    const Monk *monk=&game->level->world.monks[c->errand.monk];
    const int directions[][2]={{0,1},{1,0},{-1,0},{0,-1}}; bool spoke=false;
    for (int d=0;d<4;d++) {
      int dx=directions[d][0],dy=directions[d][1];
      position(game,monk->x+.5f+dx,monk->y+.5f+dy,-dx,-dy);
      if (!game_is_blocked(game,game->player.x,game->player.y,game->player.radius,NULL) && horror_near_note(game)==monk->note) {
        Input use={.weapon=-1,.use=true}; game_update(game,.01f,&use); spoke=true; break;
      }
    }
    CHECK(spoke && game->campaign.errand_done && reward->state==DOOR_OPENING);
    CHECK(game->secrets_found==1 && !strcmp(campaign_dialogue(game,monk->note),c->errand.thanks));
    int score=session.score; campaign_converse(game,monk->note); CHECK(score==session.score && game->secrets_found==1);
    CHECK(savegame_write(path,game,err,sizeof err));
    CHECK(savegame_load(path,levels,levels_loaded,&game,&session,err,sizeof err));
    CHECK(game->campaign.errand_done && game_door_at(game,c->errand_door_x,c->errand_door_y)->state==DOOR_OPENING);
    game->horror.reading=-1;
    if (n==LEVEL_COUNT-1) {
      int last=c->count-1; const Mechanism *m=&c->stations[last]; CHECK(m->kind==MECHANISM_OATH);
      game->campaign.done=(1u<<last)-1;
      position(game,m->x-.5f,m->y+.5f,1,0); CHECK(campaign_use(game));
      CHECK(!game->campaign.finale && !game->horror.pending_hunts);
      game->player.keys=3; CHECK(campaign_use(game));
      CHECK(game->campaign.finale && game->horror.pending_hunts==1 && campaign_gate_ready(game));
      CHECK(savegame_write(path,game,err,sizeof err));
      CHECK(savegame_load(path,levels,levels_loaded,&game,&session,err,sizeof err));
      CHECK(game->campaign.finale && game->horror.pending_hunts==1);
      game->horror.pending_hunts=0; game->horror.warden.state=WARDEN_DORMANT;
      campaign_update(game,.01f); CHECK(game->horror.pending_hunts==1);
      game->campaign.finale=false; game->campaign.done=(1u<<last)-1;
      game->horror.pending_hunts=0; game->horror.calm_timer=10000;
      set_state(ST_PLAYING); input.use=true; CHECK(safe_to_autosave()); update(.01f);
      CHECK(game->campaign.finale);
      Game *before=NULL; Session preparation={0};
      CHECK(savegame_load(slot_path(4),levels,levels_loaded,&before,&preparation,err,sizeof err));
      CHECK(!before->campaign.finale && before->campaign.done==(1u<<last)-1 && before->player.keys==3);
      CHECK(preparation.run_id==session.run_id); game_destroy(before);
    }
  }
  CHECK(SDL_RemovePath(path)); free(path);
  reset_fixture(); position(play,2.5,3.5,0,1); run.relics=1u<<RELIC_QUIET;
  play->player.bob_phase=(float)M_PI-.01f;
  Input footstep={.weapon=-1,.forward=true,.run=true}; game_update(play,.02f,&footstep);
  CHECK(play->horror.noise_radius==4.5f && play->horror.noise_timer>0);
  puts("All five authored errand doors, real monk conversations, one-time rewards, final bell prerequisites and quiet footsteps passed.");
}
static void save_frame(const char *name) {
  SDL_Surface *surface=SDL_CreateSurfaceFrom(W,H,SDL_PIXELFORMAT_ARGB8888,fb->px,W*4);
  CHECK(surface && SDL_SaveBMP(surface,name)); SDL_DestroySurface(surface);
}
static void screenshots(void) {
  CHECK(assets_build(&assets) && comic_art_build(&comic_art)); renderer_init(&renderer,&assets); fb=image_new(W,H);
  preferences_get()->text=150; options_page=0; options_row=3; set_state(ST_OPTIONS); render(); save_frame("build/production-options.bmp");
  set_state(ST_BINDINGS); binding_row=ACTION_DECOY; render(); save_frame("build/production-bindings.bmp");
  refresh_save_slots(); set_state(ST_SLOTS); slot_save_mode=false; render(); save_frame("build/production-slots.bmp");
  new_game(1,DIFFICULTY_NORMAL); set_state(ST_PLAYING); game->campaign.journal=true;
  render(); save_frame("build/production-journal.bmp"); game->campaign.journal=false;
  game->horror.reading=game->level->world.monks[0].note;
  render(); save_frame("build/production-large-dialogue.bmp");
  new_game(0,DIFFICULTY_NORMAL); set_state(ST_PLAYING);
  const Mechanism *m=&game->level->campaign.stations[0];
  position(game,m->x-.7f,m->y+.5f,1,0); render(); save_frame("build/production-winch.bmp");
  const float outdoors[5][4]={{42.5f,20.5f,1,0},{56.5f,24.5f,0,-1},{55.5f,20.5f,1,0},{53.5f,13.5f,0,-1},{57.5f,16.5f,0,-1}};
  for (int chapter=0;chapter<LEVEL_COUNT;chapter++) for (int view=0;view<2;view++) {
    new_game(chapter,DIFFICULTY_NORMAL); set_state(ST_PLAYING);
    if (view) {
      const float *p=outdoors[chapter]; position(game,p[0],p[1],p[2],p[3]);
    }
    game->time=3; render();
    char filename[80]; snprintf(filename,sizeof filename,"build/production-chapter-%d-%s.bmp",chapter+1,view?"exterior":"interior"); save_frame(filename);
    Uint64 start=SDL_GetPerformanceCounter();
    for (int frame=0;frame<40;frame++) render();
    double ms=(double)(SDL_GetPerformanceCounter()-start)*1000/SDL_GetPerformanceFrequency()/40;
    printf("Chapter %d %s: %.3f ms per 640x400 frame (40-frame mean, rendering only).\n",chapter+1,view?"exterior":"interior",ms);
  }
  image_free(fb); fb=NULL; renderer_free(&renderer); assets_free(&assets); comic_art_free(&comic_art);
}
int main(int argc,char **argv) {
  CHECK(SDL_Init(0)); fixture_map(); progression(); tactical_tools(); soldier_combat(); preferences_and_saves(); authored_campaign();
  if (argc>1 && !strcmp(argv[1],"--screenshots")) screenshots();
  game_destroy(play); level_free(&fixture); game_destroy(game);
  for (int i=0;i<levels_loaded;i++) level_free(&levels[i]);
  free(save_path); for (int i=0;i<2;i++) free(manual_paths[i]); free(automatic_path); free(checkpoint_path);
  SDL_Quit(); return 0;
}
