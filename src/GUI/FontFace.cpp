#include "GUI/FontFace.h"
#include "GUI/CSS/CSS.h"
#include "Bazzalt/AssetManager.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>
#include <SheenBidi/SheenBidi.h>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
namespace Bazzalt::Runtime {
struct FontFace::Impl {
    FT_Library Library=nullptr;FT_Face Face=nullptr;hb_font_t* Shaper=nullptr;std::vector<unsigned char> Bytes;float Scale=1;std::uint32_t Pixels=0;
    using LayoutKey=std::tuple<std::string,float,float,float,bool>;
    std::map<LayoutKey,ShapedText> Layouts;
    ~Impl(){if(Shaper)hb_font_destroy(Shaper);if(Face)FT_Done_Face(Face);if(Library)FT_Done_FreeType(Library);}
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
    auto* face=m_impl->Face;if(!face||pixels<1||pixels>512)return false;if(m_impl->Pixels==pixels)return true;m_impl->Scale=1;m_impl->Pixels=0;m_impl->Layouts.clear();
    if(!FT_Set_Pixel_Sizes(face,0,pixels)){m_impl->Pixels=pixels;return true;}
    if(face->num_fixed_sizes<=0)return false;
    int best=0;float distance=100000;
    for(int i=0;i<face->num_fixed_sizes;++i){const auto size=face->available_sizes[i].y_ppem/64.f;const auto delta=std::fabs(size-float(pixels));if(delta<distance){distance=delta;best=i;}}
    if(FT_Select_Size(face,best))return false;
    const auto strike=face->available_sizes[best].y_ppem/64.f;if(strike<=0)return false;m_impl->Scale=float(pixels)/strike;m_impl->Pixels=pixels;return true;
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
    auto* face=m_impl->Face;if(!face)return {};
    auto index=FT_Get_Char_Index(face,cp);if(!index)index=FT_Get_Char_Index(face,0xfffd);
    return RenderGlyph(index);
}
FontGlyph FontFace::RenderGlyph(std::uint32_t index){
    FontGlyph result;auto* face=m_impl->Face;if(!face)return result;result.Index=index;
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
ShapedText FontFace::Layout(const std::string& text,float wrapWidth,float letterSpacing,float lineSpacing,bool kerning){
    ShapedText result;
    if(!m_impl->Face||!m_impl->Face->size||text.empty()||text.size()>256*1024||!std::isfinite(wrapWidth)||wrapWidth<0||!std::isfinite(letterSpacing)||std::fabs(letterSpacing)>1000||!std::isfinite(lineSpacing)||lineSpacing<.1f||lineSpacing>10)return result;
    Impl::LayoutKey key{text,wrapWidth,letterSpacing,lineSpacing,kerning};
    if(auto found=m_impl->Layouts.find(key);found!=m_impl->Layouts.end())return found->second;
    std::vector<std::uint32_t> codepoints;
    for(std::size_t at=0;at<text.size();){auto cp=NextCodepoint(text,at);if(cp=='\r'){if(at<text.size()&&text[at]=='\n')++at;cp='\n';}if(cp=='\t')cp=' ';codepoints.push_back(cp);}
    if(codepoints.size()>65536)return result;
    if(!m_impl->Shaper)m_impl->Shaper=hb_ft_font_create_referenced(m_impl->Face);
    hb_ft_font_changed(m_impl->Shaper);hb_ft_font_set_load_flags(m_impl->Shaper,FT_LOAD_DEFAULT|FT_LOAD_TARGET_NORMAL);
    const float scale=m_impl->Scale/64.f,lineHeight=std::max(1.f,GetLineHeight())*lineSpacing;
    SBCodepointSequence sequence{SBStringEncodingUTF32,codepoints.data(),codepoints.size()};
    auto* algorithm=SBAlgorithmCreate(&sequence);if(!algorithm)return result;
    struct AlgorithmOwner{SBAlgorithmRef Value;~AlgorithmOwner(){SBAlgorithmRelease(Value);}} algorithmOwner{algorithm};
    auto* unicode=hb_unicode_funcs_get_default();
    // Resolve neutral script characters from neighboring strong scripts, within bidi runs.
    const auto neutral=[](hb_script_t script){return script==HB_SCRIPT_COMMON||script==HB_SCRIPT_INHERITED||script==HB_SCRIPT_UNKNOWN;};
    float y=0;
    for(std::size_t paragraphOffset=0;paragraphOffset<codepoints.size();){
        auto* paragraph=SBAlgorithmCreateParagraph(algorithm,paragraphOffset,codepoints.size()-paragraphOffset,SBLevelDefaultLTR);if(!paragraph)break;
        struct ParagraphOwner{SBParagraphRef Value;~ParagraphOwner(){SBParagraphRelease(Value);}} paragraphOwner{paragraph};
        const auto paragraphLength=std::size_t(SBParagraphGetLength(paragraph));if(!paragraphLength)break;
        auto contentEnd=paragraphOffset+paragraphLength;
        while(contentEnd>paragraphOffset&&(codepoints[contentEnd-1]=='\n'||codepoints[contentEnd-1]==0x2029||codepoints[contentEnd-1]==0x2028))--contentEnd;
        const auto shapeLine=[&](std::size_t begin,std::size_t end){
            std::vector<ShapedGlyph> glyphs;
            if(begin==end)return glyphs;
            auto* line=SBParagraphCreateLine(paragraph,begin,end-begin);if(!line)return glyphs;
            struct LineOwner{SBLineRef Value;~LineOwner(){SBLineRelease(Value);}} lineOwner{line};
            float pen=0;const auto* runs=SBLineGetRunsPtr(line);
            for(SBUInteger r=0;r<SBLineGetRunCount(line);++r){
                const auto& run=runs[r];const auto start=std::size_t(run.offset),length=std::size_t(run.length);
                std::vector<hb_script_t> scripts(length);hb_script_t previous=HB_SCRIPT_UNKNOWN;
                for(std::size_t i=0;i<length;++i){auto script=hb_unicode_script(unicode,codepoints[start+i]);if(neutral(script)&&!neutral(previous))script=previous;scripts[i]=script;if(!neutral(script))previous=script;}
                previous=HB_SCRIPT_UNKNOWN;for(std::size_t i=length;i-->0;){if(neutral(scripts[i])&&!neutral(previous))scripts[i]=previous;if(!neutral(scripts[i]))previous=scripts[i];}
                std::vector<std::pair<std::size_t,std::size_t>> items;
                for(std::size_t i=0;i<length;){auto endItem=i+1;while(endItem<length&&scripts[endItem]==scripts[i])++endItem;items.emplace_back(i,endItem);i=endItem;}
                if(run.level&1)std::reverse(items.begin(),items.end());
                for(const auto& [first,last]:items){
                    auto* buffer=hb_buffer_create();hb_buffer_set_direction(buffer,run.level&1?HB_DIRECTION_RTL:HB_DIRECTION_LTR);
                    hb_buffer_set_script(buffer,scripts[first]);hb_buffer_set_language(buffer,hb_language_from_string("und",-1));
                    hb_buffer_set_cluster_level(buffer,HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
                    hb_buffer_add_utf32(buffer,codepoints.data()+begin,int(end-begin),unsigned(start+first-begin),int(last-first));
                    hb_feature_t feature{HB_TAG('k','e','r','n'),kerning?1u:0u,0,~0u};hb_shape(m_impl->Shaper,buffer,&feature,1);
                    unsigned count=0;const auto* info=hb_buffer_get_glyph_infos(buffer,&count);const auto* positions=hb_buffer_get_glyph_positions(buffer,nullptr);
                    if(!hb_buffer_allocation_successful(buffer)||glyphs.size()+count>262144){hb_buffer_destroy(buffer);return std::vector<ShapedGlyph>{};}
                    for(unsigned i=0;i<count;++i){
                        const bool clusterEnd=i+1==count||info[i+1].cluster!=info[i].cluster;
                        const auto advance=positions[i].x_advance*scale+(clusterEnd?letterSpacing:0.f);
                        glyphs.push_back({info[i].codepoint,std::uint32_t(info[i].cluster+begin),pen+positions[i].x_offset*scale,-positions[i].y_offset*scale,advance});pen+=advance;
                    }
                    hb_buffer_destroy(buffer);
                }
            }return glyphs;
        };
        const auto full=shapeLine(paragraphOffset,contentEnd);
        // Accumulate advances by logical grapheme/ligature cluster. Never break inside it.
        std::map<std::size_t,float> clusters;for(const auto& glyph:full)clusters[glyph.Cluster]+=glyph.Advance;
        std::vector<std::pair<std::size_t,std::size_t>> lines;
        std::size_t lineStart=paragraphOffset;float width=0;
        for(const auto& [cluster,advance]:clusters){if(wrapWidth>0&&cluster>lineStart&&width+advance>wrapWidth){lines.emplace_back(lineStart,cluster);lineStart=cluster;width=0;}width+=advance;}
        lines.emplace_back(lineStart,contentEnd);
        for(const auto& [begin,end]:lines){auto glyphs=lines.size()==1?full:shapeLine(begin,end);float lineWidth=0;
            for(auto& glyph:glyphs){lineWidth+=glyph.Advance;glyph.Y+=y;glyph.Line=result.LineWidths.size();result.Glyphs.push_back(glyph);}result.LineWidths.push_back(lineWidth);
            result.Size.X=std::max(result.Size.X,lineWidth);y+=lineHeight;
        }
        paragraphOffset+=paragraphLength;
        if(paragraphOffset==codepoints.size()&&contentEnd<paragraphOffset)y+=lineHeight;
    }
    result.Size.Y=y;if(text.size()<=8192){if(m_impl->Layouts.size()>=4)m_impl->Layouts.clear();m_impl->Layouts.emplace(std::move(key),result);}return result;
}
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
    return face.Layout(text,wrapWidth).Size;
}
FontPreviewImage PreviewFontAsset(UUID asset,std::uint32_t index,const std::string& text,float size,std::uint32_t width){
    FontPreviewImage image;if(!std::isfinite(size)||size<8||size>128||text.size()>8192||width<64||width>1024)return image;
    auto owned=OpenFont(asset,index);if(!owned||!owned->SetSize(std::uint32_t(std::ceil(size))))return image;auto& face=*owned;
    image.Width=width;image.Height=192;image.Pixels.resize(width*192*4);
    const auto shaped=face.Layout(text,float(width-8));
    for(const auto& placed:shaped.Glyphs){
        if(placed.Y>=192)continue;auto glyph=face.RenderGlyph(placed.Index);
        for(std::uint32_t row=0;row<glyph.Height;++row)for(std::uint32_t column=0;column<glyph.Width;++column){
            const int px=int(4+placed.X+glyph.Left)+int(column),py=int(4+placed.Y+face.GetAscender()-glyph.Top)+int(row);if(px<0||py<0||px>=int(width)||py>=192)continue;
            const auto target=(py*width+px)*4;image.Pixels[target]=image.Pixels[target+1]=image.Pixels[target+2]=255;image.Pixels[target+3]=std::max(image.Pixels[target+3],glyph.Alpha[row*glyph.Width+column]);
        }
    }return image;
}
std::vector<UUID> StylesheetReferences(const std::filesystem::path& path){
    std::ifstream input(path,std::ios::binary|std::ios::ate);if(!input||input.tellg()<0||input.tellg()>1024*1024)return {};
    std::string source(static_cast<std::size_t>(input.tellg()),'\0');input.seekg(0);input.read(source.data(),source.size());auto parsed=CSS::Parse(source);auto sheet=CSS::BindRequirements(parsed);CSS::Rule defaults;for(const auto& requirement:parsed.Requirements)defaults.Declarations.push_back({requirement.Name,requirement.Default});for(const auto& [name,frames]:sheet.Keyframes)for(const auto& frame:frames)for(const auto& declaration:frame.Declarations)defaults.Declarations.push_back(declaration);sheet.Rules.push_back(std::move(defaults));std::vector<UUID> result;
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
