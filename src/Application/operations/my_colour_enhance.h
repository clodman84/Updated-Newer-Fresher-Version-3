#ifndef MY_COLOUR_ENHANCE_H
#define MY_COLOUR_ENHANCE_H

#include <gegl-plugin.h>
#include <gegl.h>

typedef struct _MyColourEnhance MyColourEnhance;
typedef struct _MyColourEnhanceClass MyColourEnhanceClass;

void colour_enhance_op_register(void);

#endif // MY_COLOUR_ENHANCE_H
