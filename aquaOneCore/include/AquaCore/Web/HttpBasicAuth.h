#pragma once

#include <stddef.h>

namespace AquaCore { namespace Web {

// Implementation capacity for one Authorization header, not a credential policy.
constexpr size_t HTTP_AUTHORIZATION_HEADER_CAPACITY = 258U;
constexpr size_t HTTP_BASIC_REALM_CAPACITY = 63U;

bool verifyHttpBasicAuthorization(const char* header, size_t length,
                                  const char* user, const char* password);
bool validHttpBasicRealm(const char* realm);

} }
