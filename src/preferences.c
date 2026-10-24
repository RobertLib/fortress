#include "preferences.h"
#include <SDL3/SDL.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const Preferences defaults={100,100,100,100,100,85,45,true,true,false,true,false,
  {SDL_SCANCODE_W,SDL_SCANCODE_S,SDL_SCANCODE_A,SDL_SCANCODE_D,SDL_SCANCODE_J,SDL_SCANCODE_E,
   SDL_SCANCODE_LSHIFT,SDL_SCANCODE_TAB,SDL_SCANCODE_I,SDL_SCANCODE_Q,SDL_SCANCODE_B}};
static Preferences current={100,100,100,100,100,85,45,true,true,false,true,false,
  {SDL_SCANCODE_W,SDL_SCANCODE_S,SDL_SCANCODE_A,SDL_SCANCODE_D,SDL_SCANCODE_J,SDL_SCANCODE_E,
   SDL_SCANCODE_LSHIFT,SDL_SCANCODE_TAB,SDL_SCANCODE_I,SDL_SCANCODE_Q,SDL_SCANCODE_B}};
Preferences *preferences_get(void) { return &current; }
void preferences_defaults(void) { current=defaults; }
const char *preferences_action_name(KeyAction action) {
  static const char *names[]={"MOVE FORWARD","MOVE BACK","STRAFE LEFT","STRAFE RIGHT","FIRE",
    "USE / SPEAK","RUN","AUTOMAP","FIELD JOURNAL","THROW DECOY","BRACE DOOR"};
  return action>=0 && action<ACTION_COUNT?names[action]:"UNKNOWN ACTION";
}
static bool bindable(int sc) {
  return sc>SDL_SCANCODE_UNKNOWN && sc<SDL_SCANCODE_COUNT && sc!=SDL_SCANCODE_ESCAPE &&
    sc!=SDL_SCANCODE_RETURN && sc!=SDL_SCANCODE_F5 && sc!=SDL_SCANCODE_F9 &&
    sc!=SDL_SCANCODE_F && sc!=SDL_SCANCODE_M && !(sc>=SDL_SCANCODE_1 && sc<=SDL_SCANCODE_3) &&
    sc!=SDL_SCANCODE_LEFT && sc!=SDL_SCANCODE_RIGHT && sc!=SDL_SCANCODE_UP && sc!=SDL_SCANCODE_DOWN &&
    sc!=SDL_SCANCODE_SPACE && sc!=SDL_SCANCODE_LCTRL && sc!=SDL_SCANCODE_RCTRL &&
    sc!=SDL_SCANCODE_LALT && sc!=SDL_SCANCODE_RALT && *SDL_GetScancodeName((SDL_Scancode)sc);
}
static bool valid(const Preferences *p) {
  if (p->sensitivity<25 || p->sensitivity>250 || p->brightness<70 || p->brightness>140 ||
      p->bob<0 || p->bob>100 || p->shake<0 || p->shake>100 ||
      p->text<100 || p->text>150 || p->effects<0 || p->effects>100 || p->music<0 || p->music>100) return false;
  for (int i=0;i<ACTION_COUNT;i++) {
    if (!bindable(p->keys[i])) return false;
    for (int j=0;j<i;j++) if (p->keys[j]==p->keys[i]) return false;
  }
  return true;
}
int preferences_action_for_key(int scancode) {
  for (int i=0;i<ACTION_COUNT;i++) if (current.keys[i]==scancode) return i;
  return -1;
}
bool preferences_bind(KeyAction action,int scancode) {
  if (action<0 || action>=ACTION_COUNT || !bindable(scancode)) return false;
  int other=preferences_action_for_key(scancode),previous=current.keys[action];
  current.keys[action]=scancode;
  if (other>=0 && other!=(int)action) current.keys[other]=previous;
  return true;
}
static bool read_integer(const char **cursor,int *value) {
  const char *start=*cursor;
  while (isspace((unsigned char)*start)) start++;
  char *end; errno=0; long parsed=strtol(start,&end,10);
  if (end==start || errno==ERANGE || parsed<INT_MIN || parsed>INT_MAX ||
      (*end && !isspace((unsigned char)*end))) return false;
  *value=(int)parsed; *cursor=end; return true;
}
bool preferences_load(const char *path) {
  if (!path) return false;
  SDL_IOStream *file=SDL_IOFromFile(path,"rb"); if (!file) return false;
  Sint64 length=SDL_GetIOSize(file);
  if (length<=0 || length>4096) { SDL_CloseIO(file); return false; }
  size_t size=(size_t)length; char *data=malloc(size+1);
  bool read=data && SDL_ReadIO(file,data,size)==size;
  if (!SDL_CloseIO(file)) read=false;
  if (!read) { free(data); return false; }
  data[size]=0;
  Preferences staged=defaults;
  int v=0,tutorials=0,autosave=0,arcade=0,sound=1,fullscreen=0;
  const char *prefix="FORTRESS OPTIONS ";
  bool ok=size>strlen(prefix) && !strncmp(data,prefix,strlen(prefix));
  const char *p=data+(ok?strlen(prefix):0);
  int *values[]={&v,&staged.sensitivity,&staged.brightness,&staged.bob,&staged.shake,&staged.text,
    &staged.effects,&staged.music,&tutorials,&autosave,&arcade};
  for (size_t i=0;i<SDL_arraysize(values) && ok;i++) ok=read_integer(&p,values[i]);
  ok=ok && (v==1 || v==2) && tutorials>=0 && tutorials<=1 && autosave>=0 && autosave<=1 && arcade>=0 && arcade<=1;
  if (ok && v>=2) ok=read_integer(&p,&sound) && read_integer(&p,&fullscreen) &&
    sound>=0 && sound<=1 && fullscreen>=0 && fullscreen<=1;
  staged.tutorials=tutorials!=0; staged.autosave=autosave!=0; staged.arcade=arcade!=0;
  staged.sound=sound!=0; staged.fullscreen=fullscreen!=0;
  for (int i=0;i<ACTION_COUNT && ok;i++) {
    ok=read_integer(&p,&staged.keys[i]);
  }
  while (isspace((unsigned char)*p)) p++;
  if (*p || (size_t)(p-data)!=size) ok=false;
  if (ok) ok=valid(&staged);
  free(data);
  if (ok) current=staged;
  return ok;
}
bool preferences_save(const char *path) {
  if (!path || !valid(&current)) return false;
  char data[1024];
  int n=snprintf(data,sizeof data,"FORTRESS OPTIONS 2\n%d %d %d %d %d %d %d\n%d %d %d %d %d\n",
    current.sensitivity,current.brightness,current.bob,current.shake,current.text,current.effects,current.music,
    current.tutorials,current.autosave,current.arcade,current.sound,current.fullscreen);
  for (int i=0;i<ACTION_COUNT;i++) n+=snprintf(data+n,sizeof data-(size_t)n,"%d%s",current.keys[i],i+1==ACTION_COUNT?"\n":" ");
  size_t len=strlen(path)+5; char *temporary=malloc(len);
  if (!temporary) return false;
  snprintf(temporary,len,"%s.tmp",path);
  bool ok=SDL_SaveFile(temporary,data,(size_t)n) && SDL_RenamePath(temporary,path);
  if (!ok) SDL_RemovePath(temporary);
  free(temporary); return ok;
}
