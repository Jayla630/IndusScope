#include "indusscope/protocol/ProtocolFactory.h"

namespace indusscope::protocol {

ProtocolFactory& ProtocolFactory::instance() {
    static ProtocolFactory s_instance; // thread-safe since C++11 / C++11 起静态局部变量线程安全
    return s_instance;
}

bool ProtocolFactory::registerProtocol(const std::string& name, Creator creator) {
    if (!creator) return false; // reject null creator / 拒绝空 creator
    auto [it, inserted] = m_creators.emplace(name, std::move(creator));
    (void)it;
    return inserted; // false if name already exists / 重名则首注册生效,返回 false
}

std::unique_ptr<IDeviceProtocol> ProtocolFactory::create(const std::string& name) const {
    auto it = m_creators.find(name);
    if (it == m_creators.end()) return nullptr; // not registered / 未注册
    return it->second();
}

std::vector<std::string> ProtocolFactory::registeredNames() const {
    std::vector<std::string> names;
    names.reserve(m_creators.size());
    for (const auto& [name, creator] : m_creators) { // cold path — push_back is fine / 冷路径无碍
        names.push_back(name);
    }
    return names;
}

} // namespace indusscope::protocol
