
/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _GLUE_MESSAGES_H_
#define _GLUE_MESSAGES_H_

#include "pin.H"
#include <string>

namespace glue
{
/**
 * @brief A structure holding service request data.
 * 
 */
struct Service_request
{
    /** @brief 64 bit channel id */
    UINT64 callbackAddress;
    /** @brief 5 Alpha Numeric characters named for this service */
    std::string name;
    /** @brief A string representing the name of the service */
    std::string serviceName;
    /** @brief The name of the callback function to call with the result */
    std::string callbackName;
    /** @brief Array of knob strings */
    std::vector< std::string > knobs;
    /** @brief Array of script fragments */
    std::vector< std::string > scripts;
};

} // namespace glue

#endif // _GLUE_MESSAGES_H_
