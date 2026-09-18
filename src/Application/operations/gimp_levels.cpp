#include "gimp_levels.h"
#include <cmath>
#include <gegl-plugin.h>
#include <gegl.h>

#define GIMP_LEVELS_TYPE (gimp_levels_get_type())

typedef struct _GimpLevels GimpLevels;
typedef struct _GimpLevelsClass GimpLevelsClass;

// Helper applied per sample per channel
static inline gdouble levels_map(gdouble value, gdouble low_input,
                                 gdouble high_input, gdouble inv_gamma,
                                 gdouble low_output, gdouble high_output) {
  // Remap input range to [0, 1]
  if (high_input != low_input)
    value = (value - low_input) / (high_input - low_input);
  else
    value = value - low_input;

  value = CLAMP(value, 0.0, 1.0);

  // Midtone (gamma) correction
  if (inv_gamma != 1.0 && value > 0.0)
    value = pow(value, inv_gamma);

  // Remap to output range
  if (high_output >= low_output)
    value = value * (high_output - low_output) + low_output;
  else
    value = low_output - value * (low_output - high_output);

  return CLAMP(value, 0.0, 1.0);
}

struct _GimpLevels {
  GeglOperationPointFilter parent_instance;

  // Per-channel: [0]=composite [1]=R [2]=G [3]=B [4]=A
  gdouble in_low[5];
  gdouble in_high[5];
  gdouble gamma[5]; // midtone: 1.0 = neutral, >1 brightens, <1 darkens
  gdouble out_low[5];
  gdouble out_high[5];
};

struct _GimpLevelsClass {
  GeglOperationPointFilterClass parent_class;
};

static GType gimp_levels_get_type(void);

G_DEFINE_TYPE(GimpLevels, gimp_levels, GEGL_TYPE_OPERATION_POINT_FILTER)

enum {
  PROP_0,
  // Composite channel
  PROP_IN_LOW,
  PROP_IN_HIGH,
  PROP_GAMMA,
  PROP_OUT_LOW,
  PROP_OUT_HIGH,
  // Per-channel R/G/B/A  (suffixed _R, _G, _B, _A)
  PROP_IN_LOW_R,
  PROP_IN_HIGH_R,
  PROP_GAMMA_R,
  PROP_OUT_LOW_R,
  PROP_OUT_HIGH_R,
  PROP_IN_LOW_G,
  PROP_IN_HIGH_G,
  PROP_GAMMA_G,
  PROP_OUT_LOW_G,
  PROP_OUT_HIGH_G,
  PROP_IN_LOW_B,
  PROP_IN_HIGH_B,
  PROP_GAMMA_B,
  PROP_OUT_LOW_B,
  PROP_OUT_HIGH_B,
  PROP_IN_LOW_A,
  PROP_IN_HIGH_A,
  PROP_GAMMA_A,
  PROP_OUT_LOW_A,
  PROP_OUT_HIGH_A,
  N_PROPS
};

// Convenience: map channel index → (in_low, in_high, gamma, out_low, out_high)
// prop ids
static const guint chan_props[5][5] = {
    {PROP_IN_LOW, PROP_IN_HIGH, PROP_GAMMA, PROP_OUT_LOW, PROP_OUT_HIGH},
    {PROP_IN_LOW_R, PROP_IN_HIGH_R, PROP_GAMMA_R, PROP_OUT_LOW_R,
     PROP_OUT_HIGH_R},
    {PROP_IN_LOW_G, PROP_IN_HIGH_G, PROP_GAMMA_G, PROP_OUT_LOW_G,
     PROP_OUT_HIGH_G},
    {PROP_IN_LOW_B, PROP_IN_HIGH_B, PROP_GAMMA_B, PROP_OUT_LOW_B,
     PROP_OUT_HIGH_B},
    {PROP_IN_LOW_A, PROP_IN_HIGH_A, PROP_GAMMA_A, PROP_OUT_LOW_A,
     PROP_OUT_HIGH_A},
};

