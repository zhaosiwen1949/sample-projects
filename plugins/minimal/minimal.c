static struct tm_logger_api* tm_logger_api;

#include <foundation/api_registry.h>

#include <foundation/log.h>

TM_DLL_EXPORT void tm_load_plugin(struct tm_api_registry_api* reg, bool load)
{
    tm_logger_api = reg->get(TM_LOGGER_API_NAME);

    TM_LOG("Minimal plugin %s.\n", load ? "loaded" : "unloaded");
}
