#include "fft.h"
#include "fft_format.h"
#include "include/pocketfft_hdronly.h"
#include <gegl-plugin.h>
#include <gegl.h>
#include <operation/gegl-operation-filter.h>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

#define CHANNEL_FFT_TYPE (channel_fft_get_type())
struct _FFT {
  GeglOperationFilter parent_instance;
  gint channel;
};

struct _FFTClass {
  GeglOperationFilterClass parent_class;
};

static GType channel_fft_get_type(void);
G_DEFINE_TYPE(FFT, channel_fft, GEGL_TYPE_OPERATION_FILTER)

enum { PROP_0, PROP_CHANNEL };

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

static void prepare(GeglOperation *operation) {
  FFT *self = (FFT *)operation;
  const Babl *in_format = gegl_operation_get_source_format(operation, "input");
  const Babl *float_format;
  gint n_components;

  if (in_format) {
    n_components = babl_format_get_n_components(in_format);
    float_format = babl_format_n(babl_type("float"), n_components);
  } else {
    float_format = babl_format("Y float");
    n_components = 1;
  }

  // Default behaviour is that if the index > number
  // of channels, we clamp it to the last one.
  if (self->channel >= n_components)
    self->channel = n_components - 1;
  if (self->channel < 0)
    self->channel = 0;

  gegl_operation_set_format(operation, "input", float_format);
  gegl_operation_set_format(operation, "output", get_complex_format());
}

static gboolean process(GeglOperation *operation, GeglBuffer *input,
                        GeglBuffer *output, const GeglRectangle *result,
                        gint level) {
#ifdef TRACY_ENABLE
  ZoneScopedN("FFT:ChannelFFT");
#endif
  FFT *self = (FFT *)operation;
  const Babl *input_format = gegl_operation_get_format(operation, "input");
  gint n_components = babl_format_get_n_components(input_format);
  if (n_components <= 0) {
    g_warning("channel_fft: input format has no components");
    return FALSE;
  }
  gint channel = self->channel;
  if (channel < 0)
    channel = 0;
  if (channel >= n_components)
    channel = n_components - 1;
  const gint width = result->width;
  const gint height = result->height;
  const gint half_width = width / 2 + 1;

#ifdef TRACY_ENABLE
  {
    char buf[128];
    snprintf(buf, sizeof(buf), "%dx%d ch=%d/%d", width, height, channel, n_components);
    ZoneText(buf, strlen(buf));
  }
#endif

  gint rowstride = 0;
  gfloat *pixels = nullptr;
  {
#ifdef TRACY_ENABLE
    ZoneScopedN("FFT:OpenInput");
#endif
    pixels = (gfloat *)gegl_buffer_linear_open(input, result, &rowstride,
                                               input_format);
  }
  if (!pixels) {
    g_warning("channel_fft: failed to open input buffer");
    return FALSE;
  }
  const float *chan_ptr = pixels + channel;
  pocketfft::shape_t shape = {(size_t)height, (size_t)width};
  pocketfft::stride_t stride_in = {rowstride,
                                   n_components * (ptrdiff_t)sizeof(float)};
  // pocketfft's r2c only ever computes the non-redundant half-spectrum
  // (width/2+1 columns per row); the rest is recovered below via
  // Hermitian symmetry, not computed directly.
  pocketfft::shape_t shape_out = {(size_t)height, (size_t)half_width};

  gint out_rowstride = 0;
  std::complex<float> *out_pixels = nullptr;
  {
#ifdef TRACY_ENABLE
    ZoneScopedN("FFT:OpenOutput");
#endif
    out_pixels = (std::complex<float> *)gegl_buffer_linear_open(
        output, result, &out_rowstride, get_complex_format());
  }
  if (!out_pixels) {
    g_warning("channel_fft: failed to open output buffer");
#ifdef TRACY_ENABLE
    ZoneScopedN("FFT:CloseInput");
#endif
    gegl_buffer_linear_close(input, pixels);
    return FALSE;
  }
  // out_rowstride is in bytes; work out the stride in complex<float> units
  // so we can index full rows for the symmetry-fill pass below.
  const gint out_row_stride_elems =
      out_rowstride / (ptrdiff_t)sizeof(std::complex<float>);
  pocketfft::stride_t stride_out = {(ptrdiff_t)out_rowstride,
                                    (ptrdiff_t)sizeof(std::complex<float>)};
  {
#ifdef TRACY_ENABLE
    ZoneScopedN("FFT:PocketFFTZone");
#endif
    pocketfft::r2c(shape, stride_in, stride_out, {0, 1}, pocketfft::FORWARD,
                   chan_ptr, out_pixels, 1.0f, 0);
  }
  // Fill in the remaining columns (half_width .. width-1) using the
  // Hermitian symmetry of a real-input 2D DFT:
  //   X[y, x] = conj(X[(height - y) % height, width - x])
  // for x in [half_width, width).
  {
#ifdef TRACY_ENABLE
    ZoneScopedN("FFT:ConjugateFill");
#endif
    for (gint y = 0; y < height; ++y) {
      std::complex<float> *row = out_pixels + (ptrdiff_t)y * out_row_stride_elems;
      gint mirror_y = (height - y) % height;
      std::complex<float> *mirror_row =
          out_pixels + (ptrdiff_t)mirror_y * out_row_stride_elems;
      for (gint x = half_width; x < width; ++x) {
        gint src_x = width - x;
        row[x] = std::conj(mirror_row[src_x]);
      }
    }
  }
  {
#ifdef TRACY_ENABLE
    ZoneScopedN("FFT:CloseOutput");
#endif
    gegl_buffer_linear_close(output, out_pixels);
  }
  {
#ifdef TRACY_ENABLE
    ZoneScopedN("FFT:CloseInput");
#endif
    gegl_buffer_linear_close(input, pixels);
  }
  return TRUE;
}

