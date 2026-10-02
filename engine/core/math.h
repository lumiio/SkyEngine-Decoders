// SkyEngine core math: Vec2/Vec3/Vec4/Mat4/Color
#pragma once
#include <cmath>
#include <cstring>
#include <algorithm>

namespace sky {

struct Vec4;

struct Vec2 { float x=0,y=0;
  Vec2()=default; Vec2(float a,float b):x(a),y(b){}
  Vec2 operator+(const Vec2&o)const{return{x+o.x,y+o.y};}
  Vec2 operator-(const Vec2&o)const{return{x-o.x,y-o.y};}
  Vec2 operator*(float s)const{return{x*s,y*s};}
};
struct Vec3 { float x=0,y=0,z=0;
  Vec3()=default; Vec3(float a,float b,float c):x(a),y(b),z(c){}
  explicit Vec3(const Vec4& v);
  Vec3 operator+(const Vec3&o)const{return{x+o.x,y+o.y,z+o.z};}
  Vec3 operator-(const Vec3&o)const{return{x-o.x,y-o.y,z-o.z};}
  Vec3 operator*(float s)const{return{x*s,y*s,z*s};}
  Vec3& operator+=(const Vec3&o){x+=o.x;y+=o.y;z+=o.z;return *this;}
  float dot(const Vec3&o)const{return x*o.x+y*o.y+z*o.z;}
  Vec3 cross(const Vec3&o)const{return{y*o.z-z*o.y,z*o.x-x*o.z,x*o.y-y*o.x};}
  float len()const{return std::sqrt(dot(*this));}
  Vec3 norm()const{float l=len();return l>1e-8f?(*this)*(1.f/l):Vec3(0,1,0);}
  float sq()const{return dot(*this);}
};
inline Vec3 operator*(float s,const Vec3&v){return v*s;}
struct Vec4 { float x=0,y=0,z=0,w=0;
  Vec4()=default; Vec4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}
  Vec4(const Vec3& v, float ww):x(v.x),y(v.y),z(v.z),w(ww){}
};
inline Vec3::Vec3(const Vec4& v):x(v.x),y(v.y),z(v.z){}

struct Mat4 {
  float m[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; // column-major
  static Mat4 identity(){ return Mat4(); }
  static Mat4 perspective(float fovY, float aspect, float zn, float zf) {
    Mat4 r; float t=1.f/std::tan(fovY*0.5f);
    r.m[0]=t/aspect; r.m[5]=t; r.m[10]=(zf+zn)/(zn-zf); r.m[11]=-1.f; r.m[14]=2.f*zf*zn/(zn-zf); r.m[15]=0;
    return r;
  }
  static Mat4 lookAt(const Vec3& eye, const Vec3& at, const Vec3& up) {
    Vec3 f=(at-eye).norm(), s=f.cross(up).norm(), u=s.cross(f);
    Mat4 r;
    r.m[0]=s.x; r.m[4]=s.y; r.m[8]=s.z;
    r.m[1]=u.x; r.m[5]=u.y; r.m[9]=u.z;
    r.m[2]=-f.x; r.m[6]=-f.y; r.m[10]=-f.z;
    r.m[12]=-s.dot(eye); r.m[13]=-u.dot(eye); r.m[14]=f.dot(eye);
    return r;
  }
  static Mat4 translate(const Vec3& t) {
    Mat4 r; r.m[12]=t.x; r.m[13]=t.y; r.m[14]=t.z; return r;
  }
  static Mat4 scale(const Vec3& s) {
    Mat4 r; r.m[0]=s.x; r.m[5]=s.y; r.m[10]=s.z; return r;
  }
  static Mat4 rotateX(float a){Mat4 r;float c=std::cos(a),s=std::sin(a);r.m[5]=c;r.m[6]=s;r.m[9]=-s;r.m[10]=c;return r;}
  static Mat4 rotateY(float a){Mat4 r;float c=std::cos(a),s=std::sin(a);r.m[0]=c;r.m[2]=-s;r.m[8]=s;r.m[10]=c;return r;}
  static Mat4 rotateZ(float a){Mat4 r;float c=std::cos(a),s=std::sin(a);r.m[0]=c;r.m[1]=s;r.m[4]=-s;r.m[5]=c;return r;}
  Vec4 operator*(const Vec4& v) const {
    return { m[0]*v.x+m[4]*v.y+m[8]*v.z+m[12]*v.w,
             m[1]*v.x+m[5]*v.y+m[9]*v.z+m[13]*v.w,
             m[2]*v.x+m[6]*v.y+m[10]*v.z+m[14]*v.w,
             m[3]*v.x+m[7]*v.y+m[11]*v.z+m[15]*v.w };
  }
  Mat4 operator*(const Mat4& b) const {
    Mat4 r;
    for(int c=0;c<4;c++) for(int rr=0;rr<4;rr++) {
      float s=0;
      for(int k=0;k<4;k++) s+=m[k*4+rr]*b.m[c*4+k];
      r.m[c*4+rr]=s;
    }
    return r;
  }
};

struct Color {
  float r=0,g=0,b=0,a=1;
  Color()=default;
  Color(float R,float G,float B,float A=1):r(R),g(G),b(B),a(A){}
  Color operator*(float s) const { return {r*s,g*s,b*s,a}; }
  Color operator+(const Color& o) const { return {r+o.r,g+o.g,b+o.b,a}; }
  operator Vec3() const { return {r,g,b}; }
  static Color fromHex(unsigned h){return Color(((h>>16)&255)/255.f,((h>>8)&255)/255.f,(h&255)/255.f);}
};

// 简化 ACES 近似色调映射（HDR->LDR），光遇式柔和高光
inline float tonemap(float x){ return (x*(2.51f*x+0.03f))/(x*(2.43f*x+0.59f)+0.14f); }
inline Vec3 tonemap(const Vec3& c){ return { tonemap(c.x), tonemap(c.y), tonemap(c.z) }; }
inline Vec3 gamma(const Vec3& c, float g=1.f/2.2f){ return { std::pow(std::max(c.x,0.f),g), std::pow(std::max(c.y,0.f),g), std::pow(std::max(c.z,0.f),g) }; }

inline float clampf(float x,float lo,float hi){ return x<lo?lo:(x>hi?hi:x); }
inline float lerp(float a,float b,float t){ return a+(b-a)*t; }
inline Vec3 lerp(const Vec3& a,const Vec3& b,float t){ return a*(1.f-t)+b*t; }
inline float smoothstep(float e0,float e1,float x){ float t=clampf((x-e0)/(e1-e0),0,1); return t*t*(3-2*t); }
inline float frac(float x){ return x-std::floor(x); }

// 简单确定性噪声（供云/地形），2D 值噪声 + fbm
namespace noise {
  inline float hash(float x,float y){ return frac(std::sin(x*127.1f+y*311.7f)*43758.5453f); }
  inline float vnoise(float x,float y){
    int ix=std::floor(x), iy=std::floor(y);
    float fx=x-ix, fy=y-iy;
    float a=hash(ix,iy), b=hash(ix+1,iy), c=hash(ix,iy+1), d=hash(ix+1,iy+1);
    float ux=fx*fx*(3-2*fx), uy=fy*fy*(3-2*fy);
    return lerp(lerp(a,b,ux),lerp(c,d,ux),uy);
  }
  inline float fbm(float x,float y,int oct=4){
    float s=0, amp=0.5f, f=1.f;
    for(int i=0;i<oct;i++){ s+=amp*vnoise(x*f,y*f); amp*=0.5f; f*=2.f; }
    return s;
  }
}

} // namespace sky
