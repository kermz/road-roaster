#pragma once
#include <Adafruit_GFX.h>
#include <algorithm>
#include <string>

// Driver boundary fake: records frames and measures the real bundled glyphs.
// It does not simulate DMA, scan timing, physical pixels, or panel power.
struct HUB75_I2S_CFG {
  struct i2s_pins { int r1,g1,b1,r2,g2,b2,a,b,c,d,e,lat,oe,clk; };
  enum { SHIFTREG };
  bool clkphase = true, double_buff = false;
  int driver = SHIFTREG;
  HUB75_I2S_CFG(int,int,int,i2s_pins) {}
};
inline bool matrix_begin_succeeds = true;
class MatrixPanel_I2S_DMA {
 public:
  unsigned frames=0, clears=0, measurements=0;
  uint8_t brightness=0, rotation=0;
  uint16_t color=0;
  int16_t x=0,y=0;
  std::string text;
  const GFXfont* font=nullptr;
  explicit MatrixPanel_I2S_DMA(const HUB75_I2S_CFG&) {}
  bool begin() { return matrix_begin_succeeds; }
  void clearScreen() { ++clears; text.clear(); }
  void flipDMABuffer() { ++frames; }
  void setRotation(uint8_t value) { rotation=value; }
  void setBrightness8(uint8_t value) { brightness=value; }
  void setFont(const GFXfont* value) { font=value; }
  void setTextWrap(bool) {}
  void setCursor(int16_t left,int16_t top) { x=left;y=top; }
  void setTextColor(uint16_t value) { color=value; }
  void print(const char* value) { text=value; }
  uint16_t color565(uint8_t r,uint8_t g,uint8_t b) const {
    return ((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3);
  }
  void getTextBounds(const char* text,int16_t x,int16_t y,int16_t* x1,
                     int16_t* y1,uint16_t* width,uint16_t* height) {
    ++measurements;
    int minx=32767,miny=32767,maxx=-32768,maxy=-32768;
    for (const auto* p=reinterpret_cast<const unsigned char*>(text);*p;++p) {
      if(*p<font->first || *p>font->last) continue;
      const auto& glyph=font->glyph[*p-font->first];
      if(glyph.width && glyph.height) {
        minx=std::min(minx,x+glyph.xOffset); miny=std::min(miny,y+glyph.yOffset);
        maxx=std::max(maxx,x+glyph.xOffset+glyph.width-1);
        maxy=std::max(maxy,y+glyph.yOffset+glyph.height-1);
      }
      x+=glyph.xAdvance;
    }
    *x1=maxx>=minx?minx:x; *y1=maxy>=miny?miny:y;
    *width=maxx>=minx?maxx-minx+1:0; *height=maxy>=miny?maxy-miny+1:0;
  }
};
