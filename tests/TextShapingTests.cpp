#include "GUI/FontFace.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
using namespace Bazzalt;
using namespace Bazzalt::Runtime;
int main(){
    std::filesystem::path font;
    for(const auto& candidate:{"C:/Windows/Fonts/arial.ttf","/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf","/System/Library/Fonts/Supplemental/Arial.ttf"})if(std::filesystem::is_regular_file(candidate)){font=candidate;break;}
    if(font.empty()){std::cerr<<"A multilingual test font is required\n";return 1;}
    FontFace face;std::string error;assert(face.Load(font,0,error)&&face.SetSize(32));
    const auto arabic=face.Layout("\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85"); // salaam, contextual joining + lam-alef
    assert(arabic.Glyphs.size()>=3&&arabic.Glyphs.size()<=4&&arabic.Size.X>0);
    bool joined=false;for(const auto& glyph:arabic.Glyphs){assert(glyph.Index!=0);joined|=glyph.Index!=face.Render(0x633).Index&&glyph.Index!=face.Render(0x644).Index&&glyph.Index!=face.Render(0x627).Index&&glyph.Index!=face.Render(0x645).Index;}
    assert(joined&&arabic.Glyphs.front().Cluster>arabic.Glyphs.back().Cluster);
    const auto mixed=face.Layout("ABC \xD7\x90\xD7\x91\xD7\x92 123");bool reversed=false;
    for(std::size_t i=1;i<mixed.Glyphs.size();++i)reversed|=mixed.Glyphs[i].Cluster<mixed.Glyphs[i-1].Cluster;assert(reversed);
    const auto marks=face.Layout("a\xCC\x81\xCC\xA3");for(const auto& glyph:marks.Glyphs)assert(glyph.Cluster==0);
    const auto wrapped=face.Layout("a\xCC\x81\xCC\xA3" "b",1);assert(wrapped.Glyphs.size()>marks.Glyphs.size());
    for(std::size_t i=0;i<marks.Glyphs.size();++i)assert(wrapped.Glyphs[i].Cluster==0&&wrapped.Glyphs[i].Y==marks.Glyphs[i].Y);
    assert(wrapped.Size.Y>marks.Size.Y);
    auto noKern=face.Layout("AV",0,0,1,false),kern=face.Layout("AV");assert(kern.Size.X<=noKern.Size.X);
    const auto newline=face.Layout("A\nB");assert(newline.Size.Y==2*face.GetLineHeight());assert(face.Layout("A\n").Size.Y==2*face.GetLineHeight());
    assert(face.Layout("").Glyphs.empty());assert(face.Layout(std::string(262145,'x')).Glyphs.empty());
    const auto invalid=face.Layout("\xff\xc0");assert(std::isfinite(invalid.Size.X));
    // Indic reordering and conjunct clusters, using a test-only system font.
    for(const auto& candidate:{"C:/Windows/Fonts/Nirmala.ttf","/usr/share/fonts/truetype/noto/NotoSansDevanagari-Regular.ttf","/System/Library/Fonts/Supplemental/Devanagari Sangam MN.ttc"})if(std::filesystem::is_regular_file(candidate)){
        FontFace indic;assert(indic.Load(candidate,0,error)&&indic.SetSize(32));
        auto syllable=indic.Layout("\xE0\xA4\x95\xE0\xA4\xBF"); // ki: pre-base vowel sign
        assert(!syllable.Glyphs.empty());for(const auto& glyph:syllable.Glyphs)assert(glyph.Index!=0&&glyph.Cluster==0);
        auto conjunct=indic.Layout("\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7");assert(!conjunct.Glyphs.empty());for(const auto& glyph:conjunct.Glyphs)assert(glyph.Index!=0&&glyph.Cluster==0);break;
    }
    for(const auto& glyph:arabic.Glyphs){auto bitmap=face.RenderGlyph(glyph.Index);assert(bitmap.Index==glyph.Index&&bitmap.Width>0&&!bitmap.Alpha.empty());}
    std::cout<<"Arabic joining, mixed bidi, combining clusters, wrapping and glyph-index rasterization passed\n";
}
