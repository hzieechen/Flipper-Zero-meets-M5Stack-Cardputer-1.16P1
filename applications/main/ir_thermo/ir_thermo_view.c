/**
 * @file ir_thermo_view.c
 * @brief Big-number temperature read-out (128x64 Canvas, Chinese labels).
 *
 * canvas_draw_str() auto-switches to the bundled wqy12 CJK font for the
 * Chinese labels, same as the other localized apps.
 */
#include "ir_thermo_view.h"

#include <gui/canvas.h>
#include <gui/elements.h>
#include <input/input.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    void (*on_ok)(void*);
    void (*on_adj)(int dir, void*);
    void* context;
} IrThermoViewContext;

/* Single-instance app; this GUI version has no view_get_context getter. */
static IrThermoViewContext s_view_ctx;

static bool ir_thermo_view_input(InputEvent* event, void* context) {
    UNUSED(context);
    if(event->type == InputTypeShort && event->key == InputKeyOk) {
        if(s_view_ctx.on_ok) s_view_ctx.on_ok(s_view_ctx.context);
        return true;
    }
    if(event->type == InputTypeShort && event->key == InputKeyLeft) {
        if(s_view_ctx.on_adj) s_view_ctx.on_adj(-1, s_view_ctx.context);
        return true;
    }
    if(event->type == InputTypeShort && event->key == InputKeyRight) {
        if(s_view_ctx.on_adj) s_view_ctx.on_adj(+1, s_view_ctx.context);
        return true;
    }
    return false;
}

static void ir_thermo_view_draw(Canvas* canvas, void* _model) {
    IrThermoViewModel* model = _model;
    char buf[32];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 3, 9, "红外测温");

    /* Emissivity read-out, top-right, same baseline as the title (above the line). */
    snprintf(buf, sizeof(buf), "e=0.%02u", model->emissivity_x100);
    canvas_draw_str(canvas, 88, 9, buf);

    canvas_draw_line(canvas, 0, 12, 128, 12);

    if(model->state == IrThermoStateError) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignCenter, "读取失败");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 42, AlignCenter, AlignCenter, "检查NCIR2接线");
        elements_button_center(canvas, "重试");
        return;
    }

    /* Big temperature number, centred. Work on the absolute value so
     * sub-zero readings keep their sign (e.g. -50 -> "-0.50 C"). */
    int32_t t = model->temp_x100;
    uint32_t mag = t < 0 ? (uint32_t)(-(int64_t)t) : (uint32_t)t;
    snprintf(
        buf,
        sizeof(buf),
        "%s%d.%02d C",
        t < 0 ? "-" : "",
        (int)(mag / 100),
        (int)(mag % 100));
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, 64, 33, AlignCenter, AlignCenter, buf);

    canvas_set_font(canvas, FontSecondary);
    if(model->need_restart) {
        /* Emissivity changed: replace the status line with the reboot prompt,
         * so the two lines never overlap. */
        canvas_draw_str_aligned(canvas, 64, 51, AlignCenter, AlignCenter, "发射率已调整,请重启");
    } else if(model->state == IrThermoStateMeasuring) {
        canvas_draw_str_aligned(canvas, 64, 51, AlignCenter, AlignCenter, "测量中...");
    } else if(model->state == IrThermoStateStopped) {
        canvas_draw_str_aligned(canvas, 64, 51, AlignCenter, AlignCenter, "已停止");
    } else {
        canvas_draw_str_aligned(canvas, 64, 51, AlignCenter, AlignCenter, "按OK开始测量");
    }
}

View* ir_thermo_view_alloc(void) {
    View* view = view_alloc();
    memset(&s_view_ctx, 0, sizeof(s_view_ctx));
    view_allocate_model(view, ViewModelTypeLocking, sizeof(IrThermoViewModel));
    view_set_context(view, &s_view_ctx);
    view_set_draw_callback(view, ir_thermo_view_draw);
    view_set_input_callback(view, ir_thermo_view_input);
    with_view_model(
        view,
        IrThermoViewModel * model,
        {
            model->state = IrThermoStateIdle;
            model->temp_x100 = 0;
            model->emissivity_x100 = 98;
            model->need_restart = false;
        },
        true);
    return view;
}

void ir_thermo_view_free(View* view) {
    s_view_ctx.on_ok = NULL;
    s_view_ctx.on_adj = NULL;
    s_view_ctx.context = NULL;
    view_free(view);
}

void ir_thermo_view_set_ok_callback(View* view, void (*callback)(void*), void* context) {
    UNUSED(view);
    s_view_ctx.on_ok = callback;
    s_view_ctx.context = context;
}

void ir_thermo_view_set_adj_callback(View* view, void (*callback)(int dir, void*), void* context) {
    UNUSED(view);
    s_view_ctx.on_adj = callback;
    s_view_ctx.context = context;
}

void ir_thermo_view_set_data(
    View* view,
    IrThermoState state,
    int16_t temp_x100,
    uint16_t emissivity_x100) {
    with_view_model(
        view,
        IrThermoViewModel * model,
        {
            model->state = state;
            model->temp_x100 = temp_x100;
            model->emissivity_x100 = emissivity_x100;
        },
        true);
}

void ir_thermo_view_set_restart_hint(View* view, bool hint) {
    with_view_model(
        view, IrThermoViewModel * model, { model->need_restart = hint; }, true);
}
