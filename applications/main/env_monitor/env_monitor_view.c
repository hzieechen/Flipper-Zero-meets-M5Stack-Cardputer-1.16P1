/**
 * @file env_monitor_view.c
 * @brief Canvas read-out: temperature / humidity / pressure / altitude.
 *
 * The whole UI is Simplified Chinese; canvas_draw_str() automatically swaps
 * to the bundled wqy12 CJK font for any string with non-ASCII bytes.
 */
#include "env_monitor_view.h"

#include <gui/canvas.h>
#include <gui/elements.h>
#include <input/input.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    void (*on_ok)(void*);
    void* context;
} EnvMonitorViewContext;

/* Single-instance app; the view input callback has no context getter in this
 * GUI version, so the context lives here and is handed via view_set_context. */
static EnvMonitorViewContext s_view_ctx;

static bool env_monitor_view_input(InputEvent* event, void* context) {
    UNUSED(context);
    if(event->type == InputTypeShort && event->key == InputKeyOk) {
        if(s_view_ctx.on_ok) s_view_ctx.on_ok(s_view_ctx.context);
        return true;
    }
    return false;
}

static void env_monitor_view_draw(Canvas* canvas, void* _model) {
    EnvMonitorViewModel* model = _model;
    char buf[48];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);

    canvas_draw_str(canvas, 3, 9, "环境监测");
    canvas_draw_line(canvas, 0, 12, 128, 12);

    if(model->link == EnvMonitorLinkConnected) {
        snprintf(
            buf,
            sizeof(buf),
            "温度  %d.%02d C",
            model->temp_x100 / 100,
            model->temp_x100 % 100 < 0 ? -model->temp_x100 % 100 : model->temp_x100 % 100);
        canvas_draw_str(canvas, 3, 24, buf);

        snprintf(buf, sizeof(buf), "湿度  %u %%", model->humidity);
        canvas_draw_str(canvas, 3, 35, buf);

        snprintf(
            buf,
            sizeof(buf),
            "气压  %u.%u hPa",
            model->pressure_x10 / 10,
            model->pressure_x10 % 10);
        canvas_draw_str(canvas, 3, 46, buf);

        snprintf(buf, sizeof(buf), "海拔  %ld m", (long)(model->altitude_cm / 100));
        canvas_draw_str(canvas, 3, 57, buf);
        /* 校准按钮不再绘制（避免遮挡海拔显示），但 Enter 键仍进入校准菜单。 */
    } else {
        canvas_set_font(canvas, FontPrimary);
        if(model->link == EnvMonitorLinkScanning) {
            canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignCenter, "正在检测...");
        } else if(model->link == EnvMonitorLinkError) {
            canvas_draw_str_aligned(canvas, 64, 27, AlignCenter, AlignCenter, "读取失败");
            canvas_set_font(canvas, FontSecondary);
            canvas_draw_str_aligned(canvas, 64, 41, AlignCenter, AlignCenter, "检查接线与供电");
        } else {
            canvas_draw_str_aligned(canvas, 64, 27, AlignCenter, AlignCenter, "未检测到Chain ENV");
            canvas_set_font(canvas, FontSecondary);
            canvas_draw_str_aligned(canvas, 64, 41, AlignCenter, AlignCenter, "请接Cap Grove口");
        }
        elements_button_center(canvas, "重试");
    }
}

View* env_monitor_view_alloc(void) {
    View* view = view_alloc();
    memset(&s_view_ctx, 0, sizeof(s_view_ctx));
    view_allocate_model(view, ViewModelTypeLocking, sizeof(EnvMonitorViewModel));
    view_set_context(view, &s_view_ctx);
    view_set_draw_callback(view, env_monitor_view_draw);
    view_set_input_callback(view, env_monitor_view_input);
    with_view_model(
        view, EnvMonitorViewModel * model, { model->link = EnvMonitorLinkIdle; }, true);
    return view;
}

void env_monitor_view_set_ok_callback(View* view, void (*callback)(void*), void* context) {
    UNUSED(view);
    s_view_ctx.on_ok = callback;
    s_view_ctx.context = context;
}

void env_monitor_view_free(View* view) {
    s_view_ctx.on_ok = NULL;
    s_view_ctx.context = NULL;
    view_free(view);
}

void env_monitor_view_set_data(
    View* view,
    EnvMonitorLinkState link,
    int16_t temp_x100,
    uint8_t humidity,
    uint16_t pressure_x10,
    int32_t altitude_cm) {
    with_view_model(
        view,
        EnvMonitorViewModel * model,
        {
            model->link = link;
            model->temp_x100 = temp_x100;
            model->humidity = humidity;
            model->pressure_x10 = pressure_x10;
            model->altitude_cm = altitude_cm;
        },
        true);
}
