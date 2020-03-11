static struct tm_api_registry_api* tm_global_api_registry;

static struct tm_draw2d_api* tm_draw2d_api;
static struct tm_temp_allocator_api* tm_temp_allocator_api;
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
#define TM_CUSTOM_TAB_VT_NAME_HASH TM_STATIC_HASH("tm_custom_tab", 0xd87f5f4217cf1a47ULL)

struct tm_tab_o {
    tm_tab_i tm_tab_i;
    tm_allocator_i* allocator;
};

static void tab__ui(tm_tab_o* tab, uint32_t font, const tm_font_t* font_info, float font_scale, tm_ui_o* ui, tm_rect_t rect)
{
    tm_ui_buffers_t uib = tm_ui_api->buffers(ui);
    const tm_ui_style_t* uistyle = &(tm_ui_style_t){
        .font = font,
        .font_info = font_info,
        .font_scale = font_scale,
    };
    tm_draw2d_style_t* style = &(tm_draw2d_style_t){ 0 };
    tm_ui_api->to_draw_style(style, uistyle);

    style->color = (tm_color_srgb_t){ .a = 255, .r = 255 };
    tm_draw2d_api->fill_rect(uib.vbuffer, *uib.ibuffers, style, rect);
}

static const char* tab__create_menu_name()
{
    return "Custom Tab";
}

static const char* tab__title(tm_tab_o* tab, struct tm_ui_o* ui)
{
    return "Custom Tab";
}

static tm_tab_i* tab__create(tm_tab_create_context_t* context)
{
    tm_allocator_i* allocator = context->allocator;
    uint64_t* id = context->id;

    static tm_the_machinery_tab_vt* vt = 0;
    if (!vt)
        vt = tm_global_api_registry->get(TM_CUSTOM_TAB_VT_NAME);

    tm_tab_o* tab = tm_alloc(allocator, sizeof(tm_tab_o));
    *tab = (tm_tab_o){
        .tm_tab_i = {
            .vt = (tm_tab_vt*)vt,
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
    .name = TM_CUSTOM_TAB_VT_NAME,
    .name_hash = TM_CUSTOM_TAB_VT_NAME_HASH,
    .create_menu_name = tab__create_menu_name,
    .create = tab__create,
    .destroy = tab__destroy,
    .title = tab__title,
    .ui = tab__ui
};

TM_DLL_EXPORT void tm_load_plugin(struct tm_api_registry_api* reg, bool load)
{
    tm_global_api_registry = reg;

    tm_draw2d_api = reg->get(TM_DRAW2D_API_NAME);
    tm_ui_api = reg->get(TM_UI_API_NAME);

    tm_set_or_remove_api(reg, load, TM_CUSTOM_TAB_VT_NAME, custom_tab_vt);
    tm_add_or_remove_implementation(reg, load, TM_TAB_VT_INTERFACE_NAME, custom_tab_vt);
}
