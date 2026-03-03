#include "ui_runtime.h"

#include "vfs.h"

#include "windowman/windowman.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef enum {
    UiLifetimeScene,
    UiLifetimeGame,
} UiLifetime;

typedef enum {
    UiWidgetCanvas,
    UiWidgetPanel,
    UiWidgetLabel,
    UiWidgetImage,
    UiWidgetButton,
} UiWidgetType;

typedef struct {
    float x;
    float y;
} UiVec2;

typedef struct {
    float x;
    float y;
    float width;
    float height;
} UiRect;

typedef struct {
    char id[128];
    UiWidgetType type;
    boolean enabled;
    int z;

    char parent_id[128];
    int parent_index;

    boolean has_layout;
    UiVec2 anchor_min;
    UiVec2 anchor_max;
    UiVec2 offset_min;
    UiVec2 offset_max;

    Color color;
    char text[512];
    int font_size;
    char on_click[PATH_MAX];

    char texture_path[PATH_MAX];
    char resolved_texture_path[PATH_MAX];
    Texture2D texture;
    boolean texture_loaded;
    boolean texture_attempted;
} UiWidget;

typedef struct {
    char id[128];
    UiLifetime lifetime;
    int layer;
    usize stack_order;

    Heap widgets_heap;
    UiWidget *widgets;
    usize widget_count;
    usize widget_capacity;
} UiDocument;

struct UiRuntime {
    Heap self_heap;
    char project_root[PATH_MAX];
    char ui_root[PATH_MAX];
    ScriptRuntime *script_runtime;

    Heap documents_heap;
    UiDocument *documents;
    usize document_count;
    usize document_capacity;
    usize next_stack_order;
};

static result ui_runtime_init(UiRuntime *runtime, const char *project_root, const char *ui_root, ScriptRuntime *script_runtime);
static void ui_runtime_dispose(UiRuntime *runtime);

static result join_path(const char *base, const char *path, char *out_path, usize out_size);
static result parse_toml_file(const char *path, toml_result_t *out_parsed);

static UiDocument *find_document_by_id(UiRuntime *runtime, const char *document_id);
static void ui_document_dispose(UiDocument *document);
static result ensure_document_capacity(UiRuntime *runtime, usize required_capacity);
static result ensure_widget_capacity(UiDocument *document, usize required_capacity);

static result parse_ui_document(UiRuntime *runtime, const char *document_path, UiDocument *out_document);
static result load_document_from_path(UiRuntime *runtime, const char *document_path, boolean prefer_top_layer);
static result parse_widget_table(UiRuntime *runtime, UiWidget *widget, toml_datum_t widget_table);
static result resolve_widget_parents(UiDocument *document);
static int find_widget_index_by_id(const UiDocument *document, const char *widget_id);
static int ui_runtime_max_layer(const UiRuntime *runtime);
static int ui_runtime_min_layer(const UiRuntime *runtime);
static int ui_runtime_find_top_document_index(const UiRuntime *runtime, boolean *visited);

static UiRect resolve_widget_rect(const UiDocument *document, usize widget_index, UiRect screen_rect, int *stack_state);
static void draw_widget(UiWidget *widget, UiRect rect);
static boolean point_in_rect(Vector2 point, UiRect rect);

static result toml_number_to_float(toml_datum_t value, float *out_number) {
    if (!out_number)
        return Err;

    if (value.type == TOML_FP64) {
        *out_number = (float)value.u.fp64;
        return Ok;
    }

    if (value.type == TOML_INT64) {
        *out_number = (float)value.u.int64;
        return Ok;
    }

    return Err;
}

static result read_vec2_array(toml_datum_t table, const char *key, UiVec2 *out_value) {
    if (!out_value || table.type != TOML_TABLE || !key)
        return Err;

    toml_datum_t value = toml_get(table, key);
    if (value.type != TOML_ARRAY || value.u.arr.size < 2)
        return Err;

    if (toml_number_to_float(value.u.arr.elem[0], &out_value->x) != Ok)
        return Err;
    if (toml_number_to_float(value.u.arr.elem[1], &out_value->y) != Ok)
        return Err;

    return Ok;
}

static result read_color_array(toml_datum_t table, const char *key, Color *out_color) {
    if (!out_color || table.type != TOML_TABLE || !key)
        return Err;

    toml_datum_t value = toml_get(table, key);
    if (value.type != TOML_ARRAY || value.u.arr.size < 4)
        return Err;

    int channels[4] = {0};
    for (int index = 0; index < 4; ++index) {
        toml_datum_t channel = value.u.arr.elem[index];
        if (channel.type != TOML_INT64)
            return Err;

        int channel_value = (int)channel.u.int64;
        if (channel_value < 0)
            channel_value = 0;
        if (channel_value > 255)
            channel_value = 255;

        channels[index] = channel_value;
    }

    out_color->r = (unsigned char)channels[0];
    out_color->g = (unsigned char)channels[1];
    out_color->b = (unsigned char)channels[2];
    out_color->a = (unsigned char)channels[3];
    return Ok;
}

