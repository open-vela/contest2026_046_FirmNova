#include <stddef.h>

#include "scooterdemo_pages.h"

void scooterdemo_page_manager_init(scooterdemo_page_manager_t *manager)
{
    if (manager == NULL) {
        return;
    }

    for (int index = 0; index < SCOOTERDEMO_PAGE_COUNT; index++) {
        manager->containers[index] = NULL;
    }

    manager->active_page = SCOOTERDEMO_PAGE_HOME;
    manager->previous_page = SCOOTERDEMO_PAGE_HOME;
}

lv_obj_t *scooterdemo_page_manager_create_container(lv_obj_t *parent,
                                                    const scooterdemo_ui_metrics_t *ui)
{
    lv_obj_t *container = lv_obj_create(parent);

    /* Page roots are full-screen canvases, so theme card padding must be removed. */
    lv_obj_remove_style_all(container);
    lv_obj_set_size(container, ui->disp_w, ui->disp_h);
    lv_obj_align(container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_0, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);

    return container;
}

void scooterdemo_page_manager_register(scooterdemo_page_manager_t *manager,
                                       scooterdemo_page_id_t page_id,
                                       lv_obj_t *container)
{
    if (manager == NULL || page_id >= SCOOTERDEMO_PAGE_COUNT) {
        return;
    }

    manager->containers[page_id] = container;
}

void scooterdemo_page_manager_show(scooterdemo_page_manager_t *manager,
                                   scooterdemo_page_id_t page_id)
{
    if (manager == NULL || page_id >= SCOOTERDEMO_PAGE_COUNT) {
        return;
    }

    if (manager->active_page != page_id) {
        manager->previous_page = manager->active_page;
    }

    for (int index = 0; index < SCOOTERDEMO_PAGE_COUNT; index++) {
        lv_obj_t *container = manager->containers[index];

        if (container == NULL) {
            continue;
        }

        if (index == page_id) {
            lv_obj_clear_flag(container, LV_OBJ_FLAG_HIDDEN);
            /* Restore visibility for containers previously faded out by overlays. */
            lv_obj_set_style_opa(container, LV_OPA_COVER, 0);
            lv_obj_move_foreground(container);
        } else {
            lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);
        }
    }

    manager->active_page = page_id;
}
