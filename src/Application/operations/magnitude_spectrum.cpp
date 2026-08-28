#include "fft.h"

#include <gegl-plugin.h>
#include <gegl.h>
#include <operation/gegl-operation-meta.h>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

#define MAGNITUDE_SPECTRUM_TYPE                                     \
  (magnitude_spectrum_get_type())

struct _MagnitudeSpectrum {
  GeglOperationMeta parent_instance;
  gboolean log_scale;
};

struct _MagnitudeSpectrumClass {
  GeglOperationMetaClass parent_class;
};

static GType magnitude_spectrum_get_type(void);
G_DEFINE_TYPE(MagnitudeSpectrum, magnitude_spectrum,
             GEGL_TYPE_OPERATION_META)

enum { PROP_0, PROP_LOG_SCALE};

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
      gegl_node_new_child(gegl, "operation", "gegl:fft", "conjugate-fill", true, NULL);
  GeglNode *shift = gegl_node_new_child(gegl, "operation", "gegl:fftshift", NULL);
  GeglNode *mag = gegl_node_new_child(gegl, "operation", "gegl:fftmagnitude", NULL);
  gegl_node_link_many(input, fft, shift, mag, output, NULL);

  gegl_operation_meta_redirect(operation, "log-scale", mag, "log");
}

static void set_property(GObject *object, guint prop_id, const GValue *value,
                         GParamSpec *pspec) {
  MagnitudeSpectrum *self = (MagnitudeSpectrum *)object;
  switch (prop_id) {
  case PROP_LOG_SCALE:
    self->log_scale = g_value_get_boolean(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
  }
}

static void get_property(GObject *object, guint prop_id, GValue *value,
                         GParamSpec *pspec) {
  MagnitudeSpectrum *self = (MagnitudeSpectrum *)object;
  switch (prop_id) {
  case PROP_LOG_SCALE:
    g_value_set_boolean(value, self->log_scale);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
  }
}

static void magnitude_spectrum_init(MagnitudeSpectrum *self) {
  // nothing -- defaults come from G_PARAM_CONSTRUCT below
}

static void
magnitude_spectrum_class_init(MagnitudeSpectrumClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  GeglOperationClass *operation_class = GEGL_OPERATION_CLASS(klass);

  object_class->set_property = set_property;
  object_class->get_property = get_property;
  operation_class->attach = attach;

  gegl_operation_class_set_keys(
      operation_class, "name", "unfv3:magnitude-spectrum", "title",
      "Magnitude Spectrum", "categories", "fft", "description",
      "Normalized magnitude spectrum ",
      NULL);

  g_object_class_install_property(
      object_class, PROP_LOG_SCALE,
      g_param_spec_boolean(
          "log-scale", "Log scale",
          "Compress magnitude with log(1+m) before normalizing", TRUE,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));

};

void magnitude_spectrum_op_register(void) {
  magnitude_spectrum_get_type();
}
