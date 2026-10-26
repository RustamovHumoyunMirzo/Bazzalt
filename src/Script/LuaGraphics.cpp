#include "Script/LuaBindings.h"
#include "Runtime/GraphicsAccess.h"
#include <stdexcept>
namespace Bazzalt::Runtime {
namespace {
std::vector<GraphicsVertex> Vertices(sol::table table){if(table.size()>Graphics::MaxVertices)throw std::length_error("Graphics vertex limit exceeded");std::vector<GraphicsVertex> values;for(std::size_t i=1;i<=table.size();++i)values.push_back(table.get<GraphicsVertex>(i));return values;}
std::vector<std::uint32_t> Indices(sol::optional<sol::table> table){std::vector<std::uint32_t> values;if(table){if(table->size()>Graphics::MaxIndices)throw std::length_error("Graphics index limit exceeded");for(std::size_t i=1;i<=table->size();++i)values.push_back(table->get<std::uint32_t>(i));}return values;}
template<class T> T Required(sol::variadic_args args,std::size_t index){if(index>=args.size())throw std::invalid_argument("Missing Graphics argument");return args[index].get<T>();}
template<class T> sol::optional<T> Optional(sol::variadic_args args,std::size_t index){if(index>=args.size()||args[index].get_type()==sol::type::nil)return {};return args[index].get<T>();}
void Budget(lua_State* state,lua_Debug*){luaL_error(state,"Graphics callback exceeded its Lua instruction budget");}
}
void BindLuaGraphics(sol::table api,Entity owner){
    api.new_enum<GraphicsTopology>("GraphicsTopology",{{"Points",GraphicsTopology::Points},{"Lines",GraphicsTopology::Lines},{"LineStrip",GraphicsTopology::LineStrip},{"Triangles",GraphicsTopology::Triangles},{"TriangleStrip",GraphicsTopology::TriangleStrip}});
    api.new_enum<GraphicsEventType>("GraphicsEventType",{{"OnBeginFrame",GraphicsEventType::OnBeginFrame},{"OnCameraResize",GraphicsEventType::OnCameraResize},{"OnBeforeCamera",GraphicsEventType::OnBeforeCamera},{"OnBeforeDraw",GraphicsEventType::OnBeforeDraw},{"OnAfterDraw",GraphicsEventType::OnAfterDraw},{"OnAfterCamera",GraphicsEventType::OnAfterCamera},{"OnEndFrame",GraphicsEventType::OnEndFrame}});
    auto vertex=api.new_usertype<GraphicsVertex>("GraphicsVertex",sol::constructors<GraphicsVertex()>());
    vertex["Position"]=&GraphicsVertex::Position;vertex["Normal"]=&GraphicsVertex::Normal;vertex["UV"]=&GraphicsVertex::UV;vertex["Color"]=&GraphicsVertex::Color;
    auto options=api.new_usertype<GraphicsDrawOptions>("GraphicsDrawOptions",sol::constructors<GraphicsDrawOptions()>());
    options["Camera"]=&GraphicsDrawOptions::Camera;options["LayerMask"]=&GraphicsDrawOptions::LayerMask;options["Priority"]=&GraphicsDrawOptions::Priority;options["CastShadows"]=&GraphicsDrawOptions::CastShadows;options["ReceiveShadows"]=&GraphicsDrawOptions::ReceiveShadows;
    auto event=api.new_usertype<GraphicsEvent>("GraphicsEvent",sol::no_constructor);
    event["Type"]=&GraphicsEvent::Type;
    event["Scene"]=&GraphicsEvent::Scene;
    event["Camera"]=&GraphicsEvent::Camera;
    event["Viewport"]=&GraphicsEvent::Viewport;
    event["Frame"]=&GraphicsEvent::Frame;
    event["Width"]=&GraphicsEvent::Width;
    event["Height"]=&GraphicsEvent::Height;
    event["IsEditorCamera"]=&GraphicsEvent::IsEditorCamera;
    event["View"]=&GraphicsEvent::View;
    event["Projection"]=&GraphicsEvent::Projection;
    event["ViewProjection"]=&GraphicsEvent::ViewProjection;
    event["DrawIndex"]=&GraphicsEvent::DrawIndex;
    event["DrawCount"]=&GraphicsEvent::DrawCount;
    auto mesh=api.new_usertype<GraphicsMesh>("GraphicsMesh",sol::constructors<GraphicsMesh()>());
    mesh["IsValid"]=&GraphicsMesh::IsValid;mesh["GetId"]=&GraphicsMesh::GetId;mesh["GetRevision"]=&GraphicsMesh::GetRevision;mesh["GetTopology"]=&GraphicsMesh::GetTopology;mesh["GetVertexCount"]=&GraphicsMesh::GetVertexCount;mesh["GetIndexCount"]=&GraphicsMesh::GetIndexCount;mesh["Destroy"]=&GraphicsMesh::Destroy;
    mesh["GetVertices"]=[](const GraphicsMesh& v){return sol::as_table(v.GetVertices());};mesh["GetIndices"]=[](const GraphicsMesh& v){return sol::as_table(v.GetIndices());};
    mesh["SetGeometry"]=[](GraphicsMesh& v,sol::table vertices,sol::optional<sol::table> indices,sol::optional<GraphicsTopology> topology){v.SetGeometry(Vertices(vertices),Indices(indices),topology.value_or(GraphicsTopology::Triangles));};
    auto graphics=api.create_named("Graphics");
    graphics["MaxVertices"]=Graphics::MaxVertices;graphics["MaxIndices"]=Graphics::MaxIndices;graphics["MaxDraws"]=Graphics::MaxDraws;
    auto offset=[](sol::variadic_args args){if(args.size()&&args[0].get_type()==sol::type::table){auto first=args[0].get<sol::table>();if(first.raw_get<sol::object>("CreateMesh").get_type()==sol::type::function)return std::size_t(1);}return std::size_t(0);};
    graphics["CreateMesh"]=[offset](sol::variadic_args args){auto i=offset(args);return Graphics::CreateMesh(Vertices(Required<sol::table>(args,i)),Indices(Optional<sol::table>(args,i+1)),Optional<GraphicsTopology>(args,i+2).value_or(GraphicsTopology::Triangles));};
    graphics["DrawMesh"]=[offset](sol::variadic_args args){auto i=offset(args);Graphics::DrawMesh(Required<GraphicsMesh>(args,i),Optional<Material>(args,i+1).value_or(Material{}),Optional<Mat4>(args,i+2).value_or(Mat4{}),Optional<GraphicsDrawOptions>(args,i+3).value_or(GraphicsDrawOptions{}));};
    graphics["DrawVertices"]=[offset](sol::variadic_args args){auto i=offset(args);Graphics::DrawVertices(Vertices(Required<sol::table>(args,i)),Indices(Optional<sol::table>(args,i+1)),Optional<GraphicsTopology>(args,i+2).value_or(GraphicsTopology::Triangles),Optional<Material>(args,i+3).value_or(Material{}),Optional<Mat4>(args,i+4).value_or(Mat4{}),Optional<GraphicsDrawOptions>(args,i+5).value_or(GraphicsDrawOptions{}));};
    graphics["DrawLine"]=[offset](sol::variadic_args args){auto i=offset(args);Graphics::DrawLine(Required<Vec3>(args,i),Required<Vec3>(args,i+1),Optional<Vec4>(args,i+2).value_or(Vec4{1,1,1,1}),Optional<GraphicsDrawOptions>(args,i+3).value_or(GraphicsDrawOptions{}));};
    graphics["DrawTriangle"]=[offset](sol::variadic_args args){auto i=offset(args);Graphics::DrawTriangle(Required<Vec3>(args,i),Required<Vec3>(args,i+1),Required<Vec3>(args,i+2),Optional<Vec4>(args,i+3).value_or(Vec4{1,1,1,1}),Optional<GraphicsDrawOptions>(args,i+4).value_or(GraphicsDrawOptions{}));};
    graphics["ClearDraws"]=sol::overload(&Graphics::ClearDraws,[](sol::table){Graphics::ClearDraws();});
    graphics["IsAvailable"]=sol::overload(&Graphics::IsAvailable,[](sol::table){return Graphics::IsAvailable();});
    graphics["Unsubscribe"]=sol::overload(&Graphics::Unsubscribe,[](sol::table,std::uint64_t id){return Graphics::Unsubscribe(id);});
    auto subscribe=[owner](GraphicsEventType type,sol::protected_function fn,sol::optional<Entity> entity){auto* state=fn.lua_state();return GraphicsAccess::SubscribeScoped(type,[fn=std::move(fn),state](const GraphicsEvent& e)mutable{
        auto hook=lua_gethook(state);auto mask=lua_gethookmask(state),count=lua_gethookcount(state);lua_sethook(state,Budget,LUA_MASKCOUNT,1000000);
        auto result=fn(e);lua_sethook(state,hook,mask,count);if(!result.valid()){sol::error error=result;throw std::runtime_error(error.what());}
    },entity.value_or(owner),state);};
    graphics["Subscribe"]=sol::overload(subscribe,[subscribe](sol::table,GraphicsEventType t,sol::protected_function f,sol::optional<Entity> e){return subscribe(t,std::move(f),e);});
    graphics["OnBeginFrame"]=sol::overload([subscribe](sol::protected_function f,sol::optional<Entity> e){return subscribe(GraphicsEventType::OnBeginFrame,std::move(f),e);},[subscribe](sol::table,sol::protected_function f,sol::optional<Entity> e){return subscribe(GraphicsEventType::OnBeginFrame,std::move(f),e);});
    graphics["OnCameraResize"]=sol::overload([subscribe](sol::protected_function f,sol::optional<Entity> e){return subscribe(GraphicsEventType::OnCameraResize,std::move(f),e);},[subscribe](sol::table,sol::protected_function f,sol::optional<Entity> e){return subscribe(GraphicsEventType::OnCameraResize,std::move(f),e);});
    graphics["OnBeforeCamera"]=sol::overload([subscribe](sol::protected_function f,sol::optional<Entity> e){return subscribe(GraphicsEventType::OnBeforeCamera,std::move(f),e);},[subscribe](sol::table,sol::protected_function f,sol::optional<Entity> e){return subscribe(GraphicsEventType::OnBeforeCamera,std::move(f),e);});
    graphics["OnBeforeDraw"]=sol::overload([subscribe](sol::protected_function f,sol::optional<Entity> e){return subscribe(GraphicsEventType::OnBeforeDraw,std::move(f),e);},[subscribe](sol::table,sol::protected_function f,sol::optional<Entity> e){return subscribe(GraphicsEventType::OnBeforeDraw,std::move(f),e);});
    graphics["OnAfterDraw"]=sol::overload([subscribe](sol::protected_function f,sol::optional<Entity> e){return subscribe(GraphicsEventType::OnAfterDraw,std::move(f),e);},[subscribe](sol::table,sol::protected_function f,sol::optional<Entity> e){return subscribe(GraphicsEventType::OnAfterDraw,std::move(f),e);});
    graphics["OnAfterCamera"]=sol::overload([subscribe](sol::protected_function f,sol::optional<Entity> e){return subscribe(GraphicsEventType::OnAfterCamera,std::move(f),e);},[subscribe](sol::table,sol::protected_function f,sol::optional<Entity> e){return subscribe(GraphicsEventType::OnAfterCamera,std::move(f),e);});
    graphics["OnEndFrame"]=sol::overload([subscribe](sol::protected_function f,sol::optional<Entity> e){return subscribe(GraphicsEventType::OnEndFrame,std::move(f),e);},[subscribe](sol::table,sol::protected_function f,sol::optional<Entity> e){return subscribe(GraphicsEventType::OnEndFrame,std::move(f),e);});
}
}
