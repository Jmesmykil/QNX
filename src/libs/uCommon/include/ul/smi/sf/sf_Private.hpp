
#pragma once
#include <ul/ul_Include.hpp>

namespace ul::smi::sf {

    constexpr const char PrivateServiceName[] = "ulsf:p";

    struct NacpMetadata {
        char name[sizeof(NacpLanguageEntry::name)];
        char author[sizeof(NacpLanguageEntry::author)];
        char display_version[sizeof(NacpStruct::display_version)];
    };

}
