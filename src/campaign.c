#include "campaign.h"
#include "game.h"
#include "preferences.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool floor_cell(const Level *lv, int x, int y) {
  return x > 0 && y > 0 && x < lv->w-1 && y < lv->h-1 && lv->grid[y*lv->w+x] == '.';
}
static bool words(const char *s, size_t capacity) {
  if (!*s || strlen(s) >= capacity) return false;
  for (; *s; s++) if ((unsigned char)*s < 32 || (unsigned char)*s > 126) return false;
  return true;
}
static bool split(char *s, char **second) {
  *second = strchr(s, '|');
  if (!*second) return false;
  *(*second)++ = 0;
  return true;
}
static bool errand_split(char *s,char **second) {
  *second=strchr(s,'~');
  if (!*second) return false;
  *(*second)++=0; return true;
}
bool campaign_configure(Level *lv, const char *text, char *err, size_t size) {
  CampaignLayout *c = &lv->campaign;
  uint32_t hash = 2166136261u;
  for (const char *p = text; *p;) {
    const char *end = strchr(p, '\n');
    size_t length = end ? (size_t)(end-p) : strlen(p);
    const char *names[] = {"@campaign", "@mechanism", "@relic", "@errand", "@drain", "@erranddoor"};
    int directive = -1;
    for (int i=0;i<6;i++) {
      size_t n = strlen(names[i]);
      if (length >= n && !strncmp(p,names[i],n) && (length==n || isspace((unsigned char)p[n]))) directive=i;
    }
    if (directive >= 0) {
      char line[768], extra;
      if (length >= sizeof line) goto invalid;
      for (size_t i=0;i<length;i++) hash = (hash ^ (uint8_t)p[i])*16777619u;
      hash = (hash ^ '\n')*16777619u;
      memcpy(line,p,length); line[length]=0;
      while (length && isspace((unsigned char)line[length-1])) line[--length]=0;
      char *value=line+strlen(names[directive]);
      while (isspace((unsigned char)*value)) value++;
      int used=0;
      if (directive==0) {
        int enabled;
        if (sscanf(value,"%d %c",&enabled,&extra)!=1 || enabled!=1 || c->enabled) goto invalid;
        c->enabled=true;
      } else if (directive==1) {
        if (c->count==CAMPAIGN_STATIONS) goto invalid;
        Mechanism *m=&c->stations[c->count];
        char kind[20], *hint;
        if (sscanf(value,"%d %d %19s %d %f %n",&m->x,&m->y,kind,&m->requires,&m->duration,&used)!=5 ||
            !used || !floor_cell(lv,m->x,m->y) || !isfinite(m->duration) || m->duration<0 || m->duration>12 ||
            m->requires<0 || m->requires >= (1<<c->count)) goto invalid;
        const char *kinds[]={"winch","valve","bell","seal","grave","oath"};
        int k=0; while (k<MECHANISM_COUNT && strcmp(kind,kinds[k])) k++;
        if (k==MECHANISM_COUNT) goto invalid;
        m->kind=(MechanismKind)k;
        char *name=value+used;
        if (!split(name,&hint) || !words(name,sizeof m->name) || !words(hint,sizeof m->hint)) goto invalid;
        snprintf(m->name,sizeof m->name,"%s",name); snprintf(m->hint,sizeof m->hint,"%s",hint);
        for (int i=0;i<c->count;i++) if (c->stations[i].x==m->x && c->stations[i].y==m->y) goto invalid;
        c->count++;
      } else if (directive==2) {
        if (c->relic_count==CAMPAIGN_RELICS) goto invalid;
        RelicSpawn *r=&c->relics[c->relic_count++]; int kind;
        if (sscanf(value,"%d %d %d %c",&r->x,&r->y,&kind,&extra)!=3 || !floor_cell(lv,r->x,r->y) ||
            kind<0 || kind>=CAMPAIGN_RELICS) goto invalid;
        r->kind=(RelicKind)kind;
      } else if (directive==3) {
        Errand *e=&c->errand; char *request,*thanks;
        if (e->present || sscanf(value,"%d %d %d %n",&e->x,&e->y,&e->monk,&used)!=3 || !used ||
            !floor_cell(lv,e->x,e->y) || e->monk<0 || e->monk>=lv->world.monk_count) goto invalid;
        char *name=value+used;
        if (!errand_split(name,&request) || !errand_split(request,&thanks) || !words(name,sizeof e->name) ||
            !words(request,sizeof e->request) || !words(thanks,sizeof e->thanks)) goto invalid;
        e->present=true;
        snprintf(e->name,sizeof e->name,"%s",name); snprintf(e->request,sizeof e->request,"%s",request);
        snprintf(e->thanks,sizeof e->thanks,"%s",thanks);
      } else if (directive==5) {
        if (c->has_errand_door || sscanf(value,"%d %d %c",&c->errand_door_x,&c->errand_door_y,&extra)!=2 ||
            c->errand_door_x<1 || c->errand_door_y<1 || c->errand_door_x>=lv->w-1 || c->errand_door_y>=lv->h-1 ||
            lv->grid[c->errand_door_y*lv->w+c->errand_door_x]!='Z') goto invalid;
        c->has_errand_door=true;
      } else {
        if (c->drain_w || sscanf(value,"%d %d %d %d %d %c",&c->drain_x,&c->drain_y,&c->drain_w,&c->drain_h,&c->drain_requires,&extra)!=5 ||
            c->drain_x<0 || c->drain_y<0 || c->drain_w<=0 || c->drain_h<=0 ||
            c->drain_w>lv->w-c->drain_x || c->drain_h>lv->h-c->drain_y || c->drain_requires<=0) goto invalid;
      }
    }
    if (!end) break;
    p=end+1;
  }
  if ((c->count || c->relic_count || c->errand.present || c->drain_w) && !c->enabled) goto invalid;
  if (c->enabled && (!c->count || !lv->world.cells)) goto invalid;
  if (c->has_errand_door && !c->errand.present) goto invalid;
  for (int i=0;i<c->count;i++) if (c->stations[i].kind==MECHANISM_OATH &&
      (i!=c->count-1 || c->stations[i].requires!=(1<<i)-1)) goto invalid;
  if (c->drain_requires >= (1<<c->count)) goto invalid;
  c->signature=c->enabled?hash:0;
  return true;
invalid:
  snprintf(err,size,"Invalid campaign directive or objective location");
  return false;
}
bool campaign_reserved(const Level *lv,int x,int y) {
  const CampaignLayout *c=&lv->campaign;
  for (int i=0;i<c->count;i++) if (abs(x-c->stations[i].x)<=1 && abs(y-c->stations[i].y)<=1) return true;
  for (int i=0;i<c->relic_count;i++) if (x==c->relics[i].x && y==c->relics[i].y) return true;
  return c->errand.present && x==c->errand.x && y==c->errand.y;
}
const char *campaign_relic_name(RelicKind kind) {
  static const char *names[]={"THE HUSHED SOLE","THE FLETCHER'S SIGN","THE HEALER'S SEAL","THE VIGIL CROSS","THE ASHEN WARD"};
  return kind>=0 && kind<CAMPAIGN_RELICS?names[kind]:"UNKNOWN RELIC";
}
bool campaign_has_relic(const Game *g,RelicKind kind) { return (g->session->relics & (1u<<kind))!=0; }
void campaign_init(Game *g) {
  g->campaign.active=-1;
  g->campaign.decoys=2; g->campaign.braces=1;
  g->campaign.lesson_timer=3;
  for (int i=0;i<CAMPAIGN_BRACES;i++) g->campaign.brace[i].door=-1;
}
static bool near(const Game *g,int x,int y,float radius,bool facing) {
  float dx=x+.5f-g->player.x,dy=y+.5f-g->player.y,d=hypotf(dx,dy);
  return d<radius && (!facing || d<.45f || dx*g->player.dir_x+dy*g->player.dir_y>d*.35f) &&
    fabsf(level_floor(g->level,x+.5f,y+.5f)-level_floor(g->level,g->player.x,g->player.y))<=.25f &&
    game_line_of_sight(g,g->player.x,g->player.y,x+.5f,y+.5f);
}
int campaign_near_station(const Game *g) {
  if (!g->level->campaign.enabled) return -1;
  for (int i=0;i<g->level->campaign.count;i++) {
    const Mechanism *m=&g->level->campaign.stations[i];
    if (near(g,m->x,m->y,1.45f,true)) return i;
  }
  return -1;
}
static void finish_mechanism(Game *g,int index) {
  CampaignState *s=&g->campaign;
  const Mechanism *m=&g->level->campaign.stations[index];
  s->done|=1u<<index; s->active=-1; s->work=0; s->objective_flash=4;
  s->checkpoint_requested=true;
  game_add_score(g,300);
  audio_play(m->kind==MECHANISM_BELL || m->kind==MECHANISM_OATH?SND_HORROR_BELL:SND_LEVER,.9f,1);
  game_say(g,m->hint,4);
  if (m->kind==MECHANISM_BELL || m->kind==MECHANISM_OATH) {
    horror_noise(g,m->x+.5f,m->y+.5f,8);
    if (!g->horror.pending_hunts) g->horror.pending_hunts=1;
    g->horror.calm_timer=0;
  }
  if (m->kind==MECHANISM_OATH) {
    s->finale=true;
    game_say(g,"THE OATH IS BROKEN. REACH THE LAST GATE!",5);
  }
}
bool campaign_use(Game *g) {
  int i=campaign_near_station(g);
  if (i<0) return false;
  CampaignState *s=&g->campaign;
  const Mechanism *m=&g->level->campaign.stations[i];
  if (s->done&(1u<<i)) { game_say(g,"THIS WORK IS ALREADY FINISHED",2); return true; }
  if ((s->done&m->requires)!=(uint32_t)m->requires) {
    game_say(g,campaign_objective(g),3); audio_play(SND_LOCKED,.6f,1); return true;
  }
  if (m->kind==MECHANISM_OATH && g->player.keys<3) {
    game_say(g,"RECOVER ALL THREE KEYS BEFORE SOUNDING THE LAST BELL",4); return true;
  }
  if (m->kind==MECHANISM_SEAL) {
    for (int k=0;k<g->enemy_count;k++) if (g->enemies[k].type==ENEMY_CAPTAIN && !enemy_dead(&g->enemies[k])) {
      game_say(g,"THE CAPTAIN STILL CARRIES THE GARRISON SEAL",3); return true;
    }
  }
  if (m->duration>0) {
    s->active=i;
    game_say(g,"WORKING THE MECHANISM - STAY CLOSE",3);
    horror_noise(g,m->x+.5f,m->y+.5f,4);
    audio_play(SND_LEVER,.8f,.8f);
  } else finish_mechanism(g,i);
  return true;
}
const char *campaign_objective(const Game *g) {
  const CampaignLayout *c=&g->level->campaign;
  for (int i=0;i<c->count;i++) if (!(g->campaign.done&(1u<<i))) {
    if (c->stations[i].kind==MECHANISM_OATH && g->player.keys<3) return "RECOVER THREE KEYS BEFORE SOUNDING THE LAST BELL";
    return c->stations[i].name;
  }
  return g->player.keys<3?"FIND THE THREE GATE KEYS":"REACH THE CHAPTER GATE";
}
bool campaign_gate_ready(Game *g) {
  const CampaignLayout *c=&g->level->campaign;
  if (!c->enabled || g->campaign.done==(uint32_t)((1<<c->count)-1)) return true;
  game_say(g,campaign_objective(g),4); audio_play(SND_LOCKED,.7f,1);
  return false;
}
bool campaign_drained(const Game *g,int x,int y) {
  const CampaignLayout *c=&g->level->campaign;
  return c->drain_w && (g->campaign.done&c->drain_requires)==(uint32_t)c->drain_requires &&
    x>=c->drain_x && y>=c->drain_y && x<c->drain_x+c->drain_w && y<c->drain_y+c->drain_h;
}
bool campaign_door_braced(const Game *g,int door) {
  for (int i=0;i<CAMPAIGN_BRACES;i++) if (g->campaign.brace[i].door==door && g->campaign.brace[i].remaining>0) return true;
  return false;
}
void campaign_throw_decoy(Game *g) {
  CampaignState *s=&g->campaign;
  if (!g->level->campaign.enabled) return;
  if (!s->decoys) { game_say(g,"NO NOISE DECOYS LEFT",2); return; }
  if (s->decoy_timer>0) { game_say(g,"A DECOY IS STILL RINGING",2); return; }
  float x=g->player.x,y=g->player.y;
  for (int i=0;i<22;i++) {
    float nx=x+g->player.dir_x*.2f,ny=y+g->player.dir_y*.2f;
    if (!level_is_open(game_cell(g,(int)nx,(int)ny)) || game_cell(g,(int)nx,(int)ny)=='!' ||
        fabsf(level_floor(g->level,nx,ny)-level_floor(g->level,x,y))>.25f) break;
    x=nx; y=ny;
  }
  s->decoys--; s->decoy_x=x; s->decoy_y=y; s->decoy_timer=7; s->decoy_pulse=0;
  game_say(g,"DECOY THROWN - BREAK LINE OF SIGHT",3);
}
void campaign_brace_door(Game *g) {
  if (!g->level->campaign.enabled) return;
  CampaignState *s=&g->campaign;
  if (!s->braces) { game_say(g,"NO DOOR BRACES LEFT",2); return; }
  for (int d=0;d<g->door_count;d++) {
    Door *door=&g->doors[d];
    if (door->kind!='D' || !near(g,door->x,door->y,1.8f,true) || door->open>.05f) continue;
    if (campaign_door_braced(g,d)) { game_say(g,"THIS DOOR IS ALREADY BRACED",2); return; }
    for (int i=0;i<CAMPAIGN_BRACES;i++) if (s->brace[i].remaining<=0) {
      s->brace[i]=(DoorBrace){d,10}; s->braces--;
      game_say(g,"DOOR BRACED FOR TEN SECONDS",3); audio_play(SND_LEVER,.8f,.7f); return;
    }
  }
  game_say(g,"FACE A CLOSED WOODEN DOOR TO BRACE IT",3);
}
void campaign_converse(Game *g,int note) {
  const Errand *e=&g->level->campaign.errand;
  if (!e->present || note!=g->level->world.monks[e->monk].note || !g->campaign.errand_found || g->campaign.errand_done) return;
  g->campaign.errand_done=true;
  g->campaign.decoys=mini(5,g->campaign.decoys+2); g->campaign.braces=mini(3,g->campaign.braces+1);
  g->player.health=mini(100,g->player.health+25); g->player.ammo=mini(99,g->player.ammo+8);
  g->campaign.checkpoint_requested=true;
  const CampaignLayout *c=&g->level->campaign;
  if (c->has_errand_door) {
    Door *door=game_door_at(g,c->errand_door_x,c->errand_door_y);
    if (door && door->state==DOOR_CLOSED) {
      door->state=DOOR_OPENING; g->secrets_found++; g->session->total_secrets_found++;
      Listener l=game_listener(g); audio_play_at(SND_SECRET,door->x+.5f,door->y+.5f,&l,.8f,1);
    }
  }
  game_add_score(g,1000); audio_play(SND_PICKUP_WEAPON,.8f,1);
}
int campaign_dialogue_pages(const Game *g,int note) {
  const Errand *e=&g->level->campaign.errand;
  return e->present && note==g->level->world.monks[e->monk].note ? 2 : 1;
}
const char *campaign_dialogue(const Game *g,int note,int page) {
  const Errand *e=&g->level->campaign.errand;
  if (page==1 && campaign_dialogue_pages(g,note)>1) {
    if (g->campaign.errand_done) return e->thanks;
    return e->request;
  }
  return g->level->horror.notes[note].text;
}
static void lesson(Game *g,uint32_t bit,const char *text) {
  if (preferences_get()->tutorials && !(g->campaign.lessons&bit) && g->campaign.lesson_timer<=0 && g->message_timer<=0) {
    g->campaign.lessons|=bit; g->campaign.lesson_timer=10; game_say(g,text,5);
  }
}
void campaign_update(Game *g,float dt) {
  if (!g->level->campaign.enabled) return;
  const CampaignLayout *c=&g->level->campaign; CampaignState *s=&g->campaign;
  s->lesson_timer=maxf(0,s->lesson_timer-dt); s->objective_flash=maxf(0,s->objective_flash-dt);
  for (int i=0;i<CAMPAIGN_BRACES;i++) {
    DoorBrace *b=&s->brace[i];
    if (b->remaining>0) {
      b->remaining=maxf(0,b->remaining-dt);
      if (b->remaining==0) { audio_play(SND_WALL_HIT,.5f,.8f); game_say(g,"A DOOR BRACE HAS GIVEN WAY",2); }
    }
  }
  if (s->active>=0) {
    const Mechanism *m=&c->stations[s->active];
    if (near(g,m->x,m->y,2.1f,false)) {
      s->work+=dt;
      if (s->work>=m->duration) finish_mechanism(g,s->active);
    } else { s->active=-1; s->work=0; game_say(g,"WORK INTERRUPTED - USE THE MECHANISM AGAIN",3); }
  }
  for (int i=0;i<c->relic_count;i++) if (!(s->relics_taken&(1u<<i)) && near(g,c->relics[i].x,c->relics[i].y,.6f,false)) {
    s->relics_taken|=1u<<i; g->session->relics|=1u<<c->relics[i].kind;
    game_say(g,campaign_relic_name(c->relics[i].kind),4); game_add_score(g,750);
    s->checkpoint_requested=true; audio_play(SND_PICKUP_WEAPON,1,.85f);
  }
  if (c->errand.present && !s->errand_found && near(g,c->errand.x,c->errand.y,.6f,false)) {
    s->errand_found=true;
    s->checkpoint_requested=true;
    char message[96]; snprintf(message,sizeof message,"FOUND %s - RETURN TO THE MONK",c->errand.name);
    game_say(g,message,4); audio_play(SND_PICKUP_KEY,.8f,1.2f);
  }
  if (s->decoy_timer>0) {
    s->decoy_timer=maxf(0,s->decoy_timer-dt); s->decoy_pulse-=dt;
    if (s->decoy_pulse<=0) {
      s->decoy_pulse=.7f; horror_noise(g,s->decoy_x,s->decoy_y,1);
      Listener l=game_listener(g); audio_play_at(SND_HORROR_KNOCK,s->decoy_x,s->decoy_y,&l,.9f,1.7f);
      for (int i=0;i<g->enemy_count;i++) {
        Enemy *e=&g->enemies[i];
        if (!enemy_dead(e) && e->type<=ENEMY_CAPTAIN && hypotf(e->x-s->decoy_x,e->y-s->decoy_y)<9 &&
            !game_line_of_sight(g,e->x,e->y,g->player.x,g->player.y)) {
          e->last_seen_x=s->decoy_x; e->last_seen_y=s->decoy_y; e->has_last_seen=true;
          e->state=ES_CHASE;
        }
      }
    }
  }
  if (s->finale && g->horror.warden.state==WARDEN_DORMANT && !g->horror.pending_hunts) {
    g->horror.pending_hunts=1; g->horror.calm_timer=0;
  }
  if (g->level_index==0) {
    lesson(g,1,"EXPLORE THE ESTATE. THE JOURNAL RECORDS YOUR OBJECTIVES.");
    if (g->fire_held) lesson(g,2,"DELIBERATE SHOTS SAVE BOLTS. CROSSBOW NOISE DRAWS ATTENTION.");
    if (horror_in_haven(g,g->player.x,g->player.y)) lesson(g,4,"WHITE CANDLES SHELTER YOU FROM THE JAILER.");
    if (g->horror.warden.state==WARDEN_WARNING || g->horror.warden.state==WARDEN_HUNT)
      lesson(g,8,"BREAK HIS SIGHT. A DECOY OR BRACED DOOR BUYS TIME.");
    if (campaign_near_station(g)>=0) lesson(g,16,"USE THE MARKED MECHANISM. THE JOURNAL EXPLAINS EACH STEP.");
  }
}