static void gimp_levels_set_property(GObject *obj, guint prop_id,
                                     const GValue *value, GParamSpec *pspec) {
  GimpLevels *self = (GimpLevels *)obj;

  for (int c = 0; c < 5; c++) {
    if (prop_id == chan_props[c][0]) {
      self->in_low[c] = g_value_get_double(value);
      return;
    }
    if (prop_id == chan_props[c][1]) {
      self->in_high[c] = g_value_get_double(value);
      return;
    }
    if (prop_id == chan_props[c][2]) {
      self->gamma[c] = g_value_get_double(value);
      return;
    }
    if (prop_id == chan_props[c][3]) {
      self->out_low[c] = g_value_get_double(value);
      return;
    }
    if (prop_id == chan_props[c][4]) {
      self->out_high[c] = g_value_get_double(value);
      return;
    }
  }
  G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
}

static void gimp_levels_get_property(GObject *obj, guint prop_id, GValue *value,
                                     GParamSpec *pspec) {
  GimpLevels *self = (GimpLevels *)obj;

  for (int c = 0; c < 5; c++) {
    if (prop_id == chan_props[c][0]) {
      g_value_set_double(value, self->in_low[c]);
      return;
    }
    if (prop_id == chan_props[c][1]) {
      g_value_set_double(value, self->in_high[c]);
      return;
    }
    if (prop_id == chan_props[c][2]) {
      g_value_set_double(value, self->gamma[c]);
      return;
    }
    if (prop_id == chan_props[c][3]) {
      g_value_set_double(value, self->out_low[c]);
      return;
    }
    if (prop_id == chan_props[c][4]) {
      g_value_set_double(value, self->out_high[c]);
      return;
    }
  }
  G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
}

static gboolean gimp_levels_process(GeglOperation *op, void *in_buf,
                                    void *out_buf, glong n_pixels,
                                    const GeglRectangle *roi, gint level) {
  GimpLevels *self = (GimpLevels *)op;
  gfloat *src = (gfloat *)in_buf;
  gfloat *dest = (gfloat *)out_buf;

  gdouble inv_gamma[5];
  for (int c = 0; c < 5; c++) {
    if (self->gamma[c] == 0.0)
      return FALSE;
    inv_gamma[c] = 1.0 / self->gamma[c];
  }

  while (n_pixels--) {
    for (int c = 0; c < 4; c++) {
      gdouble v = src[c];

      // Per-channel pass (channels 1–4 map to R,G,B,A)
      v = levels_map(v, self->in_low[c + 1], self->in_high[c + 1],
                     inv_gamma[c + 1], self->out_low[c + 1],
                     self->out_high[c + 1]);

      // Composite pass — applied to RGB only, not alpha (c==3)
      if (c != 3) {
        v = levels_map(v, self->in_low[0], self->in_high[0], inv_gamma[0],
                       self->out_low[0], self->out_high[0]);
      }

      dest[c] = (gfloat)v;
    }
    src += 4;
    dest += 4;
  }
  return TRUE;
}

