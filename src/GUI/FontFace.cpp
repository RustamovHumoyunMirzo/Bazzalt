#include "GUI/FontFace.h"
#include "GUI/CSS/CSS.h"
#include "Bazzalt/AssetManager.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <fstream>
#include <algorithm>
#include <cmath>
#include <map>
namespace Bazzalt::Runtime {
struct FontFace::Impl {
    FT_Library Library=nullptr;FT_Face Face=nullptr;std::vector<unsigned char> Bytes;float Scale=1;
    ~Impl(){if(Face)FT_Done_Face(Face);if(Library)FT_Done_FreeType(Library);}
};
FontFace::FontFace():m_impl(std::make_unique<Impl>()){}FontFace::~FontFace()=default;
bool FontFace::Load(const std::filesystem::path& path,std::uint32_t face,std::string& error){
    auto candidate=std::make_unique<Impl>();std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input){error="Cannot open font";return false;}const auto size=input.tellg();
    if(size<=0||size>32*1024*1024||face>255){error="Font exceeds size or face-index limits";return false;}
    candidate->Bytes.resize(static_cast<std::size_t>(size));input.seekg(0);input.read(reinterpret_cast<char*>(candidate->Bytes.data()),size);
    if(!input||FT_Init_FreeType(&candidate->Library)||FT_New_Memory_Face(candidate->Library,candidate->Bytes.data(),static_cast<FT_Long>(size),face,&candidate->Face)){
        error="Unsupported or invalid font (FreeType)";return false;
    }
    if(candidate->Face->num_faces>256||candidate->Face->num_glyphs>200000||FT_Select_Charmap(candidate->Face,FT_ENCODING_UNICODE)){
        error="Font has no Unicode character map or exceeds limits";return false;
    }
    m_impl=std::move(candidate);return true;
}
bool FontFace::SetSize(std::uint32_t pixels){
    auto* face=m_impl->Face;if(!face||pixels<1||pixels>512)return false;m_impl->Scale=1;
    if(!FT_Set_Pixel_Sizes(face,0,pixels))return true;
    if(face->num_fixed_sizes<=0)return false;
    int best=0;float distance=100000;
    for(int i=0;i<face->num_fixed_sizes;++i){const auto size=face->available_sizes[i].y_ppem/64.f;const auto delta=std::fabs(size-float(pixels));if(delta<distance){distance=delta;best=i;}}
    if(FT_Select_Size(face,best))return false;
    const auto strike=face->available_sizes[best].y_ppem/64.f;if(strike<=0)return false;m_impl->Scale=float(pixels)/strike;return true;
}
FontInfo FontFace::GetInfo()const{
    auto* face=m_impl->Face;if(!face)return {};
    const auto safe=[](const char* value){
        std::string raw,result;if(value)for(std::size_t i=0;i<4096&&value[i];++i)raw+=value[i];
        for(std::size_t at=0;at<raw.size();){auto cp=NextCodepoint(raw,at);
            if(cp<128)result+=char(cp);else if(cp<2048){result+=char(0xc0|(cp>>6));result+=char(0x80|(cp&63));}
            else if(cp<65536){result+=char(0xe0|(cp>>12));result+=char(0x80|((cp>>6)&63));result+=char(0x80|(cp&63));}
            else {result+=char(0xf0|(cp>>18));result+=char(0x80|((cp>>12)&63));result+=char(0x80|((cp>>6)&63));result+=char(0x80|(cp&63));}}
        return result;
    };
    return {true,safe(face->family_name),safe(face->style_name),static_cast<std::uint32_t>(face->num_faces),static_cast<std::uint32_t>(face->num_glyphs),bool(FT_IS_SCALABLE(face)),bool(FT_HAS_KERNING(face))};
}
FontGlyph FontFace::Render(std::uint32_t cp){
    FontGlyph result;auto* face=m_impl->Face;if(!face)return result;
    result.Index=FT_Get_Char_Index(face,cp);
    if(!result.Index)result.Index=FT_Get_Char_Index(face,0xfffd);
    if(FT_Load_Glyph(face,result.Index,FT_LOAD_RENDER|FT_LOAD_TARGET_NORMAL)){result.Index=0;result.Advance=std::max(1.f,GetLineHeight()*.75f);return result;}
    const auto& bitmap=face->glyph->bitmap;
    result.Advance=face->glyph->advance.x/64.f*m_impl->Scale;result.Left=float(face->glyph->bitmap_left)*m_impl->Scale;result.Top=float(face->glyph->bitmap_top)*m_impl->Scale;
    if(bitmap.width>1024||bitmap.rows>1024)return {};
    result.Width=std::uint32_t(std::ceil(bitmap.width*m_impl->Scale));result.Height=std::uint32_t(std::ceil(bitmap.rows*m_impl->Scale));if(result.Width>1024||result.Height>1024)return {};result.Alpha.resize(result.Width*result.Height);
    for(std::uint32_t y=0;y<result.Height;++y){
        const auto sy=std::min(bitmap.rows-1,std::uint32_t(y/m_impl->Scale));const auto* row=bitmap.buffer+(bitmap.pitch>=0?sy:bitmap.rows-sy-1)*std::abs(bitmap.pitch);
        for(std::uint32_t x=0;x<result.Width;++x){const auto sx=std::min(bitmap.width-1,std::uint32_t(x/m_impl->Scale));result.Alpha[y*result.Width+x]=bitmap.pixel_mode==FT_PIXEL_MODE_GRAY?row[sx]:bitmap.pixel_mode==FT_PIXEL_MODE_MONO?(row[sx/8]&(0x80>>(sx%8))?255:0):0;}
    }
    return result;
}
float FontFace::GetKerning(std::uint32_t left,std::uint32_t right)const{FT_Vector value{};if(m_impl->Face&&left&&right)FT_Get_Kerning(m_impl->Face,left,right,FT_KERNING_DEFAULT,&value);return value.x/64.f*m_impl->Scale;}
float FontFace::GetAscender()const{return m_impl->Face&&m_impl->Face->size?m_impl->Face->size->metrics.ascender/64.f*m_impl->Scale:0;}
float FontFace::GetLineHeight()const{return m_impl->Face&&m_impl->Face->size?m_impl->Face->size->metrics.height/64.f*m_impl->Scale:0;}
std::uint32_t NextCodepoint(const std::string& text,std::size_t& at){
    if(at>=text.size())return 0;auto c=static_cast<unsigned char>(text[at++]);if(c<128)return c;
    int count=c>=0xc2&&c<=0xdf?1:c>=0xe0&&c<=0xef?2:c>=0xf0&&c<=0xf4?3:0;if(!count)return 0xfffd;
    std::uint32_t cp=c&((1u<<(6-count))-1);const auto minimum=count==1?0x80u:count==2?0x800u:0x10000u;
    while(count--){if(at==text.size()||(static_cast<unsigned char>(text[at])&0xc0)!=0x80)return 0xfffd;cp=(cp<<6)|(static_cast<unsigned char>(text[at++])&63);}
    return cp<minimum||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)?0xfffd:cp;
}
bool ValidateGuiAsset(const std::filesystem::path& path,const std::string& kind,std::string& error){
    if(kind=="font"){FontFace face;return face.Load(path,0,error);}
    std::ifstream input(path,std::ios::binary|std::ios::ate);if(!input||input.tellg()<0||input.tellg()>1024*1024){error="Stylesheet exceeds size limit or cannot be read";return false;}
    std::string source(static_cast<std::size_t>(input.tellg()),'\0');input.seekg(0);input.read(source.data(),source.size());if(!input){error="Cannot read stylesheet";return false;}
    auto sheet=CSS::Parse(source);if(!sheet.HasErrors())return true;
    for(const auto& diagnostic:sheet.Diagnostics)if(diagnostic.Level==CSS::Severity::Error){error="CSS "+std::to_string(diagnostic.Source.Line)+":"+std::to_string(diagnostic.Source.Column)+": "+diagnostic.Message;break;}return false;
}
namespace {
std::shared_ptr<FontFace> OpenFont(UUID asset,std::uint32_t index){
    auto info=AssetManager::GetAsset(asset);if(!info||info->State!=AssetState::Ready||info->Importer!="Bazzalt.Font")return {};
    const auto raw=info->CachePath.u8string();const auto key=std::string(reinterpret_cast<const char*>(raw.data()),raw.size())+":"+std::to_string(index);
    static thread_local std::map<std::string,std::shared_ptr<FontFace>> cache;
    if(auto found=cache.find(key);found!=cache.end())return found->second;
    auto face=std::make_shared<FontFace>();std::string error;if(!face->Load(info->CachePath,index,error))return {};
    if(cache.size()>=8)cache.clear();cache[key]=face;return face;
}
}
FontInfo InspectFontAsset(UUID asset,std::uint32_t faceIndex){auto face=OpenFont(asset,faceIndex);return face?face->GetInfo():FontInfo{};}
Vec2 MeasureFontAsset(UUID asset,std::uint32_t faceIndex,const std::string& text,float size,float wrapWidth){
    if(!std::isfinite(size)||size<1||size>512||!std::isfinite(wrapWidth)||wrapWidth<0||text.size()>256*1024)return {};
    auto owned=OpenFont(asset,faceIndex);if(!owned||!owned->SetSize(static_cast<std::uint32_t>(std::ceil(size))))return {};auto& face=*owned;
    float x=0,y=face.GetLineHeight(),width=0;std::uint32_t previous=0;
    for(std::size_t at=0;at<text.size();){auto cp=NextCodepoint(text,at);if(cp=='\n'){width=std::max(width,x);x=0;y+=face.GetLineHeight();previous=0;continue;}if(cp<32)continue;
        auto glyph=face.Render(cp);const auto kerning=face.GetKerning(previous,glyph.Index);
        if(wrapWidth>0&&x>0&&x+kerning+glyph.Advance>wrapWidth){width=std::max(width,x);x=0;y+=face.GetLineHeight();previous=0;}
        x+=(previous?kerning:0)+glyph.Advance;previous=glyph.Index;
    }return {std::max(width,x),text.empty()?0:y};
}
FontPreviewImage PreviewFontAsset(UUID asset,std::uint32_t index,const std::string& text,float size,std::uint32_t width){
    FontPreviewImage image;if(!std::isfinite(size)||size<8||size>128||text.size()>8192||width<64||width>1024)return image;
    auto owned=OpenFont(asset,index);if(!owned||!owned->SetSize(std::uint32_t(std::ceil(size))))return image;auto& face=*owned;
    image.Width=width;image.Height=192;image.Pixels.resize(width*192*4);float x=4,y=4;std::uint32_t previous=0;
    for(std::size_t at=0;at<text.size();){auto cp=NextCodepoint(text,at);if(cp=='\n'){x=4;y+=face.GetLineHeight();previous=0;continue;}if(cp<32)continue;
        auto glyph=face.Render(cp);auto gap=face.GetKerning(previous,glyph.Index);if(x>4&&x+gap+glyph.Advance>width-4){x=4;y+=face.GetLineHeight();previous=0;gap=0;}if(y>=192)break;x+=gap;
        for(std::uint32_t row=0;row<glyph.Height;++row)for(std::uint32_t column=0;column<glyph.Width;++column){
            const int px=int(x+glyph.Left)+int(column),py=int(y+face.GetAscender()-glyph.Top)+int(row);if(px<0||py<0||px>=int(width)||py>=192)continue;
            const auto target=(py*width+px)*4;image.Pixels[target]=image.Pixels[target+1]=image.Pixels[target+2]=255;image.Pixels[target+3]=std::max(image.Pixels[target+3],glyph.Alpha[row*glyph.Width+column]);
        }x+=glyph.Advance;previous=glyph.Index;
    }return image;
}
std::vector<UUID> StylesheetReferences(const std::filesystem::path& path){
    std::ifstream input(path,std::ios::binary|std::ios::ate);if(!input||input.tellg()<0||input.tellg()>1024*1024)return {};
    std::string source(static_cast<std::size_t>(input.tellg()),'\0');input.seekg(0);input.read(source.data(),source.size());auto sheet=CSS::Parse(source);std::vector<UUID> result;
    if(sheet.HasErrors())return result;
    for(const auto& rule:sheet.Rules)for(const auto& declaration:rule.Declarations){const auto& tokens=declaration.Data.Tokens;
        for(std::size_t i=0;i+1<tokens.size();++i)if(tokens[i].Kind==CSS::TokenKind::Ident&&(tokens[i].Text=="url"||tokens[i].Text=="asset")&&tokens[i+1].Text=="("){
            std::size_t end=i+2,depth=1;for(;end<tokens.size();++end){if(tokens[end].Text=="(")++depth;else if(tokens[end].Text==")"&&!--depth)break;}if(end==tokens.size())continue;
            auto text=CSS::ResolveReference(CSS::Value{{tokens.begin()+i,tokens.begin()+end+1}});UUID id;
            if(text){if(text->starts_with("asset://"))text->erase(0,8);if(UUID::TryParse(*text,id)&&id&&std::find(result.begin(),result.end(),id)==result.end())result.push_back(id);}
        }
    }return result;
}
}
