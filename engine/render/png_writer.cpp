#include "png_writer.h"
#include <png.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace sky {

bool write_png(const std::string& path, int w, int h, const uint8_t* rgb) {
  FILE* fp = std::fopen(path.c_str(), "wb");
  if(!fp) return false;
  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if(!png){ std::fclose(fp); return false; }
  png_infop info = png_create_info_struct(png);
  if(!info){ png_destroy_write_struct(&png, nullptr); std::fclose(fp); return false; }
  if(setjmp(png_jmpbuf(png))){ png_destroy_write_struct(&png,&info); std::fclose(fp); return false; }
  png_init_io(png, fp);
  png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png, info);
  std::vector<png_bytep> rows(h);
  for(int y=0;y<h;y++) rows[y] = const_cast<png_bytep>(rgb + (size_t)y*w*3);
  png_write_image(png, rows.data());
  png_write_end(png, info);
  png_destroy_write_struct(&png, &info);
  std::fclose(fp);
  return true;
}

} // namespace sky
