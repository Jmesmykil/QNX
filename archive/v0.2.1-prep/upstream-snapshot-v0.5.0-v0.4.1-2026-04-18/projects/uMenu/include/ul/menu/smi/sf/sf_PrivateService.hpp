
#pragma once
#include <ul/smi/smi_Protocol.hpp>
#include <ul/smi/sf/sf_Private.hpp>

namespace ul::menu::smi::sf {

    using namespace ul::smi;
    using namespace ul::smi::sf;

    using OnMessageCallback = std::function<void(const MenuMessageContext&)>;

    Result InitializePrivateService();
    void FinalizePrivateService();

    void RegisterOnMessageDetect(OnMessageCallback callback, const MenuMessage desired_msg = MenuMessage::Invalid);

}
