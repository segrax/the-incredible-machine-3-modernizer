typedef int BOOL;
typedef int HWND;
typedef int HDC;

typedef struct RECT {
    int left;
    int top;
    int right;
    int bottom;
} RECT;

typedef struct Surface {
    unsigned int next;
    unsigned int reserved_04;
    HWND hwnd;
    unsigned int dib_chain;
    int x;
    int y;
    int width;
    int height;
    unsigned int reserved_20;
    unsigned int reserved_24;
    unsigned int reserved_28;
    unsigned int reserved_2c;
    unsigned int reserved_30;
    unsigned int reserved_34;
    unsigned int flags;
} Surface;

typedef BOOL (__stdcall *GetClientRectFn)(HWND hwnd, RECT *rect);
typedef BOOL (__stdcall *GetWindowRectFn)(HWND hwnd, RECT *rect);
typedef int (__stdcall *GetSystemMetricsFn)(int index);
typedef BOOL (__stdcall *MoveWindowFn)(HWND hwnd, int x, int y, int w, int h, BOOL repaint);
typedef BOOL (__stdcall *ShowWindowFn)(HWND hwnd, int command);
typedef BOOL (__stdcall *StretchBltFn)(
    HDC dest_dc,
    int dest_x,
    int dest_y,
    int dest_w,
    int dest_h,
    HDC src_dc,
    int src_x,
    int src_y,
    int src_w,
    int src_h
);

#define MEM32(addr) (*(volatile int *)(addr))

#define GET_WINDOW_RECT ((GetWindowRectFn)0x00467191)
#define GET_SYSTEM_METRICS ((GetSystemMetricsFn)0x004671A9)
#define GET_CLIENT_RECT ((GetClientRectFn)0x004671E5)
#define MOVE_WINDOW ((MoveWindowFn)0x00467311)
#define SHOW_WINDOW ((ShowWindowFn)0x0046731D)

#define MAIN_HWND MEM32(0x00490EF0)

#define TOOLBAR_HWND MEM32(0x00480FB8)
#define TOOLBAR_SURFACE ((Surface *)MEM32(0x00480FCC))
#define PLAYFIELD_HWND MEM32(0x00480FB4)
#define PARTBIN_HWND MEM32(0x00480FBC)
#define GOAL_HWND MEM32(0x00480FC0)
#define TOP_MENU_HWND MEM32(0x00480FC4)
#define MAIN_MENU_HWND MEM32(0x0047F93C)

#define PLAYFIELD_SURFACE ((Surface *)MEM32(0x00480FC8))
#define GOAL_SURFACE ((Surface *)MEM32(0x00480FD8))
#define PARTBIN_SURFACE ((Surface *)MEM32(0x00480FD0))

#define PLAY_X MEM32(0x00471D0C)
#define PLAY_Y MEM32(0x00471D10)
#define PLAY_W MEM32(0x00471D14)
#define PLAY_H MEM32(0x00471D18)

#define GOAL_X MEM32(0x00471B24)
#define GOAL_Y MEM32(0x00471B28)
#define GOAL_W MEM32(0x00471B2C)
#define GOAL_H MEM32(0x00471B30)

#define PART_X MEM32(0x00471F7C)
#define PART_Y MEM32(0x00471F80)
#define PART_W MEM32(0x00471F84)
#define PART_H MEM32(0x00471F88)

#define PART_HEADER_H MEM32(0x0047C500)
#define STRETCH_PROC MEM32(0x0047A5F4)
#define CURRENT_DEST_DC MEM32(0x004B1754)

#define MOUSE_MOVE_X MEM32(0x0049005C)
#define MOUSE_MOVE_Y MEM32(0x00490060)
#define MOUSE_DOWN_X MEM32(0x00490064)
#define MOUSE_DOWN_Y MEM32(0x00490068)

#define WMSZ_LEFT 1
#define WMSZ_RIGHT 2
#define WMSZ_TOP 3
#define WMSZ_TOPLEFT 4
#define WMSZ_TOPRIGHT 5
#define WMSZ_BOTTOM 6
#define WMSZ_BOTTOMLEFT 7
#define WMSZ_BOTTOMRIGHT 8

static int max_int(int a, int b)
{
    return a > b ? a : b;
}

static int min_client_size(int value, int min_value)
{
    return value < min_value ? min_value : value;
}

