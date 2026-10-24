// Small authored roof meshes, rasterised against the same per-pixel depth as
// walls and sprites. These add exterior silhouettes without stacked rooms.
#include "render.h"
#include <math.h>
typedef struct RoofVertex { float x,y,z,u,v; } RoofVertex;
typedef struct Projected { float x,y,q,u,v; } Projected;
static float edge(float ax,float ay,float bx,float by,float x,float y) { return (x-ax)*(by-ay)-(y-ay)*(bx-ax); }
static RoofVertex interpolate(RoofVertex a,RoofVertex b,float t) {
  return (RoofVertex){a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.u+(b.u-a.u)*t,a.v+(b.v-a.v)*t};
}
static void triangle(Renderer *r,Image *fb,const Game *g,RoofVertex a,RoofVertex b,RoofVertex c,const Image *texture,float light) {
  RoofVertex input[]={a,b,c},clipped[5]; int count=0;
  float inv=1/(g->player.plane_x*g->player.dir_y-g->player.dir_x*g->player.plane_y);
  for (int i=0;i<3;i++) {
    RoofVertex *v=&input[i]; float dx=v->x-g->player.x,dy=v->y-g->player.y;
    v->x=inv*(g->player.dir_y*dx-g->player.dir_x*dy);
    v->y=inv*(-g->player.plane_y*dx+g->player.plane_x*dy);
  }
  for (int i=0;i<3;i++) {
    RoofVertex p=input[i],q=input[(i+1)%3]; bool pi=p.y>=.06f,qi=q.y>=.06f;
    if (pi) clipped[count++]=p;
    if (pi!=qi) clipped[count++]=interpolate(p,q,(.06f-p.y)/(q.y-p.y));
  }
  for (int tri=1;tri+1<count;tri++) {
    RoofVertex v[]={clipped[0],clipped[tri],clipped[tri+1]}; Projected s[3];
    for (int i=0;i<3;i++) {
      float q=1/v[i].y;
      s[i]=(Projected){SCREEN_W*.5f*(1+v[i].x*q),VIEW_H*.5f+(r->eye_z-v[i].z)*VIEW_H*q,q,v[i].u*q,v[i].v*q};
    }
    float area=edge(s[0].x,s[0].y,s[1].x,s[1].y,s[2].x,s[2].y);
    if (fabsf(area)<.0001f) continue;
    int x0=clampi((int)floorf(minf(s[0].x,minf(s[1].x,s[2].x))),0,SCREEN_W-1);
    int x1=clampi((int)ceilf(maxf(s[0].x,maxf(s[1].x,s[2].x))),0,SCREEN_W-1);
    int y0=clampi((int)floorf(minf(s[0].y,minf(s[1].y,s[2].y))),0,VIEW_H-1);
    int y1=clampi((int)ceilf(maxf(s[0].y,maxf(s[1].y,s[2].y))),0,VIEW_H-1);
    for (int y=y0;y<=y1;y++) for (int x=x0;x<=x1;x++) {
      float w0=edge(s[1].x,s[1].y,s[2].x,s[2].y,x+.5f,y+.5f)/area;
      float w1=edge(s[2].x,s[2].y,s[0].x,s[0].y,x+.5f,y+.5f)/area;
      float w2=1-w0-w1;
      if (w0<-.00001f || w1<-.00001f || w2<-.00001f) continue;
      float q=w0*s[0].q+w1*s[1].q+w2*s[2].q,depth=1/q;
      int index=y*SCREEN_W+x;
      if (depth>=r->depth[index]-.0001f) continue;
      float u=(w0*s[0].u+w1*s[1].u+w2*s[2].u)*depth;
      float vtex=(w0*s[0].v+w1*s[1].v+w2*s[2].v)*depth;
      int tx=(int)((u-floorf(u))*texture->w),ty=(int)((vtex-floorf(vtex))*texture->h);
      uint32_t color=texture->px[ty*texture->w+tx];
      float fog=clampf((depth-3)/27,0,.84f),shade=light*(1-fog);
      fb->px[index]=pack_argb(255,((color>>16)&255)*shade+9*fog,((color>>8)&255)*shade+15*fog,(color&255)*shade+21*fog);
      r->depth[index]=depth; r->floor_mask[index]=0;
    }
  }
}
static void gable_face(Renderer *r,Image *fb,const Game *g,RoofVertex a,RoofVertex b,RoofVertex c,const Image *texture,float light,int axis) {
  a.u=(axis?a.x:a.y)*.5f; b.u=(axis?b.x:b.y)*.5f; c.u=(axis?c.x:c.y)*.5f;
  a.v=-a.z; b.v=-b.z; c.v=-c.z;
  triangle(r,fb,g,a,b,c,texture,light);
}
void render_roofs(Renderer *r,Image *fb,const Game *g) {
  for (int i=0;i<g->level->world.roof_count;i++) {
    const WorldRoof *roof=&g->level->world.roofs[i];
    float x=roof->x,y=roof->y,xx=x+roof->w,yy=y+roof->h,z=roof->eave;
    RoofVertex a={x,y,z,0,0},b={xx,y,z,roof->w*.5f,0},c={xx,yy,z,roof->w*.5f,roof->h*.5f},d={x,yy,z,0,roof->h*.5f};
    RoofVertex e=roof->axis?(RoofVertex){(x+xx)*.5f,y,roof->ridge,roof->w*.25f,0}:(RoofVertex){x,(y+yy)*.5f,roof->ridge,0,roof->h*.25f};
    RoofVertex f=roof->axis?(RoofVertex){(x+xx)*.5f,yy,roof->ridge,roof->w*.25f,roof->h*.5f}:(RoofVertex){xx,(y+yy)*.5f,roof->ridge,roof->w*.5f,roof->h*.25f};
    const Image *texture=r->assets->floors[roof->slate?SURFACE_VAULT:SURFACE_BARRACKS][1];
    const Image *gable=r->assets->walls[roof->slate?WT_STONE:WT_TIMBER];
    if (roof->axis) {
      triangle(r,fb,g,a,e,f,texture,.70f); triangle(r,fb,g,a,f,d,texture,.70f);
      triangle(r,fb,g,e,b,c,texture,.48f); triangle(r,fb,g,e,c,f,texture,.48f);
      gable_face(r,fb,g,a,b,e,gable,.72f,roof->axis); gable_face(r,fb,g,d,f,c,gable,.62f,roof->axis);
    } else {
      triangle(r,fb,g,a,b,f,texture,.64f); triangle(r,fb,g,a,f,e,texture,.64f);
      triangle(r,fb,g,e,f,c,texture,.48f); triangle(r,fb,g,e,c,d,texture,.48f);
      gable_face(r,fb,g,a,e,d,gable,.65f,roof->axis); gable_face(r,fb,g,b,c,f,gable,.74f,roof->axis);
    }
  }
}
