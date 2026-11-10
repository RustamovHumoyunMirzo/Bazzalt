#pragma once
#include <algorithm>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

namespace Bazzalt {
// Case-sensitive semantic labels. Stable names persist across scenes and builds.
class TagSet {
public:
    TagSet() = default;
    TagSet(std::initializer_list<std::string> values) { for(const auto& value:values) Add(value); }
    explicit TagSet(const std::vector<std::string>& values) { for(const auto& value:values) Add(value); }
    static bool IsValidName(const std::string& value) {
        if(value.empty() || value.size()>64 || value.front()==' ' || value.back()==' ') return false;
        return std::none_of(value.begin(),value.end(),[](unsigned char c){return c<32 || c==127 || c==',';});
    }
    bool Add(const std::string& value) {
        if(!IsValidName(value)) throw std::invalid_argument("Invalid tag name");
        auto where=std::lower_bound(m_values.begin(),m_values.end(),value);
        if(where!=m_values.end() && *where==value) return false;
        if(m_values.size()>=64) throw std::length_error("An entity supports at most 64 tags");
        m_values.insert(where,value);
        return true;
    }
    bool Remove(const std::string& value) {
        auto where=std::lower_bound(m_values.begin(),m_values.end(),value);
        if(where==m_values.end() || *where!=value) return false;
        m_values.erase(where);
        return true;
    }
    bool Has(const std::string& value) const { return std::binary_search(m_values.begin(),m_values.end(),value); }
    bool HasAny(const TagSet& tags) const { return std::any_of(tags.m_values.begin(),tags.m_values.end(),[&](const auto& tag){return Has(tag);}); }
    bool HasAll(const TagSet& tags) const { return std::all_of(tags.m_values.begin(),tags.m_values.end(),[&](const auto& tag){return Has(tag);}); }
    void Clear() { m_values.clear(); }
    bool IsEmpty() const { return m_values.empty(); }
    std::size_t GetCount() const { return m_values.size(); }
    const std::vector<std::string>& GetValues() const { return m_values; }
    std::string ToString() const {
        std::string result;
        for(const auto& value:m_values) { if(!result.empty())result+=','; result+=value; }
        return result;
    }
    static bool TryParse(const std::string& text,TagSet& output) {
        if(text.size()>4160)return false;
        TagSet next;
        try {
            std::size_t start=0;
            while(start<text.size()) {
                auto end=text.find(',',start);
                next.Add(text.substr(start,end==std::string::npos?end:end-start));
                if(end==std::string::npos)break;
                start=end+1;
                if(start==text.size())return false;
            }
        } catch(const std::exception&) { return false; }
        output=std::move(next);
        return true;
    }
    bool operator==(const TagSet&) const = default;
private:
    std::vector<std::string> m_values;
};
}