static result parse_widget_type(const char *type_name, UiWidgetType *out_type) {
    if (!type_name || !out_type)
        return Err;

    if (strcmp(type_name, "Canvas") == 0) {
        *out_type = UiWidgetCanvas;
        return Ok;
    }

    if (strcmp(type_name, "Panel") == 0) {
        *out_type = UiWidgetPanel;
        return Ok;
    }

    if (strcmp(type_name, "Label") == 0) {
        *out_type = UiWidgetLabel;
        return Ok;
    }

    if (strcmp(type_name, "Image") == 0) {
        *out_type = UiWidgetImage;
        return Ok;
    }

    if (strcmp(type_name, "Button") == 0) {
        *out_type = UiWidgetButton;
        return Ok;
    }

    return Err;
}

static void ui_widget_init(UiWidget *widget) {
    if (!widget)
        return;

    memset(widget, 0, sizeof(*widget));
    widget->enabled = True;
    widget->z = 0;
    widget->parent_index = -1;
    widget->anchor_min = (UiVec2){0.0f, 0.0f};
    widget->anchor_max = (UiVec2){0.0f, 0.0f};
    widget->offset_min = (UiVec2){0.0f, 0.0f};
    widget->offset_max = (UiVec2){0.0f, 0.0f};
    widget->color = (Color){255, 255, 255, 255};
    widget->font_size = 20;
}

static void ui_widget_dispose(UiWidget *widget) {
    if (!widget)
        return;

    if (widget->texture_loaded) {
        UnloadTexture(widget->texture);
        widget->texture_loaded = False;
    }

    widget->texture_attempted = False;
}

static void ui_document_init(UiDocument *document) {
    if (!document)
        return;

    memset(document, 0, sizeof(*document));
    document->lifetime = UiLifetimeScene;
    document->layer = 0;
}

result ui_runtime_create(UiRuntime **out_runtime, const char *project_root, const char *ui_root, ScriptRuntime *script_runtime) {
    if (!out_runtime || !project_root || !ui_root)
        return Err;

    *out_runtime = Null;

    Heap runtime_heap = allocate(1, sizeof(UiRuntime));
    UiRuntime *runtime = (UiRuntime *)runtime_heap.pointer;
    if (!runtime)
        return Err;

    if (ui_runtime_init(runtime, project_root, ui_root, script_runtime) != Ok) {
        deallocate(runtime_heap);
        return Err;
    }

    runtime->self_heap = runtime_heap;

    *out_runtime = runtime;
    return Ok;
}

void ui_runtime_destroy(UiRuntime *runtime) {
    if (!runtime)
        return;

    Heap self_heap = runtime->self_heap;
    ui_runtime_dispose(runtime);
    deallocate(self_heap);
}

static result ui_runtime_init(UiRuntime *runtime, const char *project_root, const char *ui_root, ScriptRuntime *script_runtime) {
    if (!runtime || !project_root || !ui_root)
        return Err;

    memset(runtime, 0, sizeof(*runtime));

    if (snprintf(runtime->project_root, sizeof(runtime->project_root), "%s", project_root) >= (int)sizeof(runtime->project_root))
        return Err;

    if (snprintf(runtime->ui_root, sizeof(runtime->ui_root), "%s", ui_root) >= (int)sizeof(runtime->ui_root))
        return Err;

    runtime->script_runtime = script_runtime;

    runtime->documents_heap = NullHeap;
    runtime->documents = Null;
    runtime->document_count = 0;
    runtime->document_capacity = 0;
    runtime->next_stack_order = 1;

    return Ok;
}

static void ui_runtime_dispose(UiRuntime *runtime) {
    if (!runtime)
        return;

    for (usize index = 0; index < runtime->document_count; ++index)
        ui_document_dispose(&runtime->documents[index]);

    if (runtime->documents_heap.pointer)
        deallocate(runtime->documents_heap);

    memset(runtime, 0, sizeof(*runtime));
}

result ui_runtime_unload_scene_documents(UiRuntime *runtime) {
    if (!runtime)
        return Err;

    usize write_index = 0;
    for (usize read_index = 0; read_index < runtime->document_count; ++read_index) {
        UiDocument *document = &runtime->documents[read_index];
        if (document->lifetime == UiLifetimeScene) {
            ui_document_dispose(document);
            continue;
        }

        if (write_index != read_index)
            runtime->documents[write_index] = runtime->documents[read_index];

        write_index++;
    }

    runtime->document_count = write_index;
    return Ok;
}

