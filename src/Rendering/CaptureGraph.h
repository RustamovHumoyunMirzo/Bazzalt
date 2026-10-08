#pragma once
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <vector>
#include "Bazzalt/UUID.h"
namespace Bazzalt::Runtime {
// Strongly-connected components in producer-first order. Members of a cycle
// sample a frozen previous-frame snapshot and publish together.
inline std::vector<std::vector<UUID>> BuildCapturePlan(
    const std::vector<UUID>& cameras,const std::unordered_map<UUID,std::vector<UUID>>& dependencies){
    std::unordered_map<UUID,int> index,low;
    std::vector<UUID> stack;std::unordered_map<UUID,bool> pending;
    std::vector<std::vector<UUID>> result;int next=0;
    std::function<void(UUID)> visit=[&](UUID id){
        index[id]=low[id]=next++;stack.push_back(id);pending[id]=true;
        if(auto edges=dependencies.find(id);edges!=dependencies.end())for(auto source:edges->second){
            if(std::find(cameras.begin(),cameras.end(),source)==cameras.end())continue;
            if(!index.contains(source)){visit(source);low[id]=std::min(low[id],low[source]);}
            else if(pending[source])low[id]=std::min(low[id],index[source]);
        }
        if(low[id]==index[id]){auto& component=result.emplace_back();UUID value;do{value=stack.back();stack.pop_back();pending[value]=false;component.push_back(value);}while(value!=id);std::sort(component.begin(),component.end());}
    };
    for(auto camera:cameras)if(!index.contains(camera))visit(camera);
    return result;
}
}
