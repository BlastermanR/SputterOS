#ifndef SPUTTEROS_BUILDER_H
#define SPUTTEROS_BUILDER_H

/**
 * @file Builder.h
 * @brief Builder component header — fluent SystemBuilder API.
 *
 * The SystemBuilder validates configuration and populates `System<Cfg>`
 * with all kernel tasks and infrastructure.
 *
 * @code
 * #include "sputteros/Builder.h"
 *
 * SystemBuilder<Cfg> builder(&app, monitors.data(), monitors.size());
 * builder.setStream(&stream);
 * builder.core(0).addScheduledTask(&myTask);
 * auto result = builder.build();
 * @endcode
 *
 * @author SputterOS Contributors
 * @date 4/11/2026
 */

#include "sputteros/builder/SystemBuilder.h"

#endif // SPUTTEROS_BUILDER_H