result ui_runtime_load_scene_documents(UiRuntime *runtime, toml_datum_t scene_toptab) {
    if (!runtime || scene_toptab.type != TOML_TABLE)
        return Err;

    toml_datum_t scene_table = toml_get(scene_toptab, "Scene");
    if (scene_table.type != TOML_TABLE)
        return Err;

    toml_datum_t ui_table = toml_get(scene_table, "UI");
    if (ui_table.type != TOML_TABLE)
        return Ok;

    toml_datum_t documents = toml_get(ui_table, "documents");
    if (documents.type == TOML_UNKNOWN)
        return Ok;

    if (documents.type != TOML_ARRAY) {
        log_err("Scene.UI.documents must be an array");
        return Err;
    }

    for (int index = 0; index < documents.u.arr.size; ++index) {
        toml_datum_t document_entry = documents.u.arr.elem[index];
        if (document_entry.type != TOML_STRING || !document_entry.u.s || document_entry.u.s[0] == '\0') {
            log_err("Scene.UI.documents[%d] must be a non-empty string", index);
            return Err;
        }

        char document_path[PATH_MAX] = {0};
        if (join_path(runtime->ui_root, document_entry.u.s, document_path, sizeof(document_path)) != Ok) {
            log_err("Failed to resolve Scene.UI document path '%s'", document_entry.u.s);
            return Err;
        }

        if (load_document_from_path(runtime, document_path, False) != Ok)
            return Err;
    }

    return Ok;
}

result ui_runtime_node_exists(UiRuntime *runtime, const char *document_id, const char *node_id) {
    if (!runtime || !document_id || !node_id || document_id[0] == '\0' || node_id[0] == '\0')
        return Err;

    UiDocument *document = find_document_by_id(runtime, document_id);
    if (!document)
        return Err;

    return find_widget_index_by_id(document, node_id) >= 0 ? Ok : Err;
}

result ui_runtime_set_node_visible(UiRuntime *runtime, const char *document_id, const char *node_id, boolean visible) {
    if (!runtime || !document_id || !node_id || document_id[0] == '\0' || node_id[0] == '\0')
        return Err;

    UiDocument *document = find_document_by_id(runtime, document_id);
    if (!document)
        return Err;

    const int widget_index = find_widget_index_by_id(document, node_id);
    if (widget_index < 0)
        return Err;

    document->widgets[widget_index].enabled = visible ? True : False;
    return Ok;
}

result ui_runtime_set_node_text(UiRuntime *runtime, const char *document_id, const char *node_id, const char *text) {
    if (!runtime || !document_id || !node_id || !text || document_id[0] == '\0' || node_id[0] == '\0')
        return Err;

    UiDocument *document = find_document_by_id(runtime, document_id);
    if (!document)
        return Err;

    const int widget_index = find_widget_index_by_id(document, node_id);
    if (widget_index < 0)
        return Err;

    UiWidget *widget = &document->widgets[widget_index];
    if (widget->type != UiWidgetLabel && widget->type != UiWidgetButton)
        return Err;

    if (snprintf(widget->text, sizeof(widget->text), "%s", text) >= (int)sizeof(widget->text))
        return Err;

    return Ok;
}

result ui_runtime_push_document(UiRuntime *runtime, const char *document_path) {
    if (!runtime || !document_path || document_path[0] == '\0')
        return Err;

    char resolved_document_path[PATH_MAX] = {0};
    if (join_path(runtime->ui_root, document_path, resolved_document_path, sizeof(resolved_document_path)) != Ok)
        return Err;

    return load_document_from_path(runtime, resolved_document_path, True);
}

result ui_runtime_pop_document(UiRuntime *runtime, const char *document_id) {
    if (!runtime || !document_id || document_id[0] == '\0')
        return Err;

    for (usize index = 0; index < runtime->document_count; ++index) {
        UiDocument *document = &runtime->documents[index];
        if (strcmp(document->id, document_id) != 0)
            continue;

        ui_document_dispose(document);

        for (usize move_index = index + 1; move_index < runtime->document_count; ++move_index)
            runtime->documents[move_index - 1] = runtime->documents[move_index];

        runtime->document_count--;
        return Ok;
    }

    return Err;
}

result ui_runtime_set_document_layer(UiRuntime *runtime, const char *document_id, int layer) {
    if (!runtime || !document_id || document_id[0] == '\0')
        return Err;

    UiDocument *document = find_document_by_id(runtime, document_id);
    if (!document)
        return Err;

    document->layer = layer;
    return Ok;
}

result ui_runtime_bring_document_to_front(UiRuntime *runtime, const char *document_id) {
    if (!runtime || !document_id || document_id[0] == '\0')
        return Err;

    UiDocument *document = find_document_by_id(runtime, document_id);
    if (!document)
        return Err;

    document->layer = ui_runtime_max_layer(runtime) + 1;
    document->stack_order = runtime->next_stack_order++;
    return Ok;
}

result ui_runtime_send_document_to_back(UiRuntime *runtime, const char *document_id) {
    if (!runtime || !document_id || document_id[0] == '\0')
        return Err;

    UiDocument *document = find_document_by_id(runtime, document_id);
    if (!document)
        return Err;

    document->layer = ui_runtime_min_layer(runtime) - 1;
    document->stack_order = runtime->next_stack_order++;
    return Ok;
}

result ui_runtime_get_document_count(UiRuntime *runtime, usize *out_count) {
    if (!runtime || !out_count)
        return Err;

    *out_count = runtime->document_count;
    return Ok;
}

