#include "fft.h"
#include "fft_format.h"

#include <gegl-plugin.h>
#include <gegl.h>
#include <operation/gegl-operation-filter.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

#define SPECTRAL_MAGNITUDE_TYPE (spectral_magnitude_get_type())

struct _SpectralMagnitude {
  GeglOperationFilter parent_instance;
  gboolean log_scale;
  gboolean shift;
};

struct _SpectralMagnitudeClass {
  GeglOperationFilterClass parent_class;
};

static GType spectral_magnitude_get_type(void);
G_DEFINE_TYPE(SpectralMagnitude, spectral_magnitude, GEGL_TYPE_OPERATION_FILTER)

enum { PROP_0, PROP_LOG_SCALE, PROP_SHIFT };

// Same whole-image bounding-box logic as channel_fft, and for the same
// reason: normalizing by the image's own maximum magnitude means every
// output pixel depends on every input pixel, so a partial ROI is never
// enough.
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
  // Input must be exactly the complex spectrum format channel_fft
  // produces; output is a single-channel float grayscale image, ready to
  // view or feed into any ordinary GEGL op downstream.
  gegl_operation_set_format(operation, "input", get_complex_format());
  gegl_operation_set_format(operation, "output", babl_format("Y float"));
}

static gboolean process(GeglOperation *operation, GeglBuffer *input,
                        GeglBuffer *output, const GeglRectangle *result,
                        gint level) {
#ifdef TRACY_ENABLE
  ZoneScoped;
#endif

  SpectralMagnitude *self = (SpectralMagnitude *)operation;

  const gint width = result->width;
  const gint height = result->height;
  if (width <= 0 || height <= 0)
    return TRUE;

  gint in_rowstride = 0;
  std::complex<float> *in_pixels =
      (std::complex<float> *)gegl_buffer_linear_open(
          input, result, &in_rowstride, get_complex_format());
  if (!in_pixels) {
    g_warning("spectral_magnitude: failed to open input buffer");
    return FALSE;
  }
  const gint in_row_stride_elems =
      in_rowstride / (ptrdiff_t)sizeof(std::complex<float>);

  gint out_rowstride = 0;
  gfloat *out_pixels = (gfloat *)gegl_buffer_linear_open(
      output, result, &out_rowstride, babl_format("Y float"));
  if (!out_pixels) {
    g_warning("spectral_magnitude: failed to open output buffer");
    gegl_buffer_linear_close(input, in_pixels);
    return FALSE;
  }
  const gint out_row_stride_elems = out_rowstride / (ptrdiff_t)sizeof(gfloat);

  // Scratch buffer for the raw (optionally log-compressed) magnitude,
  // laid out tightly row-major -- this is our own memory, not a
  // GeglBuffer, so there's no rowstride padding to account for.
  std::vector<float> mag(static_cast<size_t>(width) * (size_t)height);

  // Pass 1: magnitude (and optional log compression) per pixel, tracking
  // the maximum as we go so we can normalize to [0, 1] in pass 2. A raw
  // FFT magnitude spectrum is dominated by an enormous DC spike, so
  // without both the log compression and the normalization, this would
  // just render as a single white dot on a black field.
  float max_mag = 0.0f;
  for (gint y = 0; y < height; ++y) {
    const std::complex<float> *row =
        in_pixels + (ptrdiff_t)y * in_row_stride_elems;
    for (gint x = 0; x < width; ++x) {
      float m = std::abs(row[x]);
      if (self->log_scale)
        m = std::log1p(m); // log(1+m): compresses range, m=0 stays 0
      mag[(size_t)y * width + x] = m;
      max_mag = std::max(max_mag, m);
    }
  }
  const float inv_max = (max_mag > 0.0f) ? (1.0f / max_mag) : 0.0f;

  // Pass 2: write the normalized value to each output pixel, optionally
  // reading from the fftshifted source position so the DC term (which
  // channel_fft places at (0,0)) lands in the center of the image instead
  // of the corner -- the conventional way a magnitude spectrum is shown.
  for (gint y = 0; y < height; ++y) {
    gfloat *out_row = out_pixels + (ptrdiff_t)y * out_row_stride_elems;
    gint src_y = self->shift ? (y + height / 2) % height : y;
    for (gint x = 0; x < width; ++x) {
      gint src_x = self->shift ? (x + width / 2) % width : x;
      out_row[x] = mag[(size_t)src_y * width + src_x] * inv_max;
    }
  }

  gegl_buffer_linear_close(output, out_pixels);
  gegl_buffer_linear_close(input, in_pixels);
  return TRUE;
}

static void set_property(GObject *object, guint prop_id, const GValue *value,
                         GParamSpec *pspec) {
  SpectralMagnitude *self = (SpectralMagnitude *)object;
  switch (prop_id) {
  case PROP_LOG_SCALE:
    self->log_scale = g_value_get_boolean(value);
    break;
  case PROP_SHIFT:
    self->shift = g_value_get_boolean(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
  }
}

static void get_property(GObject *object, guint prop_id, GValue *value,
                         GParamSpec *pspec) {
  SpectralMagnitude *self = (SpectralMagnitude *)object;
  switch (prop_id) {
  case PROP_LOG_SCALE:
    g_value_set_boolean(value, self->log_scale);
    break;
  case PROP_SHIFT:
    g_value_set_boolean(value, self->shift);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
  }
}

// Identical delegation pattern to channel_fft's operation_process: guard
// against an unbounded input, then hand off to GeglOperationFilter's
// stock buffer-managing process via the parent class, which calls back
// into our filter-level process() above to do the real work.
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
    g_warning("spectral_magnitude: cannot process an infinite plane");
    return FALSE;
  }

  {
    GeglOperationClass *operation_class = GEGL_OPERATION_CLASS(
        g_type_class_peek_parent(GEGL_OPERATION_GET_CLASS(operation)));

    return operation_class->process(operation, context, output_prop, result,
                                    gegl_operation_context_get_level(context));
  }
}

static void spectral_magnitude_init(SpectralMagnitude *self) {
  // nothing -- defaults come from G_PARAM_CONSTRUCT below
}

static void spectral_magnitude_class_init(SpectralMagnitudeClass *klass) {
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

  gegl_operation_class_set_keys(
      operation_class, "name", "unfv3:spectral_magnitude", "title",
      "Spectral Magnitude", "categories", "fft:one_channel", "description",
      "Converts a complex FFT spectrum (as produced by channel_fft) into a "
      "normalized grayscale magnitude image, optionally log-compressed and "
      "quadrant-shifted so DC sits at the center.",
      NULL);

  g_object_class_install_property(
      object_class, PROP_LOG_SCALE,
      g_param_spec_boolean(
          "log-scale", "Log scale",
          "Compress magnitude with log(1+m) before normalizing, so the "
          "DC spike doesn't wash out everything else",
          TRUE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));

  g_object_class_install_property(
      object_class, PROP_SHIFT,
      g_param_spec_boolean(
          "shift", "Shift",
          "Swap quadrants (fftshift) so the DC term is centered instead "
          "of in the corner",
          TRUE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));
};

void spectral_magnitude_op_register(void) { spectral_magnitude_get_type(); }
