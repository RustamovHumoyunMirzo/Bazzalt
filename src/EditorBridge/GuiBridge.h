#pragma once
#include "Bazzalt/GUI.h"
#include "Bazzalt/Components/CameraRenderTarget.h"
#include "GUI/GuiSerialization.h"
#include "Bazzalt/Serialization.h"
#include <pybind11/pybind11.h>
#include <sstream>
namespace Bazzalt::EditorBridge {
namespace py=pybind11;
inline constexpr const char* GuiNames[]={"Frame","RectTransform","Rectangle","GuiImage","GuiText","GuiButton","GuiTextInput","CameraRenderTarget"};
inline ComponentSerializationRegistry& GuiRegistry(){static ComponentSerializationRegistry registry;static bool initialized=(Runtime::RegisterGuiComponents(registry),true);(void)initialized;return registry;}
template<class F> bool GuiDispatch(Entity e,const std::string& name,F function){
    if(name=="Frame")function.template operator()<Frame>(e);
    else if(name=="RectTransform")function.template operator()<RectTransform>(e);
    else if(name=="Rectangle")function.template operator()<Rectangle>(e);
    else if(name=="GuiImage")function.template operator()<GuiImage>(e);
    else if(name=="GuiText")function.template operator()<GuiText>(e);
    else if(name=="GuiButton")function.template operator()<GuiButton>(e);
    else if(name=="GuiTextInput")function.template operator()<GuiTextInput>(e);
    else if(name=="CameraRenderTarget")function.template operator()<CameraRenderTarget>(e);
    else return false;
    return true;
}
inline void InspectGUI(Entity entity,py::list components,py::dict enabled,py::dict data){
    for(auto name:GuiNames){PropertyMap values;auto* descriptor=GuiRegistry().Find("Bazzalt."+std::string(name));if(!descriptor->Serialize(entity,values))continue;
        components.append(name);enabled[name]=values["Enabled"]=="true";py::dict fields;
        for(const auto& [key,text]:values){if(key=="Enabled")continue;
            if(key=="Value"||key=="Placeholder"||key=="Texture"||key=="Camera")fields[py::str(key)]=text;
            else if(text=="true"||text=="false")fields[py::str(key)]=text=="true";
            else if(text.find(',')!=std::string::npos){py::list vector;std::stringstream input(text);std::string part;while(std::getline(input,part,','))vector.append(std::stof(part));fields[py::str(key)]=py::tuple(vector);}
            else if(key=="Mode"||key=="ScaleMode"||key=="ZIndex"||key=="TabIndex"||key=="MaxLength"||key=="Width"||key=="Height")fields[py::str(key)]=std::stoll(text);
            else fields[py::str(key)]=std::stof(text);
        }
        data[name]=fields;
    }
}
inline bool SetGUIProperty(Entity entity,const std::string& type,const std::string& key,py::handle value){
    auto* descriptor=GuiRegistry().Find("Bazzalt."+type);if(!descriptor)return false;PropertyMap values;if(!descriptor->Serialize(entity,values)||!values.contains(key))return false;
    std::string text;
    if(py::isinstance<py::bool_>(value))text=value.cast<bool>()?"true":"false";
    else if(py::isinstance<py::sequence>(value)&&!py::isinstance<py::str>(value)){for(auto part:py::reinterpret_borrow<py::sequence>(value)){if(!text.empty())text+=',';text+=py::str(part).cast<std::string>();}}
    else text=py::str(value).cast<std::string>();
    values[key]=std::move(text);return descriptor->Deserialize(entity,values,1);
}
}