result ui_runtime_get_document_id_at(UiRuntime *runtime, usize index, const char **out_document_id) {
    if (!runtime || !out_document_id)
        return Err;

    if (index >= runtime->document_count)
        return Err;

    Heap visited_heap = allocate(runtime->document_count, sizeof(boolean));
    boolean *visited = (boolean *)visited_heap.pointer;
    if (!visited)
        return Err;

    memset(visited, 0, runtime->document_count * sizeof(boolean));

    int found_index = -1;
    for (usize order_index = 0; order_index <= index; ++order_index) {
        found_index = ui_runtime_find_top_document_index(runtime, visited);
        if (found_index < 0)
            break;

        visited[found_index] = True;
    }

    if (found_index < 0) {
        deallocate(visited_heap);
        return Err;
    }

    *out_document_id = runtime->documents[found_index].id;
    deallocate(visited_heap);
    return Ok;
}

void ui_runtime_draw(UiRuntime *runtime) {
    if (!runtime)
        return;

    const UiRect screen_rect = {
        0.0f,
        0.0f,
        (float)GetScreenWidth(),
        (float)GetScreenHeight(),
    };

    if (runtime->document_count == 0)
        return;

    Vector2 mouse_position = GetMousePosition();
    const boolean click_pressed = IsMouseButtonPressed(MOUSE_LEFT_BUTTON) ? True : False;

    Heap visited_heap = allocate(runtime->document_count, sizeof(boolean));
    boolean *visited = (boolean *)visited_heap.pointer;
    if (!visited)
        return;

    memset(visited, 0, runtime->document_count * sizeof(boolean));

    int clicked_document_index = -1;
    int clicked_button_index = -1;
    int clicked_document_layer = -2147483647;
    usize clicked_document_stack = 0;
    int clicked_button_z = -2147483647;

    for (usize draw_order = 0; draw_order < runtime->document_count; ++draw_order) {
        const int ordered_document_index = ui_runtime_find_top_document_index(runtime, visited);
        if (ordered_document_index < 0)
            break;

        visited[ordered_document_index] = True;

        UiDocument *document = &runtime->documents[ordered_document_index];
        if (!document->widgets || document->widget_count == 0)
            continue;

        Heap stack_heap = allocate(document->widget_count, sizeof(int));
        int *stack_state = (int *)stack_heap.pointer;
        if (!stack_state)
            continue;

        memset(stack_state, 0, document->widget_count * sizeof(int));

        int document_clicked_button_index = -1;
        int document_clicked_button_z = -2147483647;

        for (usize widget_index = 0; widget_index < document->widget_count; ++widget_index) {
            UiWidget *widget = &document->widgets[widget_index];
            if (!widget->enabled)
                continue;

            UiRect rect = resolve_widget_rect(document, widget_index, screen_rect, stack_state);
            draw_widget(widget, rect);

            if (widget->type == UiWidgetButton && point_in_rect(mouse_position, rect)) {
                if (widget->z > document_clicked_button_z || (widget->z == document_clicked_button_z && (int)widget_index > document_clicked_button_index)) {
                    document_clicked_button_z = widget->z;
                    document_clicked_button_index = (int)widget_index;
                }
            }
        }

        if (document_clicked_button_index >= 0) {
            UiWidget *button = &document->widgets[document_clicked_button_index];
            if (document->layer > clicked_document_layer
                || (document->layer == clicked_document_layer && document->stack_order > clicked_document_stack)
                || (document->layer == clicked_document_layer && document->stack_order == clicked_document_stack && button->z > clicked_button_z)) {
                clicked_document_index = ordered_document_index;
                clicked_button_index = document_clicked_button_index;
                clicked_document_layer = document->layer;
                clicked_document_stack = document->stack_order;
                clicked_button_z = button->z;
            }
        }

        deallocate(stack_heap);
    }

    if (click_pressed && clicked_document_index >= 0) {
        UiDocument *clicked_document = &runtime->documents[clicked_document_index];
        UiWidget *button = &clicked_document->widgets[clicked_button_index];

        if (button->on_click[0] != '\0') {
            if (!runtime->script_runtime) {
                log_warn("UI button '%s' click ignored; script runtime unavailable", button->id);
            } else {
                (void)script_runtime_invoke_ui_callback(runtime->script_runtime, button->on_click, clicked_document->id, button->id);
            }
        }
    }

    deallocate(visited_heap);
}

static UiDocument *find_document_by_id(UiRuntime *runtime, const char *document_id) {
    if (!runtime || !document_id || document_id[0] == '\0')
        return Null;

    for (usize index = 0; index < runtime->document_count; ++index) {
        if (strcmp(runtime->documents[index].id, document_id) == 0)
            return &runtime->documents[index];
    }

    return Null;
}

static int ui_runtime_max_layer(const UiRuntime *runtime) {
    if (!runtime || runtime->document_count == 0)
        return 0;

    int max_layer = runtime->documents[0].layer;
    for (usize index = 1; index < runtime->document_count; ++index) {
        if (runtime->documents[index].layer > max_layer)
            max_layer = runtime->documents[index].layer;
    }

    return max_layer;
}