void __cdecl timwin_apply_sizing_aspect(int edge, RECT *rect)
{
    HWND parent = MAIN_HWND;
    RECT client_rect;
    RECT window_rect;
    int aspect_w;
    int aspect_h;
    int extra_w = 0;
    int extra_h = 0;
    int outer_w;
    int outer_h;
    int client_w;
    int client_h;
    int min_w;
    int target_w;
    int target_h;
    int center_x;
    int center_y;

    if (!rect) {
        return;
    }

    if (parent && GET_CLIENT_RECT(parent, &client_rect) && GET_WINDOW_RECT(parent, &window_rect)) {
        extra_w = (window_rect.right - window_rect.left) - client_rect.right;
        extra_h = (window_rect.bottom - window_rect.top) - client_rect.bottom;
        if (extra_w < 0) {
            extra_w = 0;
        }
        if (extra_h < 0) {
            extra_h = 0;
        }
    }

    aspect_w = GET_SYSTEM_METRICS(0);
    aspect_h = GET_SYSTEM_METRICS(1);
    if (aspect_w <= 0 || aspect_h <= 0) {
        aspect_w = 16;
        aspect_h = 10;
    }

    outer_w = rect->right - rect->left;
    outer_h = rect->bottom - rect->top;
    if (outer_w <= extra_w || outer_h <= extra_h) {
        return;
    }

    min_w = (480 * aspect_w + aspect_h - 1) / aspect_h;
    client_w = min_client_size(outer_w - extra_w, min_w);
    client_h = min_client_size(outer_h - extra_h, 480);

    if (edge == WMSZ_LEFT || edge == WMSZ_RIGHT) {
        client_h = (client_w * aspect_h + aspect_w / 2) / aspect_w;
    } else if (edge == WMSZ_TOP || edge == WMSZ_BOTTOM) {
        client_w = (client_h * aspect_w + aspect_h / 2) / aspect_h;
    } else if (client_w * aspect_h > client_h * aspect_w) {
        client_w = (client_h * aspect_w + aspect_h / 2) / aspect_h;
    } else {
        client_h = (client_w * aspect_h + aspect_w / 2) / aspect_w;
    }

    target_w = client_w + extra_w;
    target_h = client_h + extra_h;
    center_x = (rect->left + rect->right) / 2;
    center_y = (rect->top + rect->bottom) / 2;

    switch (edge) {
    case WMSZ_LEFT:
        rect->left = rect->right - target_w;
        rect->top = center_y - target_h / 2;
        rect->bottom = rect->top + target_h;
        break;
    case WMSZ_RIGHT:
        rect->right = rect->left + target_w;
        rect->top = center_y - target_h / 2;
        rect->bottom = rect->top + target_h;
        break;
    case WMSZ_TOP:
        rect->top = rect->bottom - target_h;
        rect->left = center_x - target_w / 2;
        rect->right = rect->left + target_w;
        break;
    case WMSZ_TOPLEFT:
        rect->left = rect->right - target_w;
        rect->top = rect->bottom - target_h;
        break;
    case WMSZ_TOPRIGHT:
        rect->right = rect->left + target_w;
        rect->top = rect->bottom - target_h;
        break;
    case WMSZ_BOTTOM:
        rect->bottom = rect->top + target_h;
        rect->left = center_x - target_w / 2;
        rect->right = rect->left + target_w;
        break;
    case WMSZ_BOTTOMLEFT:
        rect->left = rect->right - target_w;
        rect->bottom = rect->top + target_h;
        break;
    case WMSZ_BOTTOMRIGHT:
    default:
        rect->right = rect->left + target_w;
        rect->bottom = rect->top + target_h;
        break;
    }
}

static void move_child(HWND hwnd, int x, int y, int w, int h)
{
    if (hwnd) {
        MOVE_WINDOW(hwnd, x, y, w, h, 1);
    }
}

static void center_existing_child(HWND hwnd, const RECT *parent_rect)
{
    RECT child_rect;
    int parent_w;
    int parent_h;
    int child_w;
    int child_h;
    int x;
    int y;

    if (!hwnd || !parent_rect || !GET_WINDOW_RECT(hwnd, &child_rect)) {
        return;
    }

    parent_w = parent_rect->right;
    parent_h = parent_rect->bottom;
    child_w = child_rect.right - child_rect.left;
    child_h = child_rect.bottom - child_rect.top;
    if (parent_w <= 0 || parent_h <= 0 || child_w <= 0 || child_h <= 0) {
        return;
    }

    x = (parent_w - child_w) / 2;
    y = (parent_h - child_h) / 2;
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }

    MOVE_WINDOW(hwnd, x, y, child_w, child_h, 1);
}