static void set_property(GObject *object, guint prop_id, const GValue *value,
                         GParamSpec *pspec) {
  FFT *self = (FFT *)object;
  switch (prop_id) {
  case PROP_CHANNEL:
    self->channel = g_value_get_int(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
  }
}

static void get_property(GObject *object, guint prop_id, GValue *value,
                         GParamSpec *pspec) {
  FFT *self = (FFT *)object;
  switch (prop_id) {
  case PROP_CHANNEL:
    g_value_set_int(value, self->channel);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
  }
}

static gboolean operation_process(GeglOperation *operation,
                                  GeglOperationContext *context,
                                  const gchar *output_prop,
                                  const GeglRectangle *result, gint level) {
#ifdef TRACY_ENABLE
  ZoneScoped;
#endif
  const GeglRectangle *in_rect =
      gegl_operation_source_get_bounding_box(operation, "input");

  if (in_rect && gegl_rectangle_is_infinite_plane(in_rect)) {
    g_warning("channel_fft: cannot compute FFT of an infinite plane");
    return FALSE;
  }

  {
    GeglOperationClass *operation_class = GEGL_OPERATION_CLASS(
        g_type_class_peek_parent(GEGL_OPERATION_GET_CLASS(operation)));

    return operation_class->process(operation, context, output_prop, result,
                                    gegl_operation_context_get_level(context));
  }
}

static void channel_fft_init(FFT *self) {
  // nothing
}

static void channel_fft_class_init(FFTClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  GeglOperationClass *operation_class = GEGL_OPERATION_CLASS(klass);
  GeglOperationFilterClass *filter_class = GEGL_OPERATION_FILTER_CLASS(klass);

  object_class->set_property = set_property;
  object_class->get_property = get_property;

  filter_class->process = process;

  operation_class->prepare = prepare;
  operation_class->process = operation_process;
  operation_class->get_required_for_output = get_required_for_output;
  operation_class->get_cached_region = get_cached_region;
  operation_class->opencl_support = FALSE;
  operation_class->threaded = FALSE;

  gegl_operation_class_set_keys(operation_class, "name", "unfv3:channel_fft",
                                "title", "Channel FFT", "categories",
                                "fft:one_channel", "description",
                                "Performs 2D FFT on a specific channel", NULL);

  g_object_class_install_property(
      object_class, PROP_CHANNEL,
      g_param_spec_int("channel", "Channel", "Channel index to transform", 0,
                       15, 0,
                       (GParamFlags)(G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));
};

void channel_fft_op_register() {
  (void)CHANNEL_FFT_TYPE;
  (void)channel_fft_get_type();
}