static int ui_runtime_min_layer(const UiRuntime *runtime) {
    if (!runtime || runtime->document_count == 0)
        return 0;

    int min_layer = runtime->documents[0].layer;
    for (usize index = 1; index < runtime->document_count; ++index) {
        if (runtime->documents[index].layer < min_layer)
            min_layer = runtime->documents[index].layer;
    }

    return min_layer;
}

static int ui_runtime_find_top_document_index(const UiRuntime *runtime, boolean *visited) {
    if (!runtime || !visited)
        return -1;

    int selected_index = -1;
    for (usize index = 0; index < runtime->document_count; ++index) {
        if (visited[index])
            continue;

        if (selected_index < 0) {
            selected_index = (int)index;
            continue;
        }

        const UiDocument *candidate = &runtime->documents[index];
        const UiDocument *selected = &runtime->documents[selected_index];

        if (candidate->layer < selected->layer) {
            selected_index = (int)index;
            continue;
        }

        if (candidate->layer == selected->layer && candidate->stack_order < selected->stack_order)
            selected_index = (int)index;
    }

    return selected_index;
}

static result load_document_from_path(UiRuntime *runtime, const char *document_path, boolean prefer_top_layer) {
    if (!runtime || !document_path || document_path[0] == '\0')
        return Err;

    UiDocument parsed_document;
    ui_document_init(&parsed_document);

    if (parse_ui_document(runtime, document_path, &parsed_document) != Ok) {
        ui_document_dispose(&parsed_document);
        return Err;
    }

    UiDocument *existing_document = find_document_by_id(runtime, parsed_document.id);
    if (existing_document) {
        if (existing_document->lifetime == UiLifetimeGame && parsed_document.lifetime == UiLifetimeGame) {
            ui_document_dispose(&parsed_document);
            return Ok;
        }

        log_err("Duplicate UI document id '%s'", parsed_document.id);
        ui_document_dispose(&parsed_document);
        return Err;
    }

    if (prefer_top_layer)
        parsed_document.layer = ui_runtime_max_layer(runtime) + 1;

    parsed_document.stack_order = runtime->next_stack_order++;

    if (ensure_document_capacity(runtime, runtime->document_count + 1) != Ok) {
        ui_document_dispose(&parsed_document);
        return Err;
    }

    runtime->documents[runtime->document_count++] = parsed_document;
    log_msg("Loaded UI document '%s'", parsed_document.id);
    return Ok;
}

static void ui_document_dispose(UiDocument *document) {
    if (!document)
        return;

    for (usize index = 0; index < document->widget_count; ++index)
        ui_widget_dispose(&document->widgets[index]);

    if (document->widgets_heap.pointer)
        deallocate(document->widgets_heap);

    memset(document, 0, sizeof(*document));
}

static result ensure_document_capacity(UiRuntime *runtime, usize required_capacity) {
    if (!runtime)
        return Err;

    if (required_capacity <= runtime->document_capacity)
        return Ok;

    usize next_capacity = runtime->document_capacity == 0 ? 4 : runtime->document_capacity * 2;
    while (next_capacity < required_capacity)
        next_capacity *= 2;

    const usize next_size = next_capacity * sizeof(UiDocument);
    if (!runtime->documents) {
        runtime->documents_heap = allocate(next_capacity, sizeof(UiDocument));
        runtime->documents = (UiDocument *)runtime->documents_heap.pointer;
        if (!runtime->documents)
            return Err;

        memset(runtime->documents, 0, next_size);
    } else {
        const usize old_capacity = runtime->document_capacity;
        runtime->documents_heap = reallocate(runtime->documents_heap, next_size);
        runtime->documents = (UiDocument *)runtime->documents_heap.pointer;
        if (!runtime->documents)
            return Err;

        memset(runtime->documents + old_capacity, 0, (next_capacity - old_capacity) * sizeof(UiDocument));
    }

    runtime->document_capacity = next_capacity;
    return Ok;
}

static result ensure_widget_capacity(UiDocument *document, usize required_capacity) {
    if (!document)
        return Err;

    if (required_capacity <= document->widget_capacity)
        return Ok;

    usize next_capacity = document->widget_capacity == 0 ? 8 : document->widget_capacity * 2;
    while (next_capacity < required_capacity)
        next_capacity *= 2;

    const usize next_size = next_capacity * sizeof(UiWidget);
    if (!document->widgets) {
        document->widgets_heap = allocate(next_capacity, sizeof(UiWidget));
        document->widgets = (UiWidget *)document->widgets_heap.pointer;
        if (!document->widgets)
            return Err;

        memset(document->widgets, 0, next_size);
    } else {
        const usize old_capacity = document->widget_capacity;
        document->widgets_heap = reallocate(document->widgets_heap, next_size);
        document->widgets = (UiWidget *)document->widgets_heap.pointer;
        if (!document->widgets)
            return Err;

        memset(document->widgets + old_capacity, 0, (next_capacity - old_capacity) * sizeof(UiWidget));
    }

    document->widget_capacity = next_capacity;
    return Ok;
}

