#include "Bazzalt/AI.h"
#include "AI/BehaviorModuleApi.h"
namespace Bazzalt::AI {
struct BehaviorTree::Impl {
    const Runtime::BehaviorModuleApi& Api=Runtime::BehaviorModule();
    void* Handle=nullptr;
    ~Impl(){if(Handle)Api.DestroyTree(Handle);}
};
BehaviorTree::BehaviorTree()=default;
BehaviorTree::~BehaviorTree()=default;
BehaviorTree::BehaviorTree(BehaviorTree&&) noexcept=default;
BehaviorTree& BehaviorTree::operator=(BehaviorTree&&) noexcept=default;
BehaviorTree BehaviorTree::FromSource(const std::string& xml){
    BehaviorTree tree;tree.m_impl=std::make_unique<Impl>();tree.m_impl->Handle=tree.m_impl->Api.CreateTree(xml);return tree;
}
BehaviorTree BehaviorTree::Load(UUID asset){
    BehaviorTree tree;tree.m_impl=std::make_unique<Impl>();tree.m_impl->Handle=tree.m_impl->Api.LoadTree(asset);return tree;
}
Status BehaviorTree::Tick(Context& context){return m_impl?m_impl->Api.TickTree(m_impl->Handle,context):Status::Failure;}
void BehaviorTree::Reset(Context& context){if(m_impl)m_impl->Api.ResetTree(m_impl->Handle,context);}
void BehaviorTree::Initialize(Blackboard& memory) const {if(m_impl)m_impl->Api.InitializeTree(m_impl->Handle,memory);}
}