static int scale_trunc(int value, int source_size, int dest_size)
{
    if (dest_size <= 0) {
        return value;
    }
    return (value * source_size) / dest_size;
}

static int scale_round(int value, int source_size, int dest_size)
{
    if (dest_size <= 0) {
        return value;
    }
    return ((value * source_size) + (dest_size / 2)) / dest_size;
}

static int scale_ceil(int value, int source_size, int dest_size)
{
    if (dest_size <= 0) {
        return value;
    }
    return ((value * source_size) + dest_size - 1) / dest_size;
}

static void scale_mouse_to_surface(HWND child, int x, int y, int *out_x, int *out_y)
{
    RECT rect;
    Surface *surface;
    int content_h;

    *out_x = x;
    *out_y = y;

    if (!GET_CLIENT_RECT(child, &rect)) {
        return;
    }

    if (child == PLAYFIELD_HWND) {
        surface = PLAYFIELD_SURFACE;
        if (!surface) {
            return;
        }
        *out_x = scale_trunc(x, surface->width, rect.right);
        *out_y = scale_trunc(y, surface->height, rect.bottom);
        return;
    }

    if (child == GOAL_HWND) {
        surface = GOAL_SURFACE;
        if (!surface) {
            return;
        }
        *out_x = scale_trunc(x, surface->width, rect.right);
        *out_y = scale_trunc(y, surface->height, rect.bottom);
        return;
    }

    if (child == PARTBIN_HWND) {
        surface = PARTBIN_SURFACE;
        content_h = rect.bottom - PART_HEADER_H;
        if (!surface || y < 0 || content_h <= 0) {
            return;
        }
        *out_x = scale_round(x, surface->width, rect.right);
        *out_y = scale_ceil(y, surface->height, content_h);
        return;
    }

    if (child == TOOLBAR_HWND) {
        surface = TOOLBAR_SURFACE;
        if (!surface) {
            return;
        }
        *out_x = scale_trunc(x, surface->height, rect.bottom);
        *out_y = scale_trunc(y, surface->height, rect.bottom);
    }
}

void __cdecl timwin_scale_mouse_move(HWND child, int x, int y)
{
    int scaled_x;
    int scaled_y;

    scale_mouse_to_surface(child, x, y, &scaled_x, &scaled_y);
    MOUSE_MOVE_X = scaled_x;
    MOUSE_MOVE_Y = scaled_y;
}

void __cdecl timwin_scale_mouse_down(HWND child, int x, int y)
{
    int scaled_x;
    int scaled_y;

    scale_mouse_to_surface(child, x, y, &scaled_x, &scaled_y);
    MOUSE_DOWN_X = scaled_x;
    MOUSE_DOWN_Y = scaled_y;
}

void __cdecl timwin_apply_dynamic_layout(void)
{
    HWND parent = MAIN_HWND;
    RECT rect;
    int width;
    int height;
    int margin;
    int top;
    int gap;
    int right_w;
    int right_x;
    int goal_h;
    int goal_y;
    Surface *toolbar;
    int saved_flags;

    if (!parent || !GET_CLIENT_RECT(parent, &rect)) {
        return;
    }

    width = rect.right;
    height = rect.bottom;
    center_existing_child(TOP_MENU_HWND, &rect);
    center_existing_child(MAIN_MENU_HWND, &rect);

    if (width < 640 || height < 480) {
        return;
    }

    margin = width / 200;
    top = height / 16;
    gap = width / 100;
    right_w = (width * 27) / 100;
    right_x = width - margin - right_w;
    goal_h = (height * 16) / 100;
    goal_y = height - margin - goal_h;

    PLAY_X = margin;
    PLAY_Y = top;
    PLAY_W = right_x - gap - margin;
    PLAY_H = height - top - margin;

    PART_X = right_x;
    PART_Y = top;
    PART_W = right_w;
    PART_H = goal_y - gap - top;

    GOAL_X = right_x;
    GOAL_Y = goal_y;
    GOAL_W = right_w;
    GOAL_H = goal_h;

    if (TOOLBAR_HWND) {
        toolbar = TOOLBAR_SURFACE;
        saved_flags = 0;
        if (toolbar) {
            saved_flags = toolbar->flags;
            toolbar->flags |= 0x40;
        }
        MOVE_WINDOW(TOOLBAR_HWND, 0, 0, width, top, 1);
        toolbar = TOOLBAR_SURFACE;
        if (toolbar) {
            toolbar->flags = saved_flags;
        }
    }

    move_child(PLAYFIELD_HWND, PLAY_X, PLAY_Y, PLAY_W, PLAY_H);
    move_child(PARTBIN_HWND, PART_X, PART_Y, PART_W, PART_H);
    move_child(GOAL_HWND, GOAL_X, GOAL_Y, GOAL_W, GOAL_H);
}

