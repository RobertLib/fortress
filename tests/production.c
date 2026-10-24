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
  // A nearly shut leaf may already be opening or still closing. Neither
  // animation may consume a brace and promise protection it cannot provide.
  game_open_door(play,door,false);
  campaign_brace_door(play);
  CHECK(play->campaign.braces==1 && !campaign_door_braced(play,(int)(door-play->doors)));
  ticks(play,2); CHECK(door->open>0 && door->open<.05f);
  campaign_brace_door(play); CHECK(play->campaign.braces==1);
  ticks(play,70); CHECK(door->state==DOOR_OPEN && door->open==1);
  for (int i=0;i<600 && !(door->state==DOOR_CLOSING && door->open<=.05f);i++) ticks(play,1);
  CHECK(door->state==DOOR_CLOSING && door->open>0 && door->open<=.05f);
  campaign_brace_door(play); CHECK(play->campaign.braces==1);
  ticks(play,10); CHECK(door->state==DOOR_CLOSED);
  campaign_brace_door(play); CHECK(play->campaign.braces==0 && campaign_door_braced(play,(int)(door-play->doors)));
  game_open_door(play,door,false); CHECK(door->state==DOOR_CLOSED);
  char *path=user_file("production-tools.dat"),err[128];
  CHECK(savegame_write(path,play,err,sizeof err));
  CHECK(savegame_load(path,&fixture,1,&play,&run,err,sizeof err));
  door=game_door_at(play,5,5); CHECK(campaign_door_braced(play,(int)(door-play->doors)));
  position(play,3.5,5.5,1,0); campaign_throw_decoy(play);
  CHECK(play->campaign.decoys==1 && play->campaign.decoy_timer==7 && play->campaign.decoy_x<5);
  campaign_throw_decoy(play); CHECK(play->campaign.decoys==1);
  // A soldier returning after a lost chase must investigate a fresh sound.
  Enemy *guard=&play->enemies[0];
  guard->hp=25; guard->state=ES_RETURN; guard->lost_timer=5.1f; guard->x=8.5f; guard->y=5.5f;
  CHECK(!game_line_of_sight(play,guard->x,guard->y,play->player.x,play->player.y));
  campaign_update(play,.01f);
  CHECK(guard->state==ES_CHASE && guard->lost_timer==0);
  enemy_update(guard,play,.01f);
  CHECK(guard->state==ES_CHASE && guard->x<8.5f);
  CHECK(guard->last_seen_x==play->campaign.decoy_x && guard->last_seen_y==play->campaign.decoy_y);
  guard->state=ES_DEAD; guard->hp=0;
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
  p->sensitivity=150; p->text=150; p->bob=0; p->shake=0; p->sound=false; p->fullscreen=true;
  char *path=user_file("production-options.cfg"); CHECK(path && preferences_save(path));
  preferences_defaults(); CHECK(preferences_load(path));
  CHECK(p->sensitivity==150 && p->text==150 && p->bob==0 && p->keys[ACTION_FORWARD]==SDL_SCANCODE_S);
  CHECK(!p->sound && p->fullscreen);
  CHECK(SDL_SaveFile(path,"malformed",9)); CHECK(!preferences_load(path)); CHECK(p->sensitivity==150);
  const char *overflow="FORTRESS OPTIONS 99999999999999999999999999999999999\n";
  CHECK(SDL_SaveFile(path,overflow,strlen(overflow))); CHECK(!preferences_load(path) && p->sensitivity==150);
  // Existing version-one options keep their bindings and receive the new defaults.
  const char *legacy="FORTRESS OPTIONS 1\n125 100 100 100 125 80 40\n1 1 0\n26 22 4 7 13 8 225 43 12 20 5\n";
  CHECK(SDL_SaveFile(path,legacy,strlen(legacy)) && preferences_load(path));
  CHECK(p->sound && !p->fullscreen && p->sensitivity==125 && p->keys[ACTION_USE]==SDL_SCANCODE_E);
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
  set_state(ST_PAUSED); act_save_game(); CHECK(state==ST_SLOTS && slot_save_mode);
  slot_row=1; activate_slot(); CHECK(state==ST_PAUSED && selected_slot==1 && slot_valid[1]);
  game->player.health=30; act_load_game(); activate_slot(); CHECK(state==ST_PLAYING && game->player.health==61);
  set_state(ST_PAUSED); act_options(); CHECK(state==ST_OPTIONS);
  options_page=0; options_row=1; p->brightness=100;
  handle_state_click(200,166); CHECK(p->brightness==100);
  handle_state_click(550,166); CHECK(p->brightness==110);
  handle_state_click(490,166); CHECK(p->brightness==100);
  on_gamepad_button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER); CHECK(options_page==1);
  CHECK(p->sound && audio_enabled());
  handle_state_click(200,130); CHECK(!p->sound && !audio_enabled());
  on_gamepad_button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT); CHECK(p->sound && audio_enabled());
  options_row=1;
  on_gamepad_button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT); CHECK(p->effects==90);
  on_gamepad_button(SDL_GAMEPAD_BUTTON_START); CHECK(state==ST_PAUSED);
  act_options();
  options_page=3; options_row=1; adjust_option(1); CHECK(state==ST_BINDINGS);
  binding_row=ACTION_DECOY; binding_wait=true; on_key_down(SDL_SCANCODE_G);
  CHECK(!binding_wait && p->keys[ACTION_DECOY]==SDL_SCANCODE_G);
  handle_state_key(SDL_SCANCODE_ESCAPE); CHECK(state==ST_OPTIONS);
  handle_state_key(SDL_SCANCODE_ESCAPE); CHECK(state==ST_PAUSED);
  set_state(ST_PLAYING); game->campaign.journal=true; game->player.health=54;
  on_key_down(SDL_SCANCODE_F5); activate_slot();
  game->player.health=2; on_key_down(SDL_SCANCODE_F9); activate_slot();
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
static void autosave_trap_safety(void) {
  new_game(0,DIFFICULTY_NORMAL); begin_level();
  for (int i=0;i<game->enemy_count;i++) { game->enemies[i].hp=0; game->enemies[i].state=ES_DEAD; }
  game->horror.calm_timer=10000;
  game->player.health=61; game->campaign.checkpoint_requested=true; autosave_game(.1f);
  SaveInfo info;
  CHECK(savegame_inspect(slot_path(3),levels,levels_loaded,&info) && info.health==61);
  CHECK(game->trap_count>0);
  Trap *trap=&game->traps[0]; const TrapSpawn *spawn=trap->spawn;
  position(game,spawn->x+.5f,spawn->y+.5f,1,0);
  game->player.health=75; game->campaign.checkpoint_requested=true; autosave_timer=90;
  input_focused=true; update(.01f);
  CHECK(trap->state==TRAP_TRIGGERED && game->projectile_count==0);
  CHECK(!safe_to_autosave() && autosave_timer>=90 && game->campaign.checkpoint_requested);
  CHECK(savegame_inspect(slot_path(3),levels,levels_loaded,&info) && info.health==61);
  trap->timer=.001f; update(.01f);
  CHECK(trap->state==TRAP_FIRING && game->projectile_count==0 && !safe_to_autosave());
  CHECK(savegame_inspect(slot_path(3),levels,levels_loaded,&info) && info.health==61);
  // Escaping the hazard must allow the deferred progress to be saved again.
  position(game,game->level->start_x,game->level->start_y,1,0);
  for (int i=0;i<100;i++) update(.01f);
  CHECK(!game->player_dead && safe_to_autosave());
  CHECK(autosave_timer<90 && !game->campaign.checkpoint_requested);
  CHECK(savegame_inspect(slot_path(3),levels,levels_loaded,&info) && info.health==75);
  puts("Triggered and firing traps defer autosaves, preserve the safe slot and allow saving after escape.");
}
static void autosave_powder_safety(void) {
  new_game(0,DIFFICULTY_NORMAL); begin_level();
  for (int i=0;i<game->enemy_count;i++) { game->enemies[i].hp=0; game->enemies[i].state=ES_DEAD; }
  game->horror.calm_timer=10000;
  game->player.health=61; game->campaign.checkpoint_requested=true; autosave_game(.1f);
  SaveInfo info;
  CHECK(savegame_inspect(slot_path(3),levels,levels_loaded,&info) && info.health==61);
  // Fire from inside the fatal blast radius of the first chapter's powder.
  position(game,12.2f,16.5f,-1,0);
  CHECK(!game_is_blocked(game,game->player.x,game->player.y,game->player.radius,NULL));
  game->player.health=60; game->campaign.checkpoint_requested=true; autosave_timer=90;
  input.fire=true; input_focused=true; update(.01f);
  CHECK(game->projectile_count==1 && game->projectiles[0].kind==SHOT_MISS);
  CHECK(!safe_to_autosave() && game->campaign.checkpoint_requested && autosave_timer>=90);
  CHECK(savegame_inspect(slot_path(3),levels,levels_loaded,&info) && info.health==61);
  input.fire=false;
  for (int i=0;i<10 && !game->player_dead;i++) update(.01f);
  CHECK(game->player_dead);
  restart_level();
  CHECK(state==ST_PLAYING && !game->player_dead && game->player.health==61 && !game->projectile_count);
  for (int i=0;i<10;i++) update(.01f);
  CHECK(!game->player_dead); // The restored save cannot replay the fatal shot.
  game->player.has_arbalest=true; game->player.weapon=WEAPON_ARBALEST;
  game->burst_remaining=1; game->fire_timer=.3f;
  CHECK(!safe_to_autosave());
  game->burst_remaining=0;
  game->player.health=75; game->campaign.checkpoint_requested=true;
  autosave_game(.01f);
  CHECK(savegame_inspect(slot_path(3),levels,levels_loaded,&info) && info.health==75);
  puts("Own bolts and committed bursts defer autosaves; powder deaths restore a safe position.");
}
static void loaded_story_recovery(void) {
  for (int browser=0;browser<2;browser++) for (int scenario=0;scenario<3;scenario++) {
    preferences_defaults(); selected_slot=2;
    new_game(1,DIFFICULTY_HARD); session.run_id=100+browser*3+scenario; begin_level();
    for (int i=0;i<game->enemy_count;i++) { game->enemies[i].hp=0; game->enemies[i].state=ES_DEAD; }
    game->horror.calm_timer=10000;
    game->player.health=scenario==1?20:61;
    game->player.venom=scenario==1?2:0;
    game->player.ammo=73; game->player.has_arbalest=true; game->player.weapon=WEAPON_ARBALEST;
    game->time=12; session.score=1234;
    int run_id=session.run_id;
    act_save_game(); activate_slot(); CHECK(!strcmp(notice,"GAME SAVED"));
    // Both automatic files now belong to a different playthrough.
    new_game(1,DIFFICULTY_NORMAL); session.run_id=run_id+1000; begin_level();
    if (scenario==2) preferences_get()->autosave=false;
    if (browser) {
      set_state(ST_MENU); act_load_game(); slot_row=2; activate_slot();
    } else { on_key_down(SDL_SCANCODE_F9); activate_slot(); }
    CHECK(state==ST_PLAYING && session.run_id==run_id && game->player.ammo==73 && game->player.has_arbalest);
    SaveInfo info;
    CHECK(savegame_inspect(slot_path(3),levels,levels_loaded,&info));
    if (scenario==0) {
      CHECK(info.run_id==run_id && info.health==61);
      CHECK(savegame_inspect(slot_path(4),levels,levels_loaded,&info) && info.run_id==run_id);
    } else {
      CHECK(info.run_id==run_id+1000); // Neither unsafe loads nor disabled saving replace safe files.
      CHECK(game->campaign.checkpoint_requested || scenario==2);
    }
    game_hurt_player(game,1000); restart_level();
    CHECK(!game->player_dead && game->player.ammo==73 && game->player.has_arbalest && game->player.weapon==WEAPON_ARBALEST);
    CHECK(session.run_id==run_id && session.score==1234 && session.difficulty==DIFFICULTY_HARD);
    if (scenario==0) CHECK(state==ST_PLAYING && game->player.health==61 && game->time==12);
    else CHECK(state==ST_LEVEL_START && game->player.health==100 && game->time==0);
  }
  preferences_defaults(); selected_slot=0;
  puts("Keyboard and menu load selection preserve Story equipment with foreign checkpoints, unsafe loads and saving disabled.");
}
static void closing_door_occupancy(void) {
  reset_fixture();
  Door *door=game_door_at(play,5,5); CHECK(door);
  position(play,4.76f,5.5f,1,0);
  door->state=DOOR_CLOSING; door->open=1;
  Input move={.weapon=-1,.forward=true,.run=true};
  for (int i=0;i<12;i++) game_update(play,.05f,&move);
  CHECK(play->player.x>6.5f && !play->player_dead);
  CHECK(door->state==DOOR_OPEN && door->open==1);
  position(play,3.5f,5.5f,1,0);
  // The same obstruction check protects soldiers and the pursuing Jailer.
  Enemy *guard=&play->enemies[0];
  guard->x=5.5f; guard->y=5.5f; guard->state=ES_STAND; guard->hp=25;
  door->state=DOOR_CLOSING; door->open=.9f;
  ticks(play,1); CHECK(door->state==DOOR_OPENING && door->open>=.9f);
  guard->state=ES_DEAD;
  play->horror.warden=(Warden){.state=WARDEN_HUNT,.x=5.5f,.y=5.5f,.next_x=5.5f,.next_y=5.5f,.remaining=20};
  door->state=DOOR_CLOSING; door->open=.9f;
  ticks(play,1); CHECK(door->state==DOOR_OPENING && door->open>=.9f);
  puts("Closing doors reopen around the player, soldiers and Jailer; sprinting through cannot trap the player.");
}
static void key_event(SDL_Scancode scancode, bool down) {
  SDL_Event event = {.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP};
  event.key.scancode = scancode;
  handle_event(&event);
}
static void overlapping_inputs(void) {
  preferences_defaults(); new_game(0,DIFFICULTY_NORMAL); begin_level();
  const struct { SDL_Scancode first, second; bool *action; } aliases[] = {
    {SDL_SCANCODE_W, SDL_SCANCODE_UP, &input.forward},
    {SDL_SCANCODE_S, SDL_SCANCODE_DOWN, &input.back},
    {SDL_SCANCODE_LALT, SDL_SCANCODE_RALT, &input.strafe_mod},
    {SDL_SCANCODE_J, SDL_SCANCODE_LCTRL, &input.fire},
    {SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL, &input.fire},
  };
  for (size_t i = 0; i < SDL_arraysize(aliases); i++) for (int order = 0; order < 2; order++) {
    clear_input();
    SDL_Scancode first = order ? aliases[i].second : aliases[i].first;
    SDL_Scancode second = order ? aliases[i].first : aliases[i].second;
    key_event(first,true); key_event(first,true); key_event(second,true);
    key_event(first,false); key_event(first,false);
    CHECK(*aliases[i].action); // Only the released source stops contributing.
    key_event(second,false); CHECK(!*aliases[i].action);
  }
  CHECK(preferences_bind(ACTION_FORWARD,SDL_SCANCODE_K));
  key_event(SDL_SCANCODE_K,true); key_event(SDL_SCANCODE_UP,true);
  key_event(SDL_SCANCODE_UP,false); CHECK(input.forward);
  key_event(SDL_SCANCODE_K,false); CHECK(!input.forward);

  SDL_Event mouse = {.type = SDL_EVENT_MOUSE_BUTTON_DOWN};
  mouse.button.button = SDL_BUTTON_LEFT;
  for (int order = 0; order < 2; order++) {
    clear_input();
    game->player.weapon = WEAPON_CROSSBOW; game->player.ammo = 10;
    game->fire_timer = 0; game->fire_held = false;
    key_event(SDL_SCANCODE_LCTRL,true); game_update(game,.01f,&input);
    CHECK(game->player.ammo == 9);
    mouse.type = SDL_EVENT_MOUSE_BUTTON_DOWN; handle_event(&mouse);
    if (order) { mouse.type = SDL_EVENT_MOUSE_BUTTON_UP; handle_event(&mouse); }
    else key_event(SDL_SCANCODE_LCTRL,false);
    CHECK(input.fire);
    game_update(game,.5f,&input);
    CHECK(game->fire_held && game->player.ammo == 9);
    // Pressing the other source again cannot retrigger a still-held crossbow.
    if (order) { mouse.type = SDL_EVENT_MOUSE_BUTTON_DOWN; handle_event(&mouse); }
    else key_event(SDL_SCANCODE_LCTRL,true);
    game_update(game,.01f,&input); CHECK(game->player.ammo == 9);
    key_event(SDL_SCANCODE_LCTRL,false);
    mouse.type = SDL_EVENT_MOUSE_BUTTON_UP; handle_event(&mouse);
    CHECK(!input.fire);
    game_update(game,.01f,&input);
    key_event(SDL_SCANCODE_J,true); game_update(game,.01f,&input);
    CHECK(game->player.ammo == 8);
    key_event(SDL_SCANCODE_J,false);
  }
  key_event(SDL_SCANCODE_K,true); key_event(SDL_SCANCODE_LCTRL,true);
  mouse.type = SDL_EVENT_MOUSE_BUTTON_DOWN; handle_event(&mouse);
  SDL_Event focus = {.type = SDL_EVENT_WINDOW_FOCUS_LOST}; handle_event(&focus);
  CHECK(state == ST_PAUSED && !input.forward && !input.fire);
  focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED; handle_event(&focus);
  handle_state_key(SDL_SCANCODE_ESCAPE);
  key_event(SDL_SCANCODE_J,true); key_event(SDL_SCANCODE_J,false);
  key_event(SDL_SCANCODE_UP,true); key_event(SDL_SCANCODE_UP,false);
  CHECK(!input.fire && !input.forward); // Cleared sources cannot reappear after resume.
  preferences_defaults(); clear_input();
  puts("Overlapping keyboard aliases, rebound movement, mouse fire and focus-reset checks passed.");
}
static void reading_input_isolation(void) {
  new_game(0,DIFFICULTY_NORMAL); begin_level();
  position(game,3.5f,28.5f,0,-1);
  input.use=true; update(.01f);
  CHECK(game->horror.reading==0 && !input.use);
  float time=game->time;
  int ammo=game->player.ammo;
  input.mouse_dx=800; input.forward=true; input.fire=true; input.weapon=WEAPON_DAGGER;
  gamepad_fire_ready=true; update(.01f);
  CHECK(game->time==time && input.mouse_dx==0 && !input.fire && !input.forward && input.weapon==-1 && !gamepad_fire_ready);
  SDL_Event click={.type=SDL_EVENT_MOUSE_BUTTON_DOWN}; click.button.button=SDL_BUTTON_LEFT;
  handle_event(&click); CHECK(!input.fire);
  // Firing and closing in the same frame must not queue a shot or a turn.
  on_key_down(SDL_SCANCODE_LCTRL); on_key_down(SDL_SCANCODE_E); update(.01f); update(.01f);
  CHECK(game->horror.reading==-1 && game->player.dir_x==0 && game->player.dir_y==-1);
  CHECK(game->player.ammo==ammo && game->player.weapon==WEAPON_CROSSBOW && game->projectile_count==0);
  on_key_down(SDL_SCANCODE_J); update(.01f);
  CHECK(game->player.ammo==ammo-1);
  puts("Reading discards camera movement, queued weapon/fire input and controller trigger readiness; fresh input works after closing.");
}
static void journal_and_map_input(void) {
  preferences_defaults(); set_state(ST_PLAYING);
  CHECK(preferences_bind(ACTION_MAP,SDL_SCANCODE_K));
  on_key_down(SDL_SCANCODE_K); CHECK(game->show_map);
  on_key_down(SDL_SCANCODE_TAB); CHECK(game->show_map);
  on_key_down(SDL_SCANCODE_K); CHECK(!game->show_map);
  // Compare the actual rendered hint with an explicit label for the rebound
  // key; controller labels must still override the keyboard binding.
  Image *actual=image_new(W,H),*expected=image_new(W,H);
  render_map(NULL,actual,game,NULL); render_map(NULL,expected,game,"K");
  CHECK(!memcmp(actual->px,expected->px,(size_t)W*H*sizeof(uint32_t)));
  image_fill(actual,0); render_map(NULL,actual,game,"TRIANGLE");
  CHECK(memcmp(actual->px,expected->px,(size_t)W*H*sizeof(uint32_t)));
  image_free(actual); image_free(expected);

  Level custom; char err[128];
  CHECK(level_parse(&custom,"111111111\n1P......1\n111111111\n",err,sizeof err));
  CHECK(!custom.campaign.enabled);
  Game *previous=game; Session custom_session={.lives=3,.next_life=40000};
  game=game_create(&custom,0,&custom_session,NULL);
  preferences_defaults(); preferences_get()->autosave=false;
  set_state(ST_PLAYING); input_focused=true;
  on_key_down(SDL_SCANCODE_W); on_key_down(SDL_SCANCODE_TAB);
  float x=game->player.x;
  on_key_down(SDL_SCANCODE_I); update(.05f);
  CHECK(!game->campaign.journal && game->show_map && input.forward);
  CHECK(game->time>0 && game->player.x>x);
  x=game->player.x;
  on_gamepad_button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT); update(.05f);
  CHECK(!game->campaign.journal && game->show_map && input.forward && game->player.x>x);
  game_destroy(game); level_free(&custom); game=previous; game_activate(game);
  clear_input(); preferences_defaults();
  puts("Rebound automap input and hints, controller labels and custom-map journal input passed.");
}
static void authored_campaign(void) {
  char *path=user_file("production-chapter.dat"),err[128]; CHECK(path);
  for (int n=0;n<LEVEL_COUNT;n++) {
    new_game(n,DIFFICULTY_NORMAL); const CampaignLayout *c=&game->level->campaign;
    CHECK(c->enabled && c->count && c->errand.present && c->has_errand_door);
    const Monk *monk=&game->level->world.monks[c->errand.monk];
    CHECK(campaign_dialogue_pages(game,monk->note)==2);
    CHECK(!strcmp(campaign_dialogue(game,monk->note,0),monk->text));
    CHECK(!strcmp(campaign_dialogue(game,monk->note,1),c->errand.request));
    game->horror.calm_timer=10000;
    for (int i=0;i<game->enemy_count;i++) { game->enemies[i].hp=0; game->enemies[i].state=ES_DEAD; }
    position(game,c->errand.x+.5f,c->errand.y+.5f,1,0); campaign_update(game,.01f);
    CHECK(game->campaign.errand_found && !game->campaign.errand_done);
    Door *reward=game_door_at(game,c->errand_door_x,c->errand_door_y); CHECK(reward && reward->state==DOOR_CLOSED);
    const int directions[][2]={{0,1},{1,0},{-1,0},{0,-1}}; bool spoke=false;
    for (int d=0;d<4;d++) {
      int dx=directions[d][0],dy=directions[d][1];
      position(game,monk->x+.5f+dx,monk->y+.5f+dy,-dx,-dy);
      if (!game_is_blocked(game,game->player.x,game->player.y,game->player.radius,NULL) && horror_near_note(game)==monk->note) {
        Input use={.weapon=-1,.use=true}; game_update(game,.01f,&use); spoke=true; break;
      }
    }
    CHECK(spoke && game->campaign.errand_done && reward->state==DOOR_OPENING);
    CHECK(game->secrets_found==1 && game->horror.reading_page==0);
    CHECK(!strcmp(campaign_dialogue(game,monk->note,0),monk->text));
    CHECK(!strcmp(campaign_dialogue(game,monk->note,1),c->errand.thanks));
    int score=session.score; campaign_converse(game,monk->note); CHECK(score==session.score && game->secrets_found==1);
    float time=game->time;
    game->player.venom=2; game->player.venom_timer=.01f;
    int health=game->player.health;
    Input use={.weapon=-1,.use=true}; game_update(game,.5f,&use);
    CHECK(!use.use && game->horror.reading==monk->note && game->horror.reading_page==1);
    CHECK(game->time==time && game->player.health==health && game->player.venom==2);
    CHECK(savegame_write(path,game,err,sizeof err));
    SaveInfo info; CHECK(savegame_inspect(path,levels,levels_loaded,&info) && info.run_id==session.run_id);
    CHECK(savegame_load(path,levels,levels_loaded,&game,&session,err,sizeof err));
    CHECK(game->campaign.errand_done && game_door_at(game,c->errand_door_x,c->errand_door_y)->state==DOOR_OPENING);
    CHECK(game->horror.reading==monk->note && game->horror.reading_page==1);
    use.use=true; game_update(game,.5f,&use);
    CHECK(!use.use && game->horror.reading==-1 && game->horror.reading_page==0 && game->time==time);
    game->player.venom=0;
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

static void collect_chapter_counts(void) {
  for (int i=0;i<game->enemy_count;i++) enemy_hit(&game->enemies[i],game,10000);
  Input idle={.weapon=-1};
  for (;;) {
    int index=0;
    while (index<game->item_count && game->items[index].kind!=ITEM_TREASURE) index++;
    if (index==game->item_count) break;
    Item item=game->items[index];
    int treasure=game->treasure;
    position(game,item.x,item.y,1,0); game_update(game,.001f,&idle);
    CHECK(!game->player_dead && game->treasure==treasure+1);
  }
  const CampaignLayout *c=&game->level->campaign;
  position(game,c->errand.x+.5f,c->errand.y+.5f,1,0); game_update(game,.001f,&idle);
  campaign_converse(game,game->level->world.monks[c->errand.monk].note);
  CHECK(game->kills==game->total_enemies && game->treasure==game->total_treasure && game->secrets_found==1);
}
static void retry_statistics(void) {
  preferences_get()->arcade=true; new_game(0,DIFFICULTY_NORMAL);
  collect_chapter_counts();
  int kills=session.total_kills,treasure=session.total_treasure,secrets=session.total_secrets_found;
  next_level(); // Completed chapters must survive retries of the following one.
  collect_chapter_counts();
  CHECK(session.total_kills>kills && session.total_treasure>treasure && session.total_secrets_found>secrets);
  char *path=user_file("production-retry.dat"),err[128]; CHECK(path);
  CHECK(savegame_write(path,game,err,sizeof err));
  CHECK(savegame_load(path,levels,levels_loaded,&game,&session,err,sizeof err));
  int score=session.score,lives=session.lives;
  uint32_t relics=session.relics;
  game_hurt_player(game,1000);
  set_state(ST_PLAYING);
  for (int i=0;i<32;i++) update(.05f);
  CHECK(state==ST_DIED && session.lives==lives-1);
  on_key_down(SDL_SCANCODE_RETURN);
  CHECK(state==ST_LEVEL_START && game->level_index==1 && game->player.health==100);
  CHECK(session.total_kills==kills && session.total_treasure==treasure && session.total_secrets_found==secrets);
  CHECK(session.score==score && session.lives==lives-1 && session.relics==relics);
  CHECK(savegame_write(path,game,err,sizeof err));
  CHECK(savegame_load(path,levels,levels_loaded,&game,&session,err,sizeof err));
  collect_chapter_counts(); next_level();
  CHECK(session.total_kills==session.total_enemies && percent(session.total_kills,session.total_enemies)==100);
  CHECK(session.total_treasure==session.max_treasure && percent(session.total_treasure,session.max_treasure)==100);
  CHECK(session.total_secrets_found==secrets+1 && session.total_secrets_found<=session.max_secrets);
  CHECK(SDL_RemovePath(path)); free(path); preferences_defaults();
  puts("Arcade retries, saved attempts, completion counts and carried score/lives/relics passed.");
}
static void custom_map_rules(void) {
  game_destroy(game); game=NULL;
  Level original=levels[0]; char err[128];
  CHECK(level_parse(&levels[0],"111111111\n1P......1\n111111111\n",err,sizeof err));
  CHECK(!levels[0].campaign.enabled);
  for (int arcade=0;arcade<2;arcade++) {
    preferences_defaults(); preferences_get()->autosave=false; preferences_get()->arcade=arcade!=0;
    new_game(0,DIFFICULTY_NORMAL); begin_level(); session.run_id=0;
    // A loaded custom map establishes the same fallback equipment as a campaign map.
    game->player.ammo=73; game->player.has_arbalest=true; game->player.weapon=WEAPON_ARBALEST;
    finish_load_game();
    int attempts=arcade?3:5;
    for (int i=0;i<attempts;i++) {
      game_hurt_player(game,1000); update(1.5f);
      CHECK(session.lives==(arcade?2-i:3));
      if (arcade && i==2) { CHECK(state==ST_GAME_OVER); break; }
      CHECK(state==ST_DIED); restart_level();
      CHECK(state==ST_LEVEL_START && game->player.health==100);
      CHECK(game->player.ammo==(arcade?16:73));
      CHECK(game->player.has_arbalest==!arcade);
      CHECK(game->player.weapon==(arcade?WEAPON_CROSSBOW:WEAPON_ARBALEST));
      begin_level();
    }
    if (!arcade) { game_add_score(game,40000); CHECK(session.lives==3 && session.next_life==80000); }
  }
  game_destroy(game); game=NULL; level_free(&levels[0]); levels[0]=original; preferences_defaults();
  puts("Custom maps honor unlimited Story retries and loaded equipment; Arcade retains three lives and fresh equipment.");
}
static void save_frame(const char *name) {
  SDL_Surface *surface=SDL_CreateSurfaceFrom(W,H,SDL_PIXELFORMAT_ARGB8888,fb->px,W*4);
  CHECK(surface && SDL_SaveBMP(surface,name)); SDL_DestroySurface(surface);
}
static void screenshots(void) {
  CHECK(assets_build(&assets) && comic_art_build(&comic_art)); renderer_init(&renderer,&assets); fb=image_new(W,H);
  preferences_get()->text=150; options_page=0; options_row=4; set_state(ST_OPTIONS); render(); save_frame("build/production-options.bmp");
  options_page=1; options_row=0; render(); save_frame("build/production-sound-options.bmp");
  set_state(ST_MENU); render(); save_frame("build/production-main-menu.bmp");
  set_state(ST_BINDINGS); binding_row=ACTION_DECOY; render(); save_frame("build/production-bindings.bmp");
  refresh_save_slots(); set_state(ST_SLOTS); slot_save_mode=false; render(); save_frame("build/production-slots.bmp");
  slot_save_mode=true; render(); save_frame("build/production-save.bmp");
  new_game(1,DIFFICULTY_NORMAL); set_state(ST_PLAYING); game->campaign.journal=true;
  render(); save_frame("build/production-journal.bmp"); game->campaign.journal=false;
  game->horror.reading=game->level->world.monks[0].note;
  render(); save_frame("build/production-large-dialogue.bmp");
  game->horror.reading_page=1;
  render(); save_frame("build/production-large-errand.bmp");
  new_game(4,DIFFICULTY_NORMAL); set_state(ST_PLAYING);
  game->campaign.errand_found=game->campaign.errand_done=true;
  game->horror.reading=game->level->world.monks[0].note; game->horror.reading_page=1;
  render(); save_frame("build/production-large-thanks.bmp");
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
  CHECK(SDL_Init(0)); fixture_map(); progression(); tactical_tools(); soldier_combat(); preferences_and_saves();
  autosave_trap_safety(); autosave_powder_safety(); loaded_story_recovery(); closing_door_occupancy();
  overlapping_inputs(); reading_input_isolation(); journal_and_map_input(); authored_campaign(); retry_statistics(); custom_map_rules();
  if (argc>1 && !strcmp(argv[1],"--screenshots")) screenshots();
  game_destroy(play); level_free(&fixture); game_destroy(game);
  for (int i=0;i<levels_loaded;i++) level_free(&levels[i]);
  free(save_path); for (int i=0;i<2;i++) free(manual_paths[i]); free(automatic_path); free(checkpoint_path);
  SDL_Quit(); return 0;
}
