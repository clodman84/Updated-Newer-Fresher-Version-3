#ifndef FFT_H
#define FFT_H

#include <gegl-plugin.h>
#include <gegl.h>

typedef struct _FFT FFT;
typedef struct _FFTClass FFTClass;

void channel_fft_op_register(void);

typedef struct _SpectralMagnitude SpectralMagnitude;
typedef struct _SpectralMagnitudeClass SpectralMagnitudeClass;
 
void spectral_magnitude_op_register(void);

typedef struct _MagnitudeSpectrumChannel MagnitudeSpectrumChannel;
typedef struct _MagnitudeSpectrumChannelClass MagnitudeSpectrumChannelClass;

void magnitude_spectrum_channel_op_register(void);

#endif // CHANNEL_FFT_H
