#include <cstdio>
#include <vector>
#include <cstdint>
#include <cstring>
static uint32_t rd32(const uint8_t* p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
// 模仿 meshopt_decodeIndexBuffer 但头检查为 (b0&0xf0)==0xb0
static int decodeIdx(void* dest, size_t count, const unsigned char* buffer, size_t size){
  if ((buffer[0] & 0xf0) != 0xb0) return -1;
  int version = buffer[0] & 0x0f;
  if (version > 1) return -1;
  unsigned short* tri = (unsigned short*)dest;
  const unsigned char* code = buffer + 1;
  const unsigned char* data = code + count / 3;
  const unsigned char* data_safe_end = buffer + size - 16;
  const unsigned char* codeaux_table = data_safe_end;
  int edgefifo[16][2]; memset(edgefifo, -1, sizeof(edgefifo));
  int vertexfifo[16]; memset(vertexfifo, -1, sizeof(vertexfifo));
  size_t edgefifooffset=0, vertexfifooffset=0;
  unsigned int next=0, last=0;
  int fecmax = version >= 1 ? 13 : 15;
  auto decodeIndex = [&](const unsigned char*& d, unsigned int last) -> unsigned int {
    unsigned int v = 0, shift = 0;
    unsigned char b;
    do { b = *d++; v += (b & 0x7f) << shift; shift += 7; } while (b >= 0x80);
    return last + (v >> 1) ^ -(int)(v & 1);
  };
  for (size_t i = 0; i < count; i += 3){
    if (data > data_safe_end) return -2;
    unsigned char codetri = *code++;
    if (codetri < 0xf0){
      int fe = codetri >> 4;
      unsigned int a = edgefifo[(edgefifooffset - 1 - fe) & 15][0];
      unsigned int b = edgefifo[(edgefifooffset - 1 - fe) & 15][1];
      int fec = codetri & 15;
      if (fec < fecmax){
        unsigned int cf = vertexfifo[(vertexfifooffset - 1 - fec) & 15];
        unsigned int c = (fec == 0) ? next : cf;
        next += (fec == 0);
        tri[i]=a; tri[i+1]=b; tri[i+2]=c;
        vertexfifo[vertexfifooffset & 15] = c; vertexfifooffset += (fec == 0);
        edgefifo[edgefifooffset & 15][0] = c; edgefifo[edgefifooffset & 15][1] = b; edgefifooffset++;
        edgefifo[edgefifooffset & 15][0] = a; edgefifo[edgefifooffset & 15][1] = c; edgefifooffset++;
      } else {
        unsigned int c;
        if (fec != 15) c = last + (fec - (fec ^ 3));
        else { last = c = decodeIndex(data, last); }
        tri[i]=a; tri[i+1]=b; tri[i+2]=c;
        vertexfifo[vertexfifooffset & 15] = c; vertexfifooffset++;
        edgefifo[edgefifooffset & 15][0] = c; edgefifo[edgefifooffset & 15][1] = b; edgefifooffset++;
        edgefifo[edgefifooffset & 15][0] = a; edgefifo[edgefifooffset & 15][1] = c; edgefifooffset++;
      }
    } else {
      if (codetri < 0xfe){
        unsigned char codeaux = codeaux_table[codetri & 15];
        int feb = codeaux >> 4, fec = codeaux & 15;
        unsigned int a = edgefifo[(edgefifooffset - 1 - feb) & 15][0];
        unsigned int b = edgefifo[(edgefifooffset - 1 - feb) & 15][1];
        if (fec < fecmax){
          unsigned int c = (fec == 0) ? next : vertexfifo[(vertexfifooffset - 1 - fec) & 15];
          next += (fec == 0);
          tri[i]=a; tri[i+1]=b; tri[i+2]=c;
          vertexfifo[vertexfifooffset & 15] = c; vertexfifooffset += (fec == 0);
          edgefifo[edgefifooffset & 15][0] = c; edgefifo[edgefifooffset & 15][1] = b; edgefifooffset++;
          edgefifo[edgefifooffset & 15][0] = a; edgefifo[edgefifooffset & 15][1] = c; edgefifooffset++;
        } else {
          unsigned int c;
          if (fec != 15) c = last + (fec - (fec ^ 3));
          else { last = c = decodeIndex(data, last); }
          tri[i]=a; tri[i+1]=b; tri[i+2]=c;
          vertexfifo[vertexfifooffset & 15] = c; vertexfifooffset++;
          edgefifo[edgefifooffset & 15][0] = c; edgefifo[edgefifooffset & 15][1] = b; edgefifooffset++;
          edgefifo[edgefifooffset & 15][0] = a; edgefifo[edgefifooffset & 15][1] = c; edgefifooffset++;
        }
      } else {
        // 新边索引进来了
        unsigned int a = decodeIndex(data, last);
        unsigned int b = decodeIndex(data, a);
        unsigned int c = decodeIndex(data, b);
        last = c;
        tri[i]=a; tri[i+1]=b; tri[i+2]=c;
        vertexfifo[vertexfifooffset & 15] = a; vertexfifooffset++;
        vertexfifo[vertexfifooffset & 15] = b; vertexfifooffset++;
        vertexfifo[vertexfifooffset & 15] = c; vertexfifooffset++;
        edgefifo[edgefifooffset & 15][0] = c; edgefifo[edgefifooffset & 15][1] = b; edgefifooffset++;
        edgefifo[edgefifooffset & 15][0] = a; edgefifo[edgefifooffset & 15][1] = c; edgefifooffset++;
      }
    }
  }
  return 0;
}
int main(int argc,char**argv){
  FILE* f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
  std::vector<uint8_t> buf(sz); fread(buf.data(),1,sz,f); fclose(f);
  uint32_t n=rd32(buf.data()+8);
  uint32_t lod0off=0,lod0len=0;
  for(uint32_t i=0;i<n;i++){
    const uint8_t* s=buf.data()+12+i*12;
    if(s[0]=='L'&&s[1]=='O'&&s[2]=='D'&&s[3]=='0'){ lod0off=rd32(s+4); lod0len=rd32(s+8); }
  }
  const uint8_t* d=buf.data()+lod0off;
  const uint8_t* idx=d+0x14e; // 索引进来了 362B
  uint16_t tri[1024];
  for(int count=96;count<=102;count+=3){
    int r=decodeIdx(tri,count,idx,362);
    printf("count=%d r=%d\n",count,r);
    if(r==0){
      int minv=0xffff,maxv=0;
      for(int i=0;i<count;i++){ if(tri[i]<minv)minv=tri[i]; if(tri[i]>maxv)maxv=tri[i]; }
      printf("  索引进来了范围 %u-%u\n",minv,maxv);
      for(int i=0;i<count && i<30;i+=3) printf("  tri%d: %u %u %u\n",i/3,tri[i],tri[i+1],tri[i+2]);
    }
  }
  return 0;
}