static result parse_ui_document(UiRuntime *runtime, const char *document_path, UiDocument *out_document) {
    if (!runtime || !document_path || !out_document)
        return Err;

    toml_result_t parsed = {0};
    if (parse_toml_file(document_path, &parsed) != Ok)
        return Err;

    toml_datum_t ui_table = toml_get(parsed.toptab, "UI");
    if (ui_table.type != TOML_TABLE) {
        log_err("UI document '%s' is missing [UI] table", document_path);
        toml_free(parsed);
        return Err;
    }

    toml_datum_t id = toml_get(ui_table, "id");
    if (id.type != TOML_STRING || !id.u.s || id.u.s[0] == '\0') {
        log_err("UI document '%s' is missing UI.id", document_path);
        toml_free(parsed);
        return Err;
    }

    if (snprintf(out_document->id, sizeof(out_document->id), "%s", id.u.s) >= (int)sizeof(out_document->id)) {
        log_err("UI.id is too long in '%s'", document_path);
        toml_free(parsed);
        return Err;
    }

    toml_datum_t lifetime = toml_get(ui_table, "lifetime");
    if (lifetime.type == TOML_STRING && lifetime.u.s) {
        if (strcmp(lifetime.u.s, "game") == 0) {
            out_document->lifetime = UiLifetimeGame;
        } else if (strcmp(lifetime.u.s, "scene") == 0) {
            out_document->lifetime = UiLifetimeScene;
        } else {
            log_err("UI.lifetime for document '%s' must be 'scene' or 'game'", out_document->id);
            toml_free(parsed);
            return Err;
        }
    }

    toml_datum_t layer = toml_get(ui_table, "layer");
    if (layer.type == TOML_INT64)
        out_document->layer = (int)layer.u.int64;

    toml_datum_t widgets = toml_get(parsed.toptab, "Widgets");
    if (widgets.type != TOML_ARRAY) {
        log_err("UI document '%s' is missing [[Widgets]] array", document_path);
        toml_free(parsed);
        return Err;
    }

    for (int index = 0; index < widgets.u.arr.size; ++index) {
        toml_datum_t widget_table = widgets.u.arr.elem[index];
        if (widget_table.type != TOML_TABLE) {
            log_err("Widgets[%d] in '%s' must be a table", index, document_path);
            toml_free(parsed);
            return Err;
        }

        if (ensure_widget_capacity(out_document, out_document->widget_count + 1) != Ok) {
            toml_free(parsed);
            return Err;
        }

        UiWidget *widget = &out_document->widgets[out_document->widget_count];
        ui_widget_init(widget);

        if (parse_widget_table(runtime, widget, widget_table) != Ok) {
            toml_free(parsed);
            return Err;
        }

        for (usize existing_index = 0; existing_index < out_document->widget_count; ++existing_index) {
            UiWidget *existing = &out_document->widgets[existing_index];
            if (strcmp(existing->id, widget->id) == 0) {
                log_err("Duplicate widget id '%s' in UI document '%s'", widget->id, out_document->id);
                toml_free(parsed);
                return Err;
            }
        }

        out_document->widget_count++;
    }

    if (resolve_widget_parents(out_document) != Ok) {
        toml_free(parsed);
        return Err;
    }

    toml_free(parsed);
    return Ok;
}

