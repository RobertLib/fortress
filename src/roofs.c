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
static void quad(Renderer *r,Image *fb,const Game *g,RoofVertex a,RoofVertex b,RoofVertex c,RoofVertex d,
                 const Image *texture,float light) {
  triangle(r,fb,g,a,b,c,texture,light);
  triangle(r,fb,g,a,c,d,texture,light);
}

static void slope(Renderer *r,Image *fb,const Game *g,RoofVertex a,RoofVertex b,RoofVertex c,RoofVertex d,
                  const Image *texture,const Image *wood,float light) {
  float length=hypotf(b.x-a.x,b.y-a.y)*.5f;
  float run=hypotf(d.x-a.x,d.y-a.y),height=d.z-a.z;
  float courses=hypotf(run,height)*.5f;
  a.u=d.u=0; b.u=c.u=length; a.v=b.v=courses; c.v=d.v=0;
  quad(r,fb,g,a,b,c,d,texture,light);
  // A solid timber underside closes the overhang when seen from below.
  a.z-=.09f; b.z-=.09f; c.z-=.09f; d.z-=.09f;
  quad(r,fb,g,a,b,c,d,wood,.30f);
}

static void roof_edge(Renderer *r,Image *fb,const Game *g,RoofVertex a,RoofVertex b,const Image *wood) {
  a.u=0; b.u=hypotf(b.x-a.x,b.y-a.y)*.5f; a.v=b.v=0;
  RoofVertex c=b,d=a; c.z-=.09f; d.z-=.09f; c.v=d.v=.10f;
  quad(r,fb,g,a,b,c,d,wood,.66f);
}

static void masonry_box(Renderer *r,Image *fb,const Game *g,float x,float y,float w,float h,
                        float bottom,float top,const Image *texture) {
  RoofVertex corners[]={{x,y,bottom,0,0},{x+w,y,bottom,0,0},
                        {x+w,y+h,bottom,0,0},{x,y+h,bottom,0,0}};
  for (int i=0;i<4;i++) {
    RoofVertex a=corners[i],b=corners[(i+1)%4],c=b,d=a;
    a.u=d.u=0; b.u=c.u=i%2?h:w;
    a.v=b.v=top-bottom; c.z=d.z=top;
    quad(r,fb,g,a,b,c,d,texture,i%2?.66f:.84f);
  }
  for (int i=0;i<4;i++) {
    corners[i].z=top; corners[i].u=(i==1||i==2)?w:0; corners[i].v=i>=2?h:0;
  }
  quad(r,fb,g,corners[0],corners[1],corners[2],corners[3],texture,.82f);
}

static void chimney(Renderer *r,Image *fb,const Game *g,const WorldRoof *roof) {
  if (roof->slate || roof->w<7 || roof->h<6) return;
  float x=roof->x+roof->w*(roof->axis?.36f:.28f);
  float y=roof->y+roof->h*(roof->axis?.28f:.36f);
  float across=roof->axis?fabsf(x-roof->x-roof->w*.5f):fabsf(y-roof->y-roof->h*.5f);
  float pitch=(roof->ridge-roof->eave)*2/(roof->axis?roof->w:roof->h);
  float bottom=roof->ridge-pitch*across-.2f,top=roof->ridge+.65f;
  masonry_box(r,fb,g,x-.20f,y-.24f,.40f,.48f,bottom,top,r->assets->walls[WT_BRICK]);
  masonry_box(r,fb,g,x-.27f,y-.31f,.54f,.62f,top,top+.09f,r->assets->walls[WT_STONE]);
  RoofVertex a={x-.13f,y-.17f,top+.091f,0,0},b={x+.13f,y-.17f,top+.091f,1,0};
  RoofVertex c={x+.13f,y+.17f,top+.091f,1,1},d={x-.13f,y+.17f,top+.091f,0,1};
  quad(r,fb,g,a,b,c,d,r->assets->roofs[1][SURFACE_MIPS-1],.16f);
}

void render_roofs(Renderer *r,Image *fb,const Game *g) {
  for (int i=0;i<g->level->world.roof_count;i++) {
    const WorldRoof *roof=&g->level->world.roofs[i];
    const float overhang=.24f;
    float pitch=(roof->ridge-roof->eave)*2/(roof->axis?roof->w:roof->h);
    float x=roof->x-overhang,y=roof->y-overhang,xx=roof->x+roof->w+overhang,yy=roof->y+roof->h+overhang;
    float z=roof->eave-pitch*overhang;
    RoofVertex a={x,y,z,0,0},b={xx,y,z,0,0},c={xx,yy,z,0,0},d={x,yy,z,0,0};
    RoofVertex e=roof->axis?(RoofVertex){(x+xx)*.5f,y,roof->ridge,0,0}:(RoofVertex){x,(y+yy)*.5f,roof->ridge,0,0};
    RoofVertex f=roof->axis?(RoofVertex){(x+xx)*.5f,yy,roof->ridge,0,0}:(RoofVertex){xx,(y+yy)*.5f,roof->ridge,0,0};
    const Image *texture=r->assets->roofs[roof->slate][1];
    const Image *wood=r->assets->floors[SURFACE_BARRACKS][1];
    if (roof->axis) {
      slope(r,fb,g,a,d,f,e,texture,wood,.90f);
      slope(r,fb,g,b,c,f,e,texture,wood,.70f);
      roof_edge(r,fb,g,a,d,wood); roof_edge(r,fb,g,b,c,wood);
      roof_edge(r,fb,g,a,e,wood); roof_edge(r,fb,g,e,b,wood);
      roof_edge(r,fb,g,d,f,wood); roof_edge(r,fb,g,f,c,wood);
    } else {
      slope(r,fb,g,a,b,f,e,texture,wood,.94f);
      slope(r,fb,g,d,c,f,e,texture,wood,.62f);
      roof_edge(r,fb,g,a,b,wood); roof_edge(r,fb,g,d,c,wood);
      roof_edge(r,fb,g,a,e,wood); roof_edge(r,fb,g,e,d,wood);
      roof_edge(r,fb,g,b,f,wood); roof_edge(r,fb,g,f,c,wood);
    }
    // A narrow cap gives the ridge a deliberate silhouette and covers the
    // seam between the two courses of shingles or slate.
    RoofVertex left=e,right=f,peak0=e,peak1=f;
    if (roof->axis) { left.x-=.09f; right.x-=.09f; }
    else { left.y-=.09f; right.y-=.09f; }
    left.z-=.025f; right.z-=.025f; peak0.z+=.045f; peak1.z+=.045f;
    left.u=peak0.u=0; right.u=peak1.u=hypotf(f.x-e.x,f.y-e.y)*.5f;
    left.v=right.v=.12f;
    quad(r,fb,g,left,right,peak1,peak0,texture,.95f);
    if (roof->axis) { left.x+=.18f; right.x+=.18f; }
    else { left.y+=.18f; right.y+=.18f; }
    quad(r,fb,g,left,right,peak1,peak0,texture,.70f);
    render_roof_gable(r, fb, g, roof, false);
    render_roof_gable(r, fb, g, roof, true);
    chimney(r,fb,g,roof);
  }
}
