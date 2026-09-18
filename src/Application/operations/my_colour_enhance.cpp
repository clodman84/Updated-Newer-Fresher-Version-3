#include "my_colour_enhance.h"
#include <gegl-plugin.h>
#include <gegl.h>

#define MY_COLOUR_ENHANCE_TYPE (my_colour_enhance_get_type())

struct _MyColourEnhance {
  GeglOperationFilter parent_instance;
};

struct _MyColourEnhanceClass {
  GeglOperationFilterClass parent_class;
};

static GType my_colour_enhance_get_type(void);

G_DEFINE_TYPE(MyColourEnhance, my_colour_enhance, GEGL_TYPE_OPERATION_FILTER)

static void buffer_get_min_max(GeglOperation *operation, GeglBuffer *buffer,
                               const GeglRectangle *result, gdouble *min,
                               gdouble *max, const Babl *format, int level) {
  GeglBufferIterator *gi;
  gi = gegl_buffer_iterator_new(buffer, result, level, format, GEGL_ACCESS_READ,
                                GEGL_ABYSS_NONE, 1);

  *min = G_MAXDOUBLE;
  *max = -G_MAXDOUBLE;

  while (gegl_buffer_iterator_next(gi)) {
    gfloat *buf = (gfloat *)gi->items[0].data;
    gint o;
    for (o = 0; o < gi->length; o++) {
      *min = MIN(buf[1], *min);
      *max = MAX(buf[1], *max);
      buf += 3;
    }
  }
}

static void prepare(GeglOperation *operation) {
  const Babl *space = gegl_operation_get_source_space(operation, "input");
  const Babl *in_format = gegl_operation_get_source_format(operation, "input");
  const Babl *format;

  if (in_format) {
    if (babl_format_has_alpha(in_format))
      format = babl_format_with_space("CIE LCH(ab) alpha float", space);
    else
      format = babl_format_with_space("CIE LCH(ab) float", space);
  } else {
    format = babl_format_with_space("CIE LCH(ab) float", space);
  }

  gegl_operation_set_format(operation, "input", format);
  gegl_operation_set_format(operation, "output", format);
}

static GeglRectangle get_required_for_output(GeglOperation *operation,
                                             const gchar *input_pad,
                                             const GeglRectangle *roi) {
  GeglRectangle result =
      *gegl_operation_source_get_bounding_box(operation, "input");

  if (gegl_rectangle_is_infinite_plane(&result))
    return *roi;

  return result;
}

static GeglRectangle get_cached_region(GeglOperation *operation,
                                       const GeglRectangle *roi) {
  GeglRectangle result =
      *gegl_operation_source_get_bounding_box(operation, "input");

  if (gegl_rectangle_is_infinite_plane(&result))
    return *roi;

  return result;
}

static gboolean process(GeglOperation *operation, GeglBuffer *input,
                        GeglBuffer *output, const GeglRectangle *result,
                        gint level) {
  const Babl *format = gegl_operation_get_format(operation, "output");
  gboolean has_alpha = babl_format_has_alpha(format);
  GeglBufferIterator *gi;
  gdouble min, max, delta;

  buffer_get_min_max(operation, input, result, &min, &max,
                     babl_format_with_space("CIE LCH(ab) float",
                                            babl_format_get_space(format)),
                     level);

  delta = max - min;
  if (!delta) {
    gegl_buffer_copy(input, NULL, GEGL_ABYSS_NONE, output, NULL);
    return TRUE;
  }

  gi = gegl_buffer_iterator_new(input, result, level, format, GEGL_ACCESS_READ,
                                GEGL_ABYSS_NONE, 2);
  gegl_buffer_iterator_add(gi, output, result, level, format, GEGL_ACCESS_WRITE,
                           GEGL_ABYSS_NONE);

  if (has_alpha) {
    while (gegl_buffer_iterator_next(gi)) {
      gfloat *in = (gfloat *)gi->items[0].data;
      gfloat *out = (gfloat *)gi->items[1].data;
      gint i;
      for (i = 0; i < gi->length; i++) {
        out[0] = in[0];
        out[1] = (in[1] - min) / delta * 100.0;
        out[2] = in[2];
        out[3] = in[3];
        in += 4;
        out += 4;
      }
    }
  } else {
    while (gegl_buffer_iterator_next(gi)) {
      gfloat *in = (gfloat *)gi->items[0].data;
      gfloat *out = (gfloat *)gi->items[1].data;
      gint i;
      for (i = 0; i < gi->length; i++) {
        out[0] = in[0];
        out[1] = (in[1] - min) / delta * 100.0;
        out[2] = in[2];
        in += 3;
        out += 3;
      }
    }
  }

  return TRUE;
}

static gboolean operation_process(GeglOperation *operation,
                                  GeglOperationContext *context,
                                  const gchar *output_prop,
                                  const GeglRectangle *result, gint level) {
  GeglOperationClass *operation_class;
  const GeglRectangle *in_rect =
      gegl_operation_source_get_bounding_box(operation, "input");

  operation_class = GEGL_OPERATION_CLASS(my_colour_enhance_parent_class);

  if (in_rect && gegl_rectangle_is_infinite_plane(in_rect)) {
    gpointer in = gegl_operation_context_get_object(context, "input");
    gegl_operation_context_take_object(context, "output",
                                       g_object_ref(G_OBJECT(in)));
    return TRUE;
  }

  return operation_class->process(operation, context, output_prop, result,
                                  gegl_operation_context_get_level(context));
}

static void my_colour_enhance_init(MyColourEnhance *self) {
  /* no properties to initialise */
}

static void my_colour_enhance_class_init(MyColourEnhanceClass *klass) {
  GeglOperationClass *operation_class = GEGL_OPERATION_CLASS(klass);
  GeglOperationFilterClass *filter_class = GEGL_OPERATION_FILTER_CLASS(klass);

  filter_class->process = process;
  operation_class->prepare = prepare;
  operation_class->process = operation_process;
  operation_class->get_required_for_output = get_required_for_output;
  operation_class->get_cached_region = get_cached_region;
  operation_class->opencl_support = FALSE;
  operation_class->threaded = FALSE;

  gegl_operation_class_set_keys(
      operation_class, "name", "unfv3:color-enhance", "title", "Color Enhance",
      "categories", "color:enhance", "description",
      "Stretch color chroma to cover maximum possible range, "
      "keeping hue and lightness untouched.",
      NULL);
}

void colour_enhance_op_register(void) {
  (void)MY_COLOUR_ENHANCE_TYPE;
  (void)my_colour_enhance_get_type();
}