static result parse_widget_table(UiRuntime *runtime, UiWidget *widget, toml_datum_t widget_table) {
    if (!runtime || !widget || widget_table.type != TOML_TABLE)
        return Err;

    toml_datum_t id = toml_get(widget_table, "id");
    if (id.type != TOML_STRING || !id.u.s || id.u.s[0] == '\0') {
        log_err("UI widget is missing id");
        return Err;
    }

    if (snprintf(widget->id, sizeof(widget->id), "%s", id.u.s) >= (int)sizeof(widget->id)) {
        log_err("UI widget id is too long: '%s'", id.u.s);
        return Err;
    }

    toml_datum_t type = toml_get(widget_table, "type");
    if (type.type != TOML_STRING || !type.u.s || parse_widget_type(type.u.s, &widget->type) != Ok) {
        log_err("UI widget '%s' has unsupported or missing type", widget->id);
        return Err;
    }

    toml_datum_t enabled = toml_get(widget_table, "enabled");
    if (enabled.type == TOML_BOOLEAN)
        widget->enabled = enabled.u.boolean ? True : False;

    toml_datum_t z = toml_get(widget_table, "z");
    if (z.type == TOML_INT64)
        widget->z = (int)z.u.int64;

    toml_datum_t parent = toml_get(widget_table, "parent");
    if (parent.type == TOML_STRING && parent.u.s && parent.u.s[0] != '\0') {
        if (snprintf(widget->parent_id, sizeof(widget->parent_id), "%s", parent.u.s) >= (int)sizeof(widget->parent_id)) {
            log_err("UI widget '%s' parent id is too long", widget->id);
            return Err;
        }
    }

    toml_datum_t color = toml_get(widget_table, "color");
    if (color.type != TOML_UNKNOWN && read_color_array(widget_table, "color", &widget->color) != Ok) {
        log_err("UI widget '%s' has invalid color", widget->id);
        return Err;
    }

    toml_datum_t text = toml_get(widget_table, "text");
    if (text.type == TOML_STRING && text.u.s) {
        if (snprintf(widget->text, sizeof(widget->text), "%s", text.u.s) >= (int)sizeof(widget->text)) {
            log_err("UI widget '%s' text is too long", widget->id);
            return Err;
        }
    }

    toml_datum_t font_size = toml_get(widget_table, "font_size");
    if (font_size.type == TOML_INT64 && font_size.u.int64 > 0)
        widget->font_size = (int)font_size.u.int64;

    toml_datum_t on_click = toml_get(widget_table, "on_click");
    if (on_click.type == TOML_STRING && on_click.u.s && on_click.u.s[0] != '\0') {
        if (snprintf(widget->on_click, sizeof(widget->on_click), "%s", on_click.u.s) >= (int)sizeof(widget->on_click)) {
            log_err("UI widget '%s' on_click callback is too long", widget->id);
            return Err;
        }
    }

    toml_datum_t texture = toml_get(widget_table, "texture");
    if (texture.type == TOML_STRING && texture.u.s && texture.u.s[0] != '\0') {
        if (snprintf(widget->texture_path, sizeof(widget->texture_path), "%s", texture.u.s) >= (int)sizeof(widget->texture_path)) {
            log_err("UI widget '%s' texture path is too long", widget->id);
            return Err;
        }

        if (join_path(runtime->project_root, widget->texture_path, widget->resolved_texture_path, sizeof(widget->resolved_texture_path)) != Ok) {
            log_err("Failed to resolve texture path for UI widget '%s'", widget->id);
            return Err;
        }
    }

    toml_datum_t layout = toml_get(widget_table, "Layout");
    if (layout.type == TOML_TABLE) {
        widget->has_layout = True;

        if (read_vec2_array(layout, "anchor_min", &widget->anchor_min) != Ok) {
            log_err("UI widget '%s' is missing Layout.anchor_min", widget->id);
            return Err;
        }

        if (read_vec2_array(layout, "anchor_max", &widget->anchor_max) != Ok) {
            log_err("UI widget '%s' is missing Layout.anchor_max", widget->id);
            return Err;
        }

        toml_datum_t offset_min = toml_get(layout, "offset_min");
        if (offset_min.type != TOML_UNKNOWN && read_vec2_array(layout, "offset_min", &widget->offset_min) != Ok) {
            log_err("UI widget '%s' has invalid Layout.offset_min", widget->id);
            return Err;
        }

        toml_datum_t offset_max = toml_get(layout, "offset_max");
        if (offset_max.type != TOML_UNKNOWN && read_vec2_array(layout, "offset_max", &widget->offset_max) != Ok) {
            log_err("UI widget '%s' has invalid Layout.offset_max", widget->id);
            return Err;
        }

        if (widget->anchor_min.x > widget->anchor_max.x || widget->anchor_min.y > widget->anchor_max.y) {
            log_err("UI widget '%s' has invalid layout anchors", widget->id);
            return Err;
        }
    }

    if (widget->type == UiWidgetCanvas && !widget->has_layout) {
        widget->has_layout = True;
        widget->anchor_min = (UiVec2){0.0f, 0.0f};
        widget->anchor_max = (UiVec2){1.0f, 1.0f};
    }

    return Ok;
}

static int find_widget_index_by_id(const UiDocument *document, const char *widget_id) {
    if (!document || !widget_id || widget_id[0] == '\0')
        return -1;

    for (usize index = 0; index < document->widget_count; ++index) {
        if (strcmp(document->widgets[index].id, widget_id) == 0)
            return (int)index;
    }

    return -1;
}

static result check_cycle_recursive(const UiDocument *document, int widget_index, int *states) {
    if (!document || !states || widget_index < 0 || (usize)widget_index >= document->widget_count)
        return Err;

    if (states[widget_index] == 1)
        return Err;
    if (states[widget_index] == 2)
        return Ok;

    states[widget_index] = 1;
    int parent_index = document->widgets[widget_index].parent_index;
    if (parent_index >= 0) {
        if (check_cycle_recursive(document, parent_index, states) != Ok)
            return Err;
    }

    states[widget_index] = 2;
    return Ok;
}

static result resolve_widget_parents(UiDocument *document) {
    if (!document)
        return Err;

    for (usize index = 0; index < document->widget_count; ++index) {
        UiWidget *widget = &document->widgets[index];
        widget->parent_index = -1;

        if (widget->parent_id[0] == '\0')
            continue;

        int parent_index = find_widget_index_by_id(document, widget->parent_id);
        if (parent_index < 0) {
            log_err("UI widget '%s' references unknown parent '%s'", widget->id, widget->parent_id);
            return Err;
        }

        widget->parent_index = parent_index;
    }

    Heap states_heap = allocate(document->widget_count, sizeof(int));
    int *states = (int *)states_heap.pointer;
    if (!states)
        return Err;

    memset(states, 0, document->widget_count * sizeof(int));

    for (usize index = 0; index < document->widget_count; ++index) {
        if (check_cycle_recursive(document, (int)index, states) != Ok) {
            deallocate(states_heap);
            log_err("Detected UI widget parent cycle in document '%s'", document->id);
            return Err;
        }
    }

    deallocate(states_heap);
    return Ok;
}