static void gimp_levels_class_init(GimpLevelsClass *klass) {
  GObjectClass *obj_class = G_OBJECT_CLASS(klass);
  GeglOperationClass *op_class = GEGL_OPERATION_CLASS(klass);
  GeglOperationPointFilterClass *pt_class =
      GEGL_OPERATION_POINT_FILTER_CLASS(klass);

  obj_class->set_property = gimp_levels_set_property;
  obj_class->get_property = gimp_levels_get_property;
  pt_class->process = gimp_levels_process;

  // Request linear-light float RGBA so our pow() gamma math is correct.
  // Blit output is still converted back to R'G'B'A u8 by the caller.
  gegl_operation_class_set_keys(
      op_class, "name", "unfv3:gimp-levels", "title", "GIMP-style Levels",
      "categories", "color", "description",
      "Per-channel levels with gamma midtone adjustment", NULL);

  // Helper macro: install one double property
#define INSTALL_DOUBLE(id, name, default_val, min_val, max_val)                \
  g_object_class_install_property(                                             \
      obj_class, id,                                                           \
      g_param_spec_double(                                                     \
          name, name, name, min_val, max_val, default_val,                     \
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));

  // Composite channel
  INSTALL_DOUBLE(PROP_IN_LOW, "in-low", 0.0, 0.0, 1.0)
  INSTALL_DOUBLE(PROP_IN_HIGH, "in-high", 1.0, 0.0, 1.0)
  INSTALL_DOUBLE(PROP_GAMMA, "gamma", 1.0, 0.1, 10.0)
  INSTALL_DOUBLE(PROP_OUT_LOW, "out-low", 0.0, 0.0, 1.0)
  INSTALL_DOUBLE(PROP_OUT_HIGH, "out-high", 1.0, 0.0, 1.0)
  // R channel
  INSTALL_DOUBLE(PROP_IN_LOW_R, "in-low-r", 0.0, 0.0, 1.0)
  INSTALL_DOUBLE(PROP_IN_HIGH_R, "in-high-r", 1.0, 0.0, 1.0)
  INSTALL_DOUBLE(PROP_GAMMA_R, "gamma-r", 1.0, 0.1, 10.0)
  INSTALL_DOUBLE(PROP_OUT_LOW_R, "out-low-r", 0.0, 0.0, 1.0)
  INSTALL_DOUBLE(PROP_OUT_HIGH_R, "out-high-r", 1.0, 0.0, 1.0)
  // G channel
  INSTALL_DOUBLE(PROP_IN_LOW_G, "in-low-g", 0.0, 0.0, 1.0)
  INSTALL_DOUBLE(PROP_IN_HIGH_G, "in-high-g", 1.0, 0.0, 1.0)
  INSTALL_DOUBLE(PROP_GAMMA_G, "gamma-g", 1.0, 0.1, 10.0)
  INSTALL_DOUBLE(PROP_OUT_LOW_G, "out-low-g", 0.0, 0.0, 1.0)
  INSTALL_DOUBLE(PROP_OUT_HIGH_G, "out-high-g", 1.0, 0.0, 1.0)
  // B channel
  INSTALL_DOUBLE(PROP_IN_LOW_B, "in-low-b", 0.0, 0.0, 1.0)
  INSTALL_DOUBLE(PROP_IN_HIGH_B, "in-high-b", 1.0, 0.0, 1.0)
  INSTALL_DOUBLE(PROP_GAMMA_B, "gamma-b", 1.0, 0.1, 10.0)
  INSTALL_DOUBLE(PROP_OUT_LOW_B, "out-low-b", 0.0, 0.0, 1.0)
  INSTALL_DOUBLE(PROP_OUT_HIGH_B, "out-high-b", 1.0, 0.0, 1.0)
  // A channel
  INSTALL_DOUBLE(PROP_IN_LOW_A, "in-low-a", 0.0, 0.0, 1.0)
  INSTALL_DOUBLE(PROP_IN_HIGH_A, "in-high-a", 1.0, 0.0, 1.0)
  INSTALL_DOUBLE(PROP_GAMMA_A, "gamma-a", 1.0, 0.1, 10.0)
  INSTALL_DOUBLE(PROP_OUT_LOW_A, "out-low-a", 0.0, 0.0, 1.0)
  INSTALL_DOUBLE(PROP_OUT_HIGH_A, "out-high-a", 1.0, 0.0, 1.0)

#undef INSTALL_DOUBLE
}

static void gimp_levels_init(GimpLevels *self) {
  for (int c = 0; c < 5; c++) {
    self->in_low[c] = 0.0;
    self->in_high[c] = 1.0;
    self->gamma[c] = 1.0;
    self->out_low[c] = 0.0;
    self->out_high[c] = 1.0;
  }
}

void gimp_levels_op_register(void) {
  (void)GIMP_LEVELS_TYPE;
  (void)gimp_levels_get_type();
}
