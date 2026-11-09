// Included in AssetDatabase.cpp: import helpers and bounded YAML parsing are shared.
namespace {
constexpr std::uintmax_t MaxDependencyBytes=64u*1024u*1024u;
std::string DependencyText(const std::filesystem::path& path){
    std::error_code ec;const auto size=std::filesystem::file_size(path,ec);
    if(ec||size>MaxDependencyBytes)throw std::runtime_error("Cannot read dependency source or source exceeds 64 MiB");
    std::ifstream stream(path,std::ios::binary);std::string text(static_cast<std::size_t>(size),'\0');
    if(!stream.read(text.data(),static_cast<std::streamsize>(text.size()))||stream.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Cannot read dependency source or source changed during analysis");return text;
}
bool BuiltinReference(UUID id){return id.GetHigh()==0xba22a17000000002ULL&&id.GetLow()>=1&&id.GetLow()<=5;}
void SortDependencies(std::vector<AssetDependency>& refs){
    std::sort(refs.begin(),refs.end(),[](const auto& a,const auto& b){return std::tie(a.Target,a.TargetPath,a.Field)<std::tie(b.Target,b.TargetPath,b.Field);});
    refs.erase(std::unique(refs.begin(),refs.end(),[](const auto& a,const auto& b){return a.Target==b.Target&&a.TargetPath==b.TargetPath&&a.Field==b.Field;}),refs.end());
}
std::vector<UUID> WalkDependencies(UUID start,bool recursive,const std::function<std::vector<UUID>(UUID)>& adjacent){
    std::unordered_set<UUID> visited{start};std::vector<UUID> pending{start},result;
    for(std::size_t cursor=0;cursor<pending.size();++cursor){for(auto id:adjacent(pending[cursor]))if(id&&visited.insert(id).second){result.push_back(id);if(recursive)pending.push_back(id);}if(!recursive)break;}
    std::sort(result.begin(),result.end());return result;
}
}
void AssetDatabase::ScanDependencies(UUID id){
    auto asset=Find(id);if(!asset)return;
    const auto key=PathUtf8(asset->SourcePath)+"\n"+PathUtf8(asset->CachePath)+"\n"+asset->Importer+"\n"+std::to_string(asset->ImporterVersion);
    if(asset->State==AssetState::Ready){auto previous=m_dependencyKeys.find(id);if(previous!=m_dependencyKeys.end()&&previous->second==key&&m_dependencies.contains(id))return;}
    AssetDependencyReport report;report.Asset=id;
    try{
        auto* importer=SelectImporter(asset->SourcePath,asset->Importer);
        const bool supplied=importer&&importer->CollectDependencies(*asset,report);
        report.Asset=id;
        const auto add=[&](const std::string& value,const std::string& field){UUID target;if(UUID::TryParse(value,target)&&target)report.References.push_back({id,target,{},field});};
        if(!supplied){
            const auto extension=LowerExtension(asset->SourcePath);
            if(extension==".gltf"){
                std::vector<std::filesystem::path> paths;std::string error;
                if(!CollectGltfUris(asset->SourcePath,paths,error))throw std::runtime_error(error);
                for(auto& path:paths)report.References.push_back({id,{},path,"uri"});
                report.State=AssetDependencyScanState::Complete;
            }else if(asset->Importer=="Bazzalt.Stylesheet"){
                if(asset->State!=AssetState::Ready)throw std::runtime_error("Stylesheet dependencies require a successful import");
                auto file=asset->CachePath;file+=".dependencies.yml";auto text=DependencyText(file);
                auto tree=ryml::parse_in_arena(ryml::csubstr(text.data(),text.size()));
                for(auto node:tree.crootref().children())add(Text(node),"Stylesheet");
                report.State=AssetDependencyScanState::Complete;
            }else if(extension==".bscene"||extension==".bprefab"||extension==".matinst"||extension==".shadg"||extension==".vfx"||extension==".banim"||extension==".btimeline"){
                // Unknown/custom component properties may have user-defined meaning.
                report.State=extension==".matinst"?AssetDependencyScanState::Complete:AssetDependencyScanState::Partial;
                auto text=DependencyText(asset->SourcePath);auto tree=ryml::parse_in_arena(ryml::csubstr(text.data(),text.size()));
                if(tree.size()>1000000)throw std::runtime_error("Dependency source has too many nodes");
                if(extension==".matinst"&&(!tree.crootref().is_map()||!tree.crootref().has_child("shader")||!tree.crootref()["shader"].has_val()))throw std::runtime_error("Material instance has no shader reference");
                struct Entry{ryml::ConstNodeRef Node;std::string Field,Component;};
                std::vector<Entry> stack{{tree.crootref(),"", ""}};
                while(!stack.empty()){
                    auto entry=stack.back();stack.pop_back();auto node=entry.Node;
                    auto component=entry.Component;
                    if(node.is_map()&&node.has_child("Properties")&&node.has_child("Type")&&node["Type"].has_val())component=Text(node["Type"]);
                    std::size_t childIndex=0;
                    for(auto child:node.children()){
                        const auto key=child.has_key()?Key(child):std::to_string(childIndex);++childIndex;
                        auto field=entry.Field.empty()?key:entry.Field+"/"+key;
                        if(field.size()>4096)throw std::runtime_error("Dependency field path exceeds 4096 bytes");
                        if(child.has_children()){stack.push_back({child,field,component});continue;}
                        if(!child.has_val())continue;const auto value=Text(child);
                        if(extension==".banim"&&key=="sourceModel")add(value,field);
                        if(extension==".btimeline"&&key=="event")add(value,field);
                        if((extension==".shadg"&&key=="texture")||(extension==".vfx"&&(key=="asset"||key=="material"||key=="texture"||key=="mesh")))add(value,field);
                        if(extension==".matinst"){
                            if(key=="shader"||entry.Field=="properties")add(value,field);
                            continue;
                        }
                        const bool environment=entry.Field=="Environment"&&(key=="Source"||key=="Material");
                        const bool named=((component=="Bazzalt.Mesh"||component=="Bazzalt.MeshCollider")&&key=="MeshAsset")||
                            ((component=="Bazzalt.Mesh"||component=="Bazzalt.PrimitiveObject"||component=="Bazzalt.GuiElement")&&key=="MaterialAsset")||
                            ((component=="Bazzalt.ModelInstance"||component=="Bazzalt.ModelNode")&&key=="ModelAsset")||
                            ((component=="Bazzalt.Frame"||component=="Bazzalt.GuiElement")&&key=="Stylesheet")||
                            ((component=="Bazzalt.GuiText"||component=="Bazzalt.GuiTextInput")&&key=="Font")||
                            (component=="Bazzalt.BehaviorTreeAgent"&&key=="Tree")||(component=="Bazzalt.NavMeshSurface"&&key=="Data")||(component=="Bazzalt.TimelineDirector"&&key=="Sequence")||(component=="Bazzalt.WorldCell"&&key=="SceneAsset")||(component=="Bazzalt.Animator"&&(key=="Model"||key=="Controller"))||(component=="Bazzalt.ParticleEmitter"&&(key=="Graph"||key=="MaterialOverride"))||(component=="Bazzalt.Impostor"&&key=="Atlas")||(component=="Bazzalt.AudioSource"&&key=="Clip")||(component=="Bazzalt.AudioListener"&&key=="MixerAsset");
                        const bool camera=component=="Bazzalt.Camera"&&(key=="RenderTarget"||key=="Post.Material"||(key.starts_with("Post.Materials.")&&key.ends_with(".Asset"))||(key.starts_with("Post.Custom.")&&(key.ends_with(".Shader")||key.ends_with(".Texture"))));
                        const bool prefab=component=="Bazzalt.PrefabInstance"&&key=="Asset";
                        const bool image=component=="Bazzalt.GuiImage"&&key=="Texture";
                        const bool terrain=component=="Bazzalt.Terrain"&&((key.starts_with("Layers.")&&(key.ends_with(".Material")||key.ends_with(".Heightmap")||key.ends_with(".Splatmap")))||(key.starts_with("Splines.")&&key.ends_with(".Material"))||(key.starts_with("Vegetation.")&&(key.ends_with(".Model")||key.ends_with(".Material"))));
                        if(environment||named||camera||prefab||image||terrain||key.starts_with("AssetReference."))add(value,field);
                        if(component=="Bazzalt.ScriptComponents"&&key.ends_with(".Value")){
                            const auto typeKey=key.substr(0,key.size()-5)+"Type";
                            if(node.has_child(ryml::csubstr(typeKey.data(),typeKey.size()))){auto type=Text(node[ryml::csubstr(typeKey.data(),typeKey.size())]);
                                if(type.starts_with("Bazzalt::"))type=type.substr(9);
                                if(type=="Material"||type=="Shader"||type=="Texture"||type=="AudioClip"||type=="Font"||type=="GlobalEvent")add(value,field);
                            }
                        }
                        if(component=="Bazzalt.Mesh"&&key=="Materials"){
                            std::istringstream values(value);std::string part;while(std::getline(values,part,','))add(part,field);
                        }
                        if(component=="Bazzalt.ScriptComponents"&&key.ends_with("Source")&&!value.empty()){
                            UUID target;if(UUID::TryParse(value,target)){if(target)report.References.push_back({id,target,{},field});}
                            else report.References.push_back({id,{},m_projectDirectory/std::filesystem::u8path(value),field});
                        }
                    }
                }
            }else if(asset->Importer=="Bazzalt.Texture"||asset->Importer=="Bazzalt.AudioClip"||asset->Importer=="Bazzalt.Environment"||asset->Importer=="Bazzalt.RenderTexture"||asset->Importer=="Bazzalt.Font"||extension==".event"||extension==".bmixer")report.State=AssetDependencyScanState::Complete;
            // Scripts, unknown formats, and opaque binaries remain Unknown, not empty/complete.
        }
        for(auto& ref:report.References){
            ref.Source=id;
            if(ref.Field.size()>4096)throw std::runtime_error("Dependency field label exceeds 4096 bytes");
            if(!ref.TargetPath.empty()){
                auto path=ref.TargetPath.is_absolute()?ref.TargetPath:asset->SourcePath.parent_path()/ref.TargetPath;
                path=NormalizeSource(path);
                if(!IsPathWithin(m_assetDirectory,path))throw std::runtime_error("Dependency path escapes project Assets directory");
                ref.TargetPath=path;
                if(auto target=Find(path))ref.Target=target->Id;
            }
        }
        std::erase_if(report.References,[](const AssetDependency& ref){return !ref.Target&&ref.TargetPath.empty();});
        SortDependencies(report.References);
    }catch(const std::exception& error){report.References.clear();report.State=AssetDependencyScanState::Failed;report.LastError=error.what();}
    m_dependencyKeys[id]=asset->State==AssetState::Ready&&report.State!=AssetDependencyScanState::Failed?key:std::string{};
    m_dependencies[id]=std::move(report);
}
void AssetDatabase::RebuildDependencies(){
    std::erase_if(m_dependencies,[&](const auto& item){return !m_byId.contains(item.first);});
    std::erase_if(m_dependencyKeys,[&](const auto& item){return !m_byId.contains(item.first);});
    for(const auto& [id,_]:m_byId)ScanDependencies(id);RebuildReferencers();
}
void AssetDatabase::RebuildReferencers(){
    m_referencers.clear();
    for(auto& [source,report]:m_dependencies)for(auto& ref:report.References){
        // A path reference can become indexed after creation/import or disappear later.
        if(!ref.TargetPath.empty()){auto asset=Find(ref.TargetPath);ref.Target=asset?asset->Id:UUID{};}
        if(ref.Target)m_referencers[ref.Target].push_back(source);
    }
    for(auto& [_,ids]:m_referencers){std::sort(ids.begin(),ids.end());ids.erase(std::unique(ids.begin(),ids.end()),ids.end());}
}
AssetDependencyReport AssetDatabase::GetDependencyReport(UUID id)const{
    auto found=m_dependencies.find(id);if(found==m_dependencies.end())return {id};
    auto report=found->second;
    for(auto& ref:report.References){
        if(BuiltinReference(ref.Target))ref.State=AssetReferenceState::Builtin;
        else if(auto target=Find(ref.Target)){ref.TargetPath=target->SourcePath;ref.State=AssetReferenceState::Found;}
        else if(ref.Target||!ref.TargetPath.empty())ref.State=AssetReferenceState::Missing;
    }
    return report;
}
std::vector<UUID> AssetDatabase::GetDependencies(UUID id,bool recursive)const{
    return WalkDependencies(id,recursive,[&](UUID source){std::vector<UUID> ids;auto it=m_dependencies.find(source);if(it!=m_dependencies.end())for(const auto& ref:it->second.References)if(ref.Target)ids.push_back(ref.Target);return ids;});
}
std::vector<UUID> AssetDatabase::GetReferencers(UUID id,bool recursive)const{
    return WalkDependencies(id,recursive,[&](UUID target){auto found=m_referencers.find(target);return found==m_referencers.end()?std::vector<UUID>{}:found->second;});
}
void AssetDatabase::SaveDependencies(const std::filesystem::path& path)const{
    std::size_t nodes=3;
    for(const auto& [_,report]:m_dependencies){
        if(report.References.size()>250000||nodes+5+report.References.size()*4>1000000)throw std::runtime_error("Cooked dependency graph exceeds the node limit");
        nodes+=5+report.References.size()*4;
    }
    std::ofstream out(path,std::ios::binary);out<<"FormatVersion: 1\nAssets:"<<(m_dependencies.empty()?" []\n":"\n");
    std::vector<UUID> ids;for(const auto& [id,_]:m_dependencies)ids.push_back(id);std::sort(ids.begin(),ids.end());
    for(auto id:ids){const auto& report=m_dependencies.at(id);out<<"  - UUID: "<<QuoteYaml(id.ToString())<<"\n    State: "<<int(report.State)<<"\n    Error: "<<QuoteYaml(report.LastError)<<"\n    References:"<<(report.References.empty()?" []\n":"\n");
        for(const auto& ref:report.References)out<<"      - Target: "<<QuoteYaml(ref.Target.ToString())<<"\n        Path: "<<QuoteYaml(ref.TargetPath.empty()?"":PathUtf8(ref.TargetPath.lexically_relative(m_projectDirectory)))<<"\n        Field: "<<QuoteYaml(ref.Field)<<'\n';
    }
    if(!out)throw std::runtime_error("Could not write cooked asset dependency graph");
    if(out.tellp()>std::streamoff(MaxDependencyBytes))throw std::runtime_error("Cooked dependency graph exceeds 64 MiB");
}
void AssetDatabase::LoadDependencies(const std::filesystem::path& path){
    m_dependencies.clear();m_dependencyKeys.clear();m_referencers.clear();if(!std::filesystem::exists(path))return;
    auto text=DependencyText(path);auto tree=ryml::parse_in_arena(ryml::csubstr(text.data(),text.size()));auto root=tree.crootref();
    if(!root.is_map()||!root.has_child("FormatVersion")||!root["FormatVersion"].has_val()||Text(root["FormatVersion"])!="1"||!root.has_child("Assets"))throw std::runtime_error("Invalid cooked dependency graph");
    if(tree.size()>1000000)throw std::runtime_error("Cooked dependency graph has too many nodes");
    if(!root["Assets"].is_seq())throw std::runtime_error("Invalid dependency owner list");
    for(auto node:root["Assets"].children()){
        if(!node.is_map()||!node.has_child("UUID")||!node["UUID"].has_val()||!node.has_child("State")||!node["State"].has_val()||!node.has_child("Error")||!node["Error"].has_val()||!node.has_child("References")||!node["References"].is_seq())throw std::runtime_error("Invalid dependency owner record");
        AssetDependencyReport report;
        if(!UUID::TryParse(Text(node["UUID"]),report.Asset)||!report.Asset||m_dependencies.contains(report.Asset))throw std::runtime_error("Invalid dependency owner");
        std::uint32_t state;if(!ReadUnsigned(node["State"],state)||state>3)throw std::runtime_error("Invalid dependency scan state");report.State=static_cast<AssetDependencyScanState>(state);report.LastError=Text(node["Error"]);
        if(!Find(report.Asset))continue; // Editor-only assets may be omitted from game output.
        for(auto item:node["References"].children()){
            if(!item.is_map()||!item.has_child("Target")||!item["Target"].has_val()||!item.has_child("Path")||!item["Path"].has_val()||!item.has_child("Field")||!item["Field"].has_val())throw std::runtime_error("Invalid dependency reference record");
            AssetDependency ref;ref.Source=report.Asset;if(!UUID::TryParse(Text(item["Target"]),ref.Target))throw std::runtime_error("Invalid dependency UUID");
            const auto value=Text(item["Path"]);if(!value.empty()){ref.TargetPath=NormalizeSource(m_projectDirectory/std::filesystem::u8path(value));if(!IsPathWithin(m_assetDirectory,ref.TargetPath))throw std::runtime_error("Unsafe cooked dependency path");}
            ref.Field=Text(item["Field"]);report.References.push_back(std::move(ref));
        }
        SortDependencies(report.References);m_dependencies.emplace(report.Asset,std::move(report));
    }
    RebuildReferencers();
}
