#include <plugins/entity/entity_api_types.h>

#define TM_TT_TYPE__INTERACTABLE_COMPONENT "tm_interactable_component"
#define TM_TT_TYPE_HASH__INTERACTABLE_COMPONENT TM_STATIC_HASH("tm_interactable_component", 0x95e4f6722c966bf4ULL)

typedef struct tm_interactable_component_manager_o tm_interactable_component_manager_o;

struct tm_interactable_component_api {
    bool (*can_interact)(tm_interactable_component_manager_o *mgr, tm_entity_t interactable, bool is_player);
    void (*interact)(tm_interactable_component_manager_o *mgr, tm_entity_t interactable);
    void (*update_active_interactables)(tm_interactable_component_manager_o *mgr, float dt, double t);
};