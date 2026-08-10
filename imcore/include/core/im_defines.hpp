#pragma once

namespace zb::ui::core
{
    typedef struct _imsize_t
    {
        int width;
        int height;
    } imsize_t;

    typedef struct _impoint_t
    {
        int x;
        int y;
    } impoint_t;

    typedef struct _imarea_t
    {
        int start_x;
        int start_y;
        int end_x;
        int end_y;
    } imarea_t;
}