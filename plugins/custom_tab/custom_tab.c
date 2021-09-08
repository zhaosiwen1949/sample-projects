static struct tm_api_registry_api* tm_global_api_registry;

static struct tm_draw2d_api* tm_draw2d_api;
static struct tm_ui_api* tm_ui_api;

#include <foundation/allocator.h>
#include <foundation/api_registry.h>

#include <plugins/ui/docking.h>
#include <plugins/ui/draw2d.h>
#include <plugins/ui/ui.h>
#include <plugins/ui/ui_custom.h>

#include <the_machinery/the_machinery_tab.h>

#include <stdio.h>

TM_DLL_EXPORT void load_custom_tab(struct tm_api_registry_api* reg, bool load);

#define TM_CUSTOM_TAB_VT_NAME "tm_custom_tab"
#define TM_CUSTOM_TAB_VT_NAME_HASH TM_STATIC_HASH("tm_custom_tab", 0xbc4e3e47fbf1cdc1ULL)

struct tm_tab_o {
    tm_tab_i tm_tab_i;
    tm_allocator_i* allocator;
};

static void tab__ui(tm_tab_o* tab, tm_ui_o* ui, const tm_ui_style_t* uistyle, tm_rect_t rect)
{
    tm_ui_buffers_t uib = tm_ui_api->buffers(ui);
    tm_draw2d_style_t* style = &(tm_draw2d_style_t){ 0 };
    tm_ui_api->to_draw_style(ui, style, uistyle);

    style->color = (tm_color_srgb_t){ .a = 255, .r = 255 };
    tm_draw2d_api->fill_rect(uib.vbuffer, *uib.ibuffers, style, rect);
}

static const char* tab__create_menu_name(void)
{
    return "Custom Tab";
}

static const char* tab__title(tm_tab_o* tab, struct tm_ui_o* ui)
{
    return "Custom Tab";
}

static tm_the_machinery_tab_vt* custom_tab_vt;

static tm_tab_i* tab__create(tm_tab_create_context_t* context, tm_ui_o* ui)
{
    tm_allocator_i* allocator = context->allocator;
    uint64_t* id = context->id;

    tm_tab_o* tab = tm_alloc(allocator, sizeof(tm_tab_o));
    *tab = (tm_tab_o){
        .tm_tab_i = {
            .vt = (tm_tab_vt*)custom_tab_vt,
            .inst = (tm_tab_o*)tab,
            .root_id = *id,
        },
        .allocator = allocator,
    };

    *id += 1000000;
    return &tab->tm_tab_i;
}

static void tab__destroy(tm_tab_o* tab)
{
    tm_free(tab->allocator, tab, sizeof(*tab));
}

static tm_the_machinery_tab_vt* custom_tab_vt = &(tm_the_machinery_tab_vt){
    .super = {
        .name = TM_CUSTOM_TAB_VT_NAME,
        .name_hash = TM_CUSTOM_TAB_VT_NAME_HASH,
        .create_menu_name = tab__create_menu_name,
        .create = tab__create,
        .destroy = tab__destroy,
        .title = tab__title,
        .ui = tab__ui,
    }
};

TM_DLL_EXPORT void tm_load_plugin(struct tm_api_registry_api* reg, bool load)
{
    tm_global_api_registry = reg;

    tm_draw2d_api = tm_get_api(reg, tm_draw2d_api);
    tm_ui_api = tm_get_api(reg, tm_ui_api);

    tm_add_or_remove_implementation(reg, load, tm_tab_vt, &custom_tab_vt->super);
}