static UiRect resolve_rect_in_parent(const UiWidget *widget, UiRect parent_rect) {
    if (!widget->has_layout)
        return (UiRect){parent_rect.x, parent_rect.y, 0.0f, 0.0f};

    const float left = parent_rect.x + (parent_rect.width * widget->anchor_min.x) + widget->offset_min.x;
    const float top = parent_rect.y + (parent_rect.height * widget->anchor_min.y) + widget->offset_min.y;
    const float right = parent_rect.x + (parent_rect.width * widget->anchor_max.x) + widget->offset_max.x;
    const float bottom = parent_rect.y + (parent_rect.height * widget->anchor_max.y) + widget->offset_max.y;

    UiRect rect = {
        left,
        top,
        right - left,
        bottom - top,
    };

    if (rect.width < 0.0f)
        rect.width = 0.0f;
    if (rect.height < 0.0f)
        rect.height = 0.0f;

    return rect;
}

static UiRect resolve_widget_rect(const UiDocument *document, usize widget_index, UiRect screen_rect, int *stack_state) {
    if (!document || !stack_state || widget_index >= document->widget_count)
        return (UiRect){0.0f, 0.0f, 0.0f, 0.0f};

    const UiWidget *widget = &document->widgets[widget_index];

    if (stack_state[widget_index] == 1)
        return (UiRect){0.0f, 0.0f, 0.0f, 0.0f};

    stack_state[widget_index] = 1;

    UiRect parent_rect = screen_rect;
    if (widget->parent_index >= 0)
        parent_rect = resolve_widget_rect(document, (usize)widget->parent_index, screen_rect, stack_state);

    stack_state[widget_index] = 2;
    return resolve_rect_in_parent(widget, parent_rect);
}

static void try_load_widget_texture(UiWidget *widget) {
    if (!widget || widget->texture_loaded || widget->texture_attempted || widget->texture_path[0] == '\0')
        return;

    widget->texture_attempted = True;
    if (vfs_load_texture(widget->resolved_texture_path, &widget->texture) != Ok) {
        log_err("Failed to load UI texture '%s'", widget->resolved_texture_path);
        return;
    }

    widget->texture_loaded = True;
}

static void draw_widget(UiWidget *widget, UiRect rect) {
    if (!widget)
        return;

    if (widget->type == UiWidgetCanvas)
        return;

    if (widget->type == UiWidgetPanel) {
        DrawRectangleRec((Rectangle){rect.x, rect.y, rect.width, rect.height}, widget->color);
        return;
    }

    if (widget->type == UiWidgetLabel) {
        if (widget->text[0] == '\0')
            return;

        DrawText(widget->text, (int)rect.x, (int)rect.y, widget->font_size, widget->color);
        return;
    }

    if (widget->type == UiWidgetImage) {
        if (widget->texture_path[0] == '\0')
            return;

        try_load_widget_texture(widget);
        if (!widget->texture_loaded)
            return;

        DrawTexturePro(widget->texture,
            (Rectangle){0.0f, 0.0f, (float)widget->texture.width, (float)widget->texture.height},
            (Rectangle){rect.x, rect.y, rect.width, rect.height},
            (Vector2){0.0f, 0.0f},
            0.0f,
            widget->color);
        return;
    }

    if (widget->type == UiWidgetButton) {
        DrawRectangleRec((Rectangle){rect.x, rect.y, rect.width, rect.height}, widget->color);

        if (widget->text[0] != '\0') {
            const int text_width = MeasureText(widget->text, widget->font_size);
            const int text_x = (int)(rect.x + ((rect.width - (float)text_width) * 0.5f));
            const int text_y = (int)(rect.y + ((rect.height - (float)widget->font_size) * 0.5f));
            DrawText(widget->text, text_x, text_y, widget->font_size, BLACK);
        }

        return;
    }
}

static boolean point_in_rect(Vector2 point, UiRect rect) {
    if (point.x < rect.x || point.y < rect.y)
        return False;

    if (point.x > (rect.x + rect.width) || point.y > (rect.y + rect.height))
        return False;

    return True;
}

static result join_path(const char *base, const char *path, char *out_path, usize out_size) {
    return vfs_resolve_path(base, path, out_path, out_size);
}

static result parse_toml_file(const char *path, toml_result_t *out_parsed) {
    if (!path || !out_parsed)
        return Err;

    toml_result_t parsed = {0};
    if (vfs_parse_toml_file(path, &parsed) != Ok) {
        log_err("Failed to parse config '%s'", path);
        return Err;
    }

    *out_parsed = parsed;
    return Ok;
}
