/* Mock credentials.h — redirects to mock_esp.h */
#pragma once
#include "mock_esp.h"
const credentials_t *credentials_get(void);
