#pragma once

#define RED 0x10000FF
#define GREEN 0x100FF00
#define BLUE 0x1FF0000
#define MAGENTA 0x1FF00FF
#define CYAN 0x1FFFF00

void    flash(u32 color);
void    injectPlugin(Handle process);