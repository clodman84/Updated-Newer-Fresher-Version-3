#include "fft.h"

#include <gegl-plugin.h>
#include <gegl.h>
#include <operation/gegl-operation-meta.h>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

#define MAGNITUDE_SPECTRUM_CHANNEL_TYPE                                     \
  (magnitude_spectrum_channel_get_type())

struct _MagnitudeSpectrumChannel {
  GeglOperationMeta parent_instance;
  gint channel;
  gboolean log_scale;
  gboolean shift;
};

struct _MagnitudeSpectrumChannelClass {
  GeglOperationMetaClass parent_class;
};

static GType magnitude_spectrum_channel_get_type(void);
G_DEFINE_TYPE(MagnitudeSpectrumChannel, magnitude_spectrum_channel,
             GEGL_TYPE_OPERATION_META)

enum { PROP_0, PROP_CHANNEL, PROP_LOG_SCALE, PROP_SHIFT };

// Builds input -> unfv3:channel_fft -> unfv3:spectral_magnitude -> output,
// then redirects this meta op's own properties onto the two child nodes so
// it behaves like a single ordinary operation from the outside.
static void attach(GeglOperation *operation) {
#ifdef TRACY_ENABLE
  ZoneScoped;
#endif
  GeglNode *gegl = operation->node;
  GeglNode *input = gegl_node_get_input_proxy(gegl, "input");
  GeglNode *output = gegl_node_get_output_proxy(gegl, "output");

  GeglNode *fft =
      gegl_node_new_child(gegl, "operation", "unfv3:channel_fft", NULL);
  GeglNode *mag = gegl_node_new_child(gegl, "operation",
                                      "unfv3:spectral_magnitude", NULL);

  gegl_node_link_many(input, fft, mag, output, NULL);

  gegl_operation_meta_redirect(operation, "channel", fft, "channel");
  gegl_operation_meta_redirect(operation, "log-scale", mag, "log-scale");
  gegl_operation_meta_redirect(operation, "shift", mag, "shift");
}

static void set_property(GObject *object, guint prop_id, const GValue *value,
                         GParamSpec *pspec) {
  MagnitudeSpectrumChannel *self = (MagnitudeSpectrumChannel *)object;
  switch (prop_id) {
  case PROP_CHANNEL:
    self->channel = g_value_get_int(value);
    break;
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
  MagnitudeSpectrumChannel *self = (MagnitudeSpectrumChannel *)object;
  switch (prop_id) {
  case PROP_CHANNEL:
    g_value_set_int(value, self->channel);
    break;
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

static void magnitude_spectrum_channel_init(MagnitudeSpectrumChannel *self) {
  // nothing -- defaults come from G_PARAM_CONSTRUCT below
}

static void
magnitude_spectrum_channel_class_init(MagnitudeSpectrumChannelClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  GeglOperationClass *operation_class = GEGL_OPERATION_CLASS(klass);

  object_class->set_property = set_property;
  object_class->get_property = get_property;
  operation_class->attach = attach;

  gegl_operation_class_set_keys(
      operation_class, "name", "unfv3:magnitude-spectrum-channel", "title",
      "Channel Magnitude Spectrum", "categories", "fft", "description",
      "Normalized magnitude spectrum of a single channel: channel_fft "
      "feeding directly into spectral_magnitude.",
      NULL);

  g_object_class_install_property(
      object_class, PROP_CHANNEL,
      g_param_spec_int("channel", "Channel", "Channel index to transform", 0,
                       15, 0,
                       (GParamFlags)(G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));

  g_object_class_install_property(
      object_class, PROP_LOG_SCALE,
      g_param_spec_boolean(
          "log-scale", "Log scale",
          "Compress magnitude with log(1+m) before normalizing", TRUE,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));

  g_object_class_install_property(
      object_class, PROP_SHIFT,
      g_param_spec_boolean(
          "shift", "Shift", "Swap quadrants (fftshift) so DC is centered",
          TRUE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));
};

void magnitude_spectrum_channel_op_register(void) {
  magnitude_spectrum_channel_get_type();
}