void __cdecl timwin_force_half_screen(void)
{
    HWND parent = MAIN_HWND;
    int screen_w;
    int screen_h;
    int win_w;
    int win_h;
    int x;
    int y;

    if (!parent) {
        return;
    }

    screen_w = GET_SYSTEM_METRICS(0);
    screen_h = GET_SYSTEM_METRICS(1);
    win_w = max_int(screen_w / 2, 640);
    win_h = max_int(screen_h / 2, 480);
    x = (screen_w - win_w) / 2;
    y = (screen_h - win_h) / 2;
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }

    SHOW_WINDOW(parent, 9);
    MOVE_WINDOW(parent, x, y, win_w, win_h, 1);
    timwin_apply_dynamic_layout();
}

void __cdecl timwin_force_half_if_near_screen(void)
{
    HWND parent = MAIN_HWND;
    RECT rect;
    int width;
    int height;

    if (!parent || !GET_WINDOW_RECT(parent, &rect)) {
        return;
    }

    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
    if (width >= GET_SYSTEM_METRICS(0) - 80 && height >= GET_SYSTEM_METRICS(1) - 80) {
        timwin_force_half_screen();
    }
}

static int main_client_metric(int metric)
{
    HWND parent = MAIN_HWND;
    RECT rect;

    if (parent && GET_CLIENT_RECT(parent, &rect)) {
        if (metric == 0 && rect.right > 0) {
            return rect.right;
        }
        if (metric == 1 && rect.bottom > 0) {
            return rect.bottom;
        }
    }
    return GET_SYSTEM_METRICS(metric);
}

static int __cdecl timwin_parent_client_width(void)
{
    return main_client_metric(0);
}

static int __cdecl timwin_parent_client_height(void)
{
    return main_client_metric(1);
}

void __cdecl timwin_center_child_rect(int *x, int *y, int w, int h)
{
    HWND parent = MAIN_HWND;
    RECT rect;
    int width;
    int height;
    int centered_x;
    int centered_y;

    if (!x || !y || !parent || !GET_CLIENT_RECT(parent, &rect)) {
        return;
    }

    width = rect.right;
    height = rect.bottom;
    if (width <= 0 || height <= 0 || w <= 0 || h <= 0) {
        return;
    }

    centered_x = (width - w) / 2;
    centered_y = (height - h) / 2;
    if (centered_x < 0) {
        centered_x = 0;
    }
    if (centered_y < 0) {
        centered_y = 0;
    }

    *x = centered_x;
    *y = centered_y;
}

__declspec(naked) void __cdecl timwin_parent_client_width_metric(void)
{
    __asm {
        call timwin_parent_client_width
        ret 4
    }
}

__declspec(naked) void __cdecl timwin_parent_client_height_metric(void)
{
    __asm {
        call timwin_parent_client_height
        ret 4
    }
}

static void __cdecl timwin_partbin_content_stretch_c(Surface *surface)
{
    RECT rect;
    int dest_w;
    int dest_h;
    int src_dc;
    int dib;
    StretchBltFn stretch;

    if (!surface || !surface->hwnd || !GET_CLIENT_RECT(surface->hwnd, &rect)) {
        return;
    }

    dest_w = rect.right;
    dest_h = rect.bottom - PART_HEADER_H;
    if (dest_w <= 0 || dest_h <= 0 || !CURRENT_DEST_DC) {
        return;
    }

    stretch = (StretchBltFn)STRETCH_PROC;
    if (!stretch || !surface->dib_chain) {
        return;
    }

    dib = *(int *)surface->dib_chain;
    if (!dib) {
        return;
    }

    src_dc = *(int *)(dib + 0x94);
    if (!src_dc) {
        return;
    }

    stretch(
        CURRENT_DEST_DC,
        0,
        PART_HEADER_H,
        dest_w,
        dest_h,
        src_dc,
        0,
        0,
        surface->width,
        surface->height
    );
}

__declspec(naked) void __cdecl timwin_partbin_content_stretch(void)
{
    __asm {
        push ebx
        call timwin_partbin_content_stretch_c
        add esp, 4
        ret
    }
}
