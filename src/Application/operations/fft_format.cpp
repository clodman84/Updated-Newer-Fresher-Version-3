#include "fft_format.h"

const Babl *get_complex_format(void) {
  static const Babl *format = NULL;
  static gsize init_done = 0;

  if (g_once_init_enter(&init_done)) {
    const Babl *type = babl_type("float");
    const Babl *real_c = babl_component_new((void *)"real", NULL);
    const Babl *imag_c = babl_component_new((void *)"imaginary", NULL);
    const Babl *model =
        babl_model_new((void *)"name", (void *)"fft", real_c, imag_c, NULL);
    format = babl_format_new("name", "fft-float", model, type, real_c, type,
                             imag_c, type, NULL);
    g_once_init_leave(&init_done, 1);
  }
  return format;
}
